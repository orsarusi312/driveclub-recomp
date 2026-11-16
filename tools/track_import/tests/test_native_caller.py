# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import inspect_native_caller as native


def u64(*values):
    return struct.pack('<'+'Q'*len(values), *values)


def fixture(relocs=(), patches=(), segment_sizes=(0x1000,0x1000)):
    # Synthetic code and linked module, not extracted game data.
    image_size=0x1000+segment_sizes[1]
    data = bytearray(b'\x90'*image_size)
    data[0x3fb:0x400] = b'\xe8'+struct.pack('<i', 0x600-0x400)
    data[0x400:0x403] = b'\x84\xc0\xc3'
    data[0x3e0:0x3e4] = bytes.fromhex('554889e5')
    data[0x600:0x605] = bytes.fromhex('554889e5c3')
    header = b'BBPROBE6'+u64(image_size,0x300,2,len(relocs),0,1)+u64(0,0,0,0,0,1)
    header += u64(0x1000,segment_sizes[1],0x1000,0,0,0,0,0)+b'fixture\0'.ljust(32,b'\0')
    header += u64(0)+u64(0,segment_sizes[0],5)+u64(0x1000,segment_sizes[1],4)
    header += b''.join(u64(*(at if isinstance(at,tuple) else (at,0,0,0))) for at in relocs)
    patch = b'BBPATCH2'+u64(0x400000,len(patches))
    patch += b''.join(u64(at,len(value))+value for at,value in patches)
    return header+data,patch


