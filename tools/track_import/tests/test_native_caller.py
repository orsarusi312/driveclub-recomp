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


def fixture(relocs=(), patches=()):
    # Synthetic code and linked module, not extracted game data.
    data = bytearray(b'\x90'*0x2000)
    data[0x3fb:0x400] = b'\xe8'+struct.pack('<i', 0x600-0x400)
    data[0x400:0x403] = b'\x84\xc0\xc3'
    data[0x3e0:0x3e4] = bytes.fromhex('554889e5')
    data[0x600:0x605] = bytes.fromhex('554889e5c3')
    header = b'BBPROBE6'+u64(0x2000,0x300,2,len(relocs),0,1)+u64(0,0,0,0,0,1)
    header += u64(0x1000,0x1000,0x1000,0,0,0,0,0)+b'fixture\0'.ljust(32,b'\0')
    header += u64(0)+u64(0,0x1000,5)+u64(0x1000,0x1000,4)
    header += b''.join(u64(at,0,0,0) for at in relocs)
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
        for offset,value in [(8,2**63),(24,65),(32,1000001),(40,100001),(48,2),(96,33)]:
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
