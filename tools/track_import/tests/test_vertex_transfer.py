# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from test_native_caller import fixture, native


@unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
class VertexTransferTests(unittest.TestCase):
    def fixture(self, shared=False):
        data,patch=fixture(relocs=[(0x1830,0,0xc00,0),(0x1840,0,0xc00 if shared else 0xe00,0)])
        image=native.Image(io.BytesIO(data)); image.load_patches(io.BytesIO(patch))
        def write(at,value):
            image.stream.seek(image.data_offset+at); image.stream.write(value)
        calls=tuple((native.BASE+a,native.BASE+t,n) for a,t,n in
                    [(0x100,0x400,512),(0x110,0x600,512),(0x120,0x800,512),
                     (0x130,0xa00,256),(0x140,0xb00,128)])
        for call,target,_ in calls:
            write(call-native.BASE,b'\xe8'+struct.pack('<i',target-call-5))
            write(target-native.BASE,b'\xc3')
        write(0x200,b'\x48\x8d\x05'+struct.pack('<i',0x1800-16-0x207)+bytes.fromhex('4883c010488903'))
        write(0x220,bytes.fromhex('ff5030ff5040')); write(0xc00,b'\xc3'); write(0xe00,b'\xc3')
        options=dict(TRANSFER_VTABLE_LOAD=native.BASE+0x200,TRANSFER_VTABLE=native.BASE+0x1800,
                     TRANSFER_CALLS=calls,TRANSFER_VIRTUAL_SITES=((native.BASE+0x220,0x30),(native.BASE+0x223,0x40)))
        return image,patch,options

    def test_guarded_slots_and_exact_dependency_budgets(self):
        image,_,options=self.fixture(); before=image.stream.getvalue()
        with mock.patch.multiple(native,**options):
            r=native.vertex_transfer_details(image,native.disassembler())
        self.assertEqual([x['code']['window_bytes'] for x in r['direct_helpers']],[512,512,512,256,128])
        self.assertEqual([x['target'] for x in r['virtual_slots']],[hex(native.BASE+0xc00),hex(native.BASE+0xe00)])
        self.assertEqual([x['code']['window_bytes'] for x in r['virtual_slots']],[512,512])
        self.assertEqual(image.stream.getvalue(),before)

    def test_each_provenance_guard_fails_closed(self):
        for at in (0x200,0x100,0x220):
            image,_,options=self.fixture()
            image.stream.seek(image.data_offset+at); image.stream.write(b'\x90')
            with mock.patch.multiple(native,**options), self.assertRaisesRegex(ValueError,'guard mismatch'):
                native.vertex_transfer_details(image,native.disassembler())

    def test_blocked_slots_and_code_never_gain_targets_or_decodes(self):
        image,_,options=self.fixture(); decoder=native.disassembler()
        with mock.patch.multiple(native,**options):
            image.patches=[(0x1830,1),(0x800,1),(0xe00,1)]
            r=native.vertex_transfer_details(image,decoder)
            self.assertIn('blocked',r['virtual_slots'][0]); self.assertNotIn('target',r['virtual_slots'][0])
            self.assertIn('blocked',r['direct_helpers'][2]['code'])
            self.assertIn('blocked',r['virtual_slots'][1]['code'])
            image.patches=[]
            for records in [[],[(0x1830,0,0xc00,0)]*2,[(0x1830,0,0xc00,1)],
                            [(0x182f,0,0xc00,0)],[(0x1830,1,0,0)]]:
                image.relocation_records=records
                row=native.vertex_transfer_details(image,decoder)['virtual_slots'][0]
                self.assertIn('blocked',row); self.assertNotIn('target',row)

    def test_shared_method_preview_is_read_once(self):
        image,_,options=self.fixture(shared=True)
        with mock.patch.multiple(native,**options), mock.patch.object(image,'code',wraps=image.code) as reads:
            r=native.vertex_transfer_details(image,native.disassembler())
            self.assertEqual(reads.call_args_list.count(mock.call(native.BASE+0xc00,512)),1)
            self.assertEqual(r['virtual_slots'][0]['code'],r['virtual_slots'][1]['code'])

    def test_profile_exclusivity_input_integrity_and_no_previous_searches(self):
        image,patch,options=self.fixture(); data=image.stream.getvalue()
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'image'; q=Path(temp)/'patch'; p.write_bytes(data); q.write_bytes(patch)
            with mock.patch.multiple(native,**options), mock.patch.object(native,'GUARDS',{}), \
                 mock.patch.object(native,'vertex_parser_details',side_effect=AssertionError('repeat')), \
                 mock.patch.object(native,'site_report',side_effect=AssertionError('repeat')), \
                 mock.patch.object(native,'direct_call_candidates',side_effect=AssertionError('scan')):
                r=native.inspect(p,q,include_vertex_transfer=True)
                self.assertEqual(r['sites'],[]); self.assertIn('vertex_transfer',r)
            for extra in [dict(include_reader_details=True),dict(include_payload_details=True),
                          dict(include_resource_consumer=True),dict(include_vertex_parser=True),
                          dict(dispatch_vtable=native.BASE+0x1800,dispatch_target=native.BASE+0xc00)]:
                with self.assertRaises(ValueError): native.inspect(p,q,include_vertex_transfer=True,**extra)
            self.assertEqual(p.read_bytes(),data); self.assertEqual(q.read_bytes(),patch)

    def destination_fixture(self):
        image,patch,options=self.fixture()
        image.relocation_records.append((0x1838,0,0xe00,0))
        options['TRANSFER_DESTINATION_THUNK']=native.BASE+0xc00
        image.stream.seek(image.data_offset+0xc00)
        image.stream.write(bytes.fromhex('4589c14189c889d1488b17488b423831d2ffe0'))
        # Synthetic destination preparation, not a prediction of the native target.
        image.stream.seek(image.data_offset+0xe00)
        image.stream.write(bytes.fromhex('488b475848890644894608c3'))
        return image,patch,options

    def test_destination_only_reads_one_method_and_never_prior_helpers(self):
        image,_,options=self.destination_fixture(); before=image.stream.getvalue()
        with mock.patch.multiple(native,**options), mock.patch.object(image,'code',wraps=image.code) as reads:
            r=native.vertex_transfer_details(image,native.disassembler(),True)
        self.assertNotIn('blocked',r)
        self.assertEqual(r['destination_slot']['slot'],hex(native.BASE+0x1838))
        self.assertEqual(r['destination_slot']['code']['decoded_bytes'],12)
        self.assertEqual(reads.call_args_list,[mock.call(native.BASE+0x200,14),
                         mock.call(native.BASE+0x220,3),mock.call(native.BASE+0xc00,19),
                         mock.call(native.BASE+0xe00,512)])
        self.assertEqual(image.stream.getvalue(),before)

    def test_destination_parent_and_thunk_fail_closed(self):
        for kind in ('wrong-parent','wrong-thunk','patched-thunk','missing-parent'):
            image,_,options=self.destination_fixture()
            if kind=='wrong-parent': image.relocation_records[0]=(0x1830,0,0xa00,0)
            if kind=='missing-parent': image.relocation_records=image.relocation_records[1:]
            if kind=='patched-thunk': image.patches=[(0xc02,1)]
            if kind=='wrong-thunk':
                image.stream.seek(image.data_offset+0xc00); image.stream.write(b'\x90')
            with mock.patch.multiple(native,**options):
                r=native.vertex_transfer_details(image,native.disassembler(),True)
            self.assertIn('blocked',r); self.assertNotIn('destination_slot',r)

    def test_destination_metadata_and_window_protections(self):
        for records,patches in [([],[]), ([(0x1838,0,0xe00,0)]*2,[]),
              ([(0x1837,0,0xe00,0)],[]), ([(0x1838,1,0xe00,0)],[]),
              ([(0x1838,0,0xe00,1)],[]), ([(0x1838,0,0x1800,0)],[]),
              ([(0x1838,0,0xe00,0)],[(0x1838,1)]),
              ([(0x1838,0,0xe00,0)],[(0xe00,1)]),
              ([(0x1838,0,0xf00,0)],[])]:
            image,_,options=self.destination_fixture()
            image.relocation_records=image.relocation_records[:1]+records
            image.patches=patches
            with mock.patch.multiple(native,**options):
                r=native.vertex_transfer_details(image,native.disassembler(),True)
            self.assertIn('blocked',r)
            self.assertNotIn('code',r.get('destination_slot',{}))

    def test_destination_modifier_requires_transfer_profile(self):
        with self.assertRaisesRegex(ValueError,'requires --vertex-transfer'):
            native.inspect(Path('not-opened'),Path('not-opened'),destination_only=True)


if __name__ == '__main__':
    unittest.main()