class ImageTests(unittest.TestCase):
    def image(self, **kwargs):
        data, patch = fixture(**kwargs)
        image = native.Image(io.BytesIO(data))
        image.load_patches(io.BytesIO(patch))
        return image

    def test_offsets_and_permissions(self):
        image = self.image()
        self.assertEqual(image.code(native.BASE+0x600,5),bytes.fromhex('554889e5c3'))
        for at,n in [(native.BASE-1,1),(native.BASE+0xfff,2),(native.BASE+0x1000,1),
                     (native.BASE+0x2000,1),(native.BASE,1025),(native.BASE,0)]:
            with self.assertRaises(ValueError): image.code(at,n)

    def test_malformed_headers(self):
        data,_ = fixture()
        for invalid in [b'',b'BBPROBE5'+data[8:],data[:-1],data+b'x',data[:40]]:
            with self.assertRaises(ValueError): native.Image(io.BytesIO(invalid))
        for offset,value in [(8,2**63),(24,65),(32,1000001),(40,100001),(48,2),(96,33),(208,1)]:
            broken=bytearray(data);struct.pack_into('<Q',broken,offset,value)
            with self.assertRaises(ValueError): native.Image(io.BytesIO(broken))

    def test_patch_and_relocation_overlap_refused(self):
        image=self.image(relocs=[0x3fc])
        with self.assertRaises(ValueError): image.code(native.BASE+0x400,2)
        image=self.image(patches=[(0x400,b'\xc3')])
        with self.assertRaises(ValueError): image.code(native.BASE+0x400,2)
        self.assertEqual(image.read(native.BASE+0x400,2),b'\x84\xc0') # never applies patches
        self.assertEqual(self.image(patches=[(0x1800,b'x')]).code(native.BASE+0x400,2),b'\x84\xc0')

    def test_invalid_patches_and_relocations(self):
        for at in [0x1ffc,2**64-1]:
            with self.assertRaises(ValueError): self.image(relocs=[at])
        _,patch=fixture()
        for value in [b'',patch[:-1],patch+b'x',b'NOTPATCH'+patch[8:]]:
            with self.assertRaises(ValueError): self.image().load_patches(io.BytesIO(value))
        for at,value in [(0x2000,b'x'),(0,b''),(0,b'x'*4097)]:
            with self.assertRaises(ValueError): self.image(patches=[(at,value)])

    def test_final_page_patch_padding_only(self):
        sizes=(0x681,0x123)
        for at,value in [(0x680,b'xx'),(0x681,b'x'),(0xfff,b'x'),
                         (0x1123,b'x'),(0x1fff,b'x')]:
            with self.subTest(at=at):
                image=self.image(segment_sizes=sizes,patches=[(at,value)])
                self.assertIn((at,len(value)),image.patches)
                self.assertTrue(image.patchable(at,len(value)))
                self.assertFalse(image.mapped(at,len(value)))
                with self.assertRaises(ValueError): image.read(native.BASE+at,len(value))
                with self.assertRaises(ValueError): image.code(native.BASE+at,len(value))
        # Padding is not made available to code or relocations without a patch either.
        image=self.image(segment_sizes=sizes)
        with self.assertRaises(ValueError): image.code(native.BASE+0x681,1)
        with self.assertRaises(ValueError): self.image(segment_sizes=sizes,relocs=[0x681])

    def test_padding_boundaries_and_patch_error_context(self):
        for at,value in [(0xfff,b'xx'),(0x2000,b'x'),(0x1fff,b'xx'),
                         (2**64-1,b'xx'),(0,b''),(0,b'x'*4097)]:
            with self.subTest(at=at,size=len(value)), self.assertRaisesRegex(
                    ValueError, r'invalid patch range: patch\[1\].*offset=.*guest-address=.*size='):
                self.image(segment_sizes=(0x681,0x123),patches=[(0,b'x'),(at,value)])
        image=self.image(segment_sizes=(0x681,0x123))
        self.assertFalse(image.patchable(-1,1))
        self.assertFalse(image.patchable(0,0))
        image.segments=[(0x1000,0x123,4)]
        self.assertFalse(image.patchable(0xfff,1)) # before this segment, not leading padding
        # Raw-code overlap protection still blocks valid patches within the code window.
        image=self.image(segment_sizes=(0x681,0x123),patches=[(0x680,b'xx')])
        with self.assertRaisesRegex(ValueError,'unapplied patches'):
            image.code(native.BASE+0x680,1)

    def test_relative_vtable_metadata_only(self):
        image=self.image(relocs=[(0x1800,0,0x600,0)])
        before=image.read(native.BASE+0x1800,8)
        row=image.relative_function(native.BASE+0x1800)
        self.assertEqual(row['target'],hex(native.BASE+0x600))
        self.assertEqual(image.read(native.BASE+0x1800,8),before)
        with self.assertRaises(ValueError): image.code(native.BASE+0x1800,8)
        for records in [[],[(0x17ff,0,0x600,0)],[(0x1800,1,0,0)],
                        [(0x1800,0,0x600,1)],[(0x1800,0,0x1800,0)],
                        [(0x1800,0,0x600,0)]*2]:
            image.relocation_records=records
            with self.assertRaises(ValueError): image.relative_function(native.BASE+0x1800)
        image.relocation_records=[(0x1800,0,0x600,0)]
        image.patches=[(0x1807,1)]
        with self.assertRaises(ValueError): image.relative_function(native.BASE+0x1800)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_call_search_boundaries_budgets_and_refusals(self):
        data,patch=fixture()
        image=native.Image(io.BytesIO(data)); image.load_patches(io.BytesIO(patch))
        stream=image.stream
        # Call crosses the 1024-byte chunk boundary and must appear exactly once.
        stream.seek(image.data_offset+0x3fb);stream.write(b'\x90'*5)
        stream.seek(image.data_offset+0x3fe);stream.write(b'\xe8'+struct.pack('<i',0x600-0x403))
        decoder=native.disassembler()
        report=native.direct_call_candidates(image,native.BASE+0x600,decoder)
        self.assertTrue(report['complete'])
        self.assertEqual(report['scanned_bytes'],0x1000) # no data-segment scan
        self.assertEqual([r['call_candidate'] for r in report['candidates']],[hex(native.BASE+0x3fe)])
        limited=native.direct_call_candidates(image,native.BASE+0x600,decoder,byte_budget=1024)
        self.assertFalse(limited['complete']);self.assertEqual(limited['candidates'],[])
        limited=native.direct_call_candidates(image,native.BASE+0x600,decoder,limit=1)
        self.assertEqual(limited['stop'],'candidate-budget')
        image.patches=[(0x400,1)]
        report=native.direct_call_candidates(image,native.BASE+0x600,decoder)
        self.assertIn('blocked',report['candidates'][0])
        with self.assertRaises(ValueError): native.direct_call_candidates(image,0,decoder,limit=9)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_reader_profile_fails_closed_on_wrong_image(self):
        with self.assertRaises(ValueError): native.reader_details(self.image(),native.disassembler())

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_reader_profile_synthetic_linked_image(self):
        # Synthetic zero-filled image, only diagnostic guard bytes and a made-up
        # call/RET. No original executable, vtable or resource data is embedded.
        size=0x1600000; payload=bytearray(size)
        for address,code in native.READER_GUARDS.items():
            at=address-native.BASE;payload[at:at+len(code)]=code
        payload[0x600]=0xc3
        payload[0x800:0x805]=b'\xe8'+struct.pack('<i',0x7aaf40-0x805)
        header=b'BBPROBE6'+u64(size,0x300,2,4,0,1)+u64(0,0,0,0,0,1)
        header+=u64(0x1000000,0x600000,0x1000000,0,0,0,0,0)+b'fixture\0'.ljust(32,b'\0')
        header+=u64(0)+u64(0,0x1000000,5)+u64(0x1000000,0x600000,4)
        header+=b''.join(u64(0x15f3c40+i,0,0x600,0) for i in (0,8,16,24))
        image=native.Image(io.BytesIO(header+payload))
        report=native.reader_details(image,native.disassembler())
        self.assertEqual(report['vtable_address_point'],'0x8015f3c40')
        self.assertEqual(len(report['slots']),4)
        self.assertTrue(all(row['target']==hex(native.BASE+0x600) for row in report['slots']))
        calls=report['initializer_call_candidates']
        self.assertTrue(calls['complete'])
        self.assertEqual([r['call_candidate'] for r in calls['candidates']],[hex(native.BASE+0x800)])
        self.assertEqual(calls['scanned_bytes'],0x1000000)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_observed_dispatch_one_slot_and_first_block(self):
        image=self.image(relocs=[(0x1808,0,0x600,0)])
        decoder=native.disassembler(); before=image.stream.getvalue()
        row=native.dispatch_details(image,decoder,native.BASE+0x1800,native.BASE+0x600)
        self.assertEqual(row['linked_slot']['slot'],hex(native.BASE+0x1808))
        self.assertEqual(row['stop'],'first-control-transfer')
        self.assertEqual(row['instructions'][-1]['mnemonic'],'ret')
        self.assertEqual(image.stream.getvalue(),before)
        # A call ends the entry preview even if padding/another routine follows.
        image.stream.seek(image.data_offset+0x600)
        image.stream.write(b'\xe8\0\0\0\0\xc3')
        row=native.dispatch_details(image,decoder,native.BASE+0x1800,native.BASE+0x600)
        self.assertEqual(len(row['instructions']),1)
        self.assertEqual(row['instructions'][0]['mnemonic'],'call')
        image.stream.seek(image.data_offset+0x600); image.stream.write(b'\x90'*256)
        row=native.dispatch_details(image,decoder,native.BASE+0x1800,native.BASE+0x600)
        self.assertEqual(len(row['instructions']),64)
        self.assertEqual(row['stop'],'instruction-budget')

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_observed_dispatch_refuses_unproven_pointer_and_code(self):
        decoder=native.disassembler()
        for records,patches in [([],[]), ([(0x1808,0,0x600,1)],[]),
                               ([(0x1808,0,0x600,0)]*2,[]),
                               ([(0x1808,0,0x600,0)],[(0x1808,b'x')]),
                               ([(0x1808,0,0x600,0)],[(0x601,b'x')]),
                               ([(0x1808,0,0x600,0),(0x601,0,0x600,0)],[])]:
            image=self.image(relocs=records,patches=patches)
            with self.assertRaises(ValueError):
                native.dispatch_details(image,decoder,native.BASE+0x1800,native.BASE+0x600)
        image=self.image(relocs=[(0x1808,0,0x600,0)])
        for point in (-8,0,native.BASE+0x1801,2**64-8):
            with self.assertRaises(ValueError): native.dispatch_details(image,decoder,point,native.BASE+0x600)
        with self.assertRaisesRegex(ValueError,'differs from linked slot'):
            native.dispatch_details(image,decoder,native.BASE+0x1800,native.BASE+0x700)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_observed_dispatch_profile_is_exclusive_and_does_not_search(self):
        data,patch=fixture(relocs=[(0x1808,0,0x600,0)])
        with tempfile.TemporaryDirectory() as temp:
            image=Path(temp)/'boot.bin'; patches=Path(temp)/'patches.bin'
            image.write_bytes(data); patches.write_bytes(patch)
            options=dict(dispatch_vtable=native.BASE+0x1800,dispatch_target=native.BASE+0x600)
            with mock.patch.object(native,'GUARDS',{}), \
                 mock.patch.object(native,'site_report',side_effect=AssertionError('repeated scan')), \
                 mock.patch.object(native,'direct_call_candidates',side_effect=AssertionError('search')):
                r=native.inspect(image,patches,**options)
                self.assertEqual(r['sites'],[])
                self.assertIn('dispatch_details',r)
            for opts in [dict(dispatch_vtable=native.BASE+0x1800),
                         dict(dispatch_target=native.BASE+0x600),
                         dict(options,include_reader_details=True),
                         dict(options,include_payload_details=True)]:
                with self.assertRaises(ValueError): native.inspect(image,patches,**opts)
            self.assertEqual(image.read_bytes(),data); self.assertEqual(patches.read_bytes(),patch)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_payload_profile_guard_and_overlap(self):
        image=self.image(); decoder=native.disassembler()
        calls=((native.BASE+0x300,native.BASE+0x600),)
        image.stream.seek(image.data_offset+0x300)
        image.stream.write(b'\xe8'+struct.pack('<i',0x600-0x305))
        end_call=native.BASE+0x680
        image.stream.seek(image.data_offset+0x680)
        image.stream.write(b'\xe8'+struct.pack('<i',native.PAYLOAD_FAILURE_TARGET-(end_call+5)))
        with mock.patch.object(native,'PAYLOAD_CALLS',calls), \
             mock.patch.object(native,'PAYLOAD_END_CALLS',{native.BASE+0x600:end_call}):
            result=native.payload_details(image,decoder)
            self.assertEqual(result['helpers'][0]['instructions'][-1]['mnemonic'],'ret')
            image.patches=[(0x600,1)]
            self.assertIn('blocked',native.payload_details(image,decoder)['helpers'][0])
            image.patches=[(0x300,1)]
            with self.assertRaises(ValueError): native.payload_details(image,decoder)
        with mock.patch.object(native,'PAYLOAD_CALLS',((native.BASE+0x300,native.BASE+0x700),)):
            image.patches=[]
            with self.assertRaisesRegex(ValueError,'call guard mismatch'):
                native.payload_details(image,decoder)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_payload_reviewed_boundary_excludes_adjacent_routine(self):
        image=self.image(); decoder=native.disassembler()
        target=native.BASE+0x600; end_call=target+0x80
        image.stream.seek(image.data_offset+0x300)
        image.stream.write(b'\xe8'+struct.pack('<i',target-(native.BASE+0x305)))
        image.stream.seek(image.data_offset+0x600)
        image.stream.write(b'\xe9'+struct.pack('<i',end_call-(target+5)))
        image.stream.seek(image.data_offset+0x680)
        tail=b'\xe8'+struct.pack('<i',native.PAYLOAD_FAILURE_TARGET-(end_call+5))
        image.stream.write(tail+bytes.fromhex('554889e5c3'))
        with mock.patch.object(native,'PAYLOAD_CALLS',((native.BASE+0x300,target),)), \
             mock.patch.object(native,'PAYLOAD_END_CALLS',{target:end_call}):
            # An adjacent routine's patch is outside the reviewed window. No
            # permission/overlap rule is relaxed for the bytes actually decoded.
            image.patches=[(0x685,1)]
            row=native.payload_details(image,decoder)['helpers'][0]
            self.assertEqual(row['window_bytes'],0x85)
            self.assertEqual([i['address'] for i in row['instructions']],
                             [hex(target),hex(end_call)])
            self.assertEqual(row['stops'][0]['reason'],'window-end-fallthrough')
            self.assertEqual(row['boundary']['end_exclusive'],hex(end_call+5))
            image.patches=[(0x684,1)]
            self.assertIn('blocked',native.payload_details(image,decoder)['helpers'][0])
            image.patches=[]; image.relocations=[0x67f]
            self.assertIn('blocked',native.payload_details(image,decoder)['helpers'][0])
            image.relocations=[]
            image.stream.seek(image.data_offset+0x680); image.stream.write(b'\x90')
            self.assertIn('end-call guard mismatch',native.payload_details(image,decoder)['helpers'][0]['blocked'])

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_payload_profile_does_not_repeat_prior_searches(self):
        data,patch=fixture()
        with tempfile.TemporaryDirectory() as temp:
            image=Path(temp)/'boot.bin';patches=Path(temp)/'patches.bin'
            image.write_bytes(data);patches.write_bytes(patch)
            with mock.patch.object(native,'GUARDS',{}), mock.patch.object(native,'PAYLOAD_CALLS',()), \
                 mock.patch.object(native,'site_report',side_effect=AssertionError('repeated site scan')), \
                 mock.patch.object(native,'reader_details',side_effect=AssertionError('repeated reader scan')):
                result=native.inspect(image,patches,include_payload_details=True)
                self.assertEqual(result['sites'],[])
                self.assertIn('payload_details',result)
                self.assertEqual(image.read_bytes(),data)
                self.assertEqual(patches.read_bytes(),patch)
            with self.assertRaises(ValueError): native.inspect(image,patches,True,True)

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_bounded_call_site_candidates(self):
        image=self.image(); decoder=native.disassembler()
        report=native.site_report(image,native.BASE+0x400,decoder)
        self.assertEqual(report['status'],'decoded')
        self.assertIsNone(report['function_name'])
        self.assertEqual(report['function_boundary'],'not established')
        calls=report['call_ending_at_return_candidates']
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0]['address'],hex(native.BASE+0x3fb))
        self.assertEqual(calls[0]['direct_target'],hex(native.BASE+0x600))
        self.assertEqual(report['forward_from_return'][0]['mnemonic'],'test')
        self.assertEqual(report['prologue_candidates'][0]['address'],hex(native.BASE+0x3e0))
        self.assertLessEqual(report['forward_decoded_bytes'],256)
        blocked=native.site_report(self.image(patches=[(0x400,b'x')]),native.BASE+0x400,decoder)
        self.assertEqual(blocked['status'],'blocked')

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_no_silent_skip_of_undecodable_bytes(self):
        rows=native.instructions(native.disassembler(), b'\x90\x0f',native.BASE)
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['bytes'],'90')

    @unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
    def test_report_guards_and_unchanged_inputs(self):
        data,patch=fixture()
        with tempfile.TemporaryDirectory() as temp:
            image=Path(temp)/'boot.bin'; patches=Path(temp)/'patches.bin'
            image.write_bytes(data);patches.write_bytes(patch)
            with mock.patch.object(native,'RETURNS',(native.BASE+0x400,)), mock.patch.object(
                    native,'GUARDS',{native.BASE+0x400:b'\x84\xc0'}):
                report=native.inspect(image,patches)
                self.assertEqual(report['sites'][0]['status'],'decoded')
                self.assertNotIn('reader_details',report)
                self.assertEqual(report['image_sha256'],native.digest(image))
                self.assertEqual(image.read_bytes(),data)
                self.assertEqual(patches.read_bytes(),patch)
            with mock.patch.object(native,'GUARDS',{native.BASE+0x400:b'\x00'}):
                with self.assertRaisesRegex(ValueError,'differs from recorded guard'):
                    native.inspect(image,patches)

    def test_cli_will_not_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'evidence.json';output.write_text('keep')
            with mock.patch.object(sys,'argv',['inspect','--image','missing','--patches','missing','--output',str(output)]):
                self.assertEqual(native.main(),1)
            self.assertEqual(output.read_text(),'keep')


if __name__ == '__main__':
    unittest.main()
