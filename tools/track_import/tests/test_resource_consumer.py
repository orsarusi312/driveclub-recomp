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
class ResourceConsumerTests(unittest.TestCase):
    def fixture(self):
        data,patch=fixture(relocs=[(0x1808,0,0x600,0)])
        image=native.Image(io.BytesIO(data)); image.load_patches(io.BytesIO(patch))
        def write(offset,code):
            image.stream.seek(image.data_offset+offset); image.stream.write(code)
        write(0x600,bytes.fromhex('554889e5')+b'\x90'*28)
        write(0x620,b'\xe8'+struct.pack('<i',0xa00-0x625)+b'\xc3')
        write(0xa00,bytes.fromhex('554889e5c3'))
        write(0x450,bytes.fromhex('554889e5'))
        write(0x47b,b'\xe8'+struct.pack('<i',0xb80-0x480)+b'\xc3')
        options=dict(CONSUMER_SLOT=native.BASE+0x1808,CONSUMER_TARGET=native.BASE+0x600,
                     CONSUMER_HELPER_CALL=(native.BASE+0x620,native.BASE+0xa00),
                     CONSUMER_ID_RETURN_CANDIDATE=native.BASE+0x480,
                     CONSUMER_ID_HELPER=native.BASE+0xb80)
        return image,patch,options

    def test_bounded_entries_and_candidate_boundary(self):
        image,_,options=self.fixture(); before=image.stream.getvalue()
        with mock.patch.multiple(native,**options):
            result=native.resource_consumer_details(image,native.disassembler())
        self.assertEqual([r['window_bytes'] for r in result['windows']],[256,1024])
        c=result['id_reader_caller_candidate']
        self.assertTrue(c['call_encoding_matches'])
        self.assertTrue(any(r['call_boundary_matches'] for r in c['prologue_leads']))
        self.assertLessEqual(len(c['prologue_leads']),2)
        self.assertEqual(image.stream.getvalue(),before)

    def test_guard_and_overlap_refusals_and_optional_candidate(self):
        image,_,options=self.fixture(); decoder=native.disassembler()
        with mock.patch.multiple(native,**options):
            image.patches=[(0x620,1)]
            with self.assertRaises(ValueError): native.resource_consumer_details(image,decoder)
            image.patches=[(0x1808,1)]
            with self.assertRaises(ValueError): native.resource_consumer_details(image,decoder)
            image.patches=[(0xa01,1),(0x47b,1)]
            result=native.resource_consumer_details(image,decoder)
            self.assertNotIn('blocked',result['windows'][0])
            self.assertIn('blocked',result['windows'][1])
            self.assertIn('blocked',result['id_reader_caller_candidate'])
            image.patches=[]
            image.stream.seek(image.data_offset+0x47b); image.stream.write(b'\x90')
            self.assertIn('not the expected direct',native.resource_consumer_details(image,decoder)
                          ['id_reader_caller_candidate']['blocked'])
            image.relocation_records=[(0x1808,0,0x700,0)]
            with self.assertRaisesRegex(ValueError,'slot target mismatch'):
                native.resource_consumer_details(image,decoder)

    def test_profile_does_not_repeat_searches_and_preserves_inputs(self):
        image,patch,options=self.fixture(); data=image.stream.getvalue()
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'image'; q=Path(folder)/'patch'
            p.write_bytes(data); q.write_bytes(patch)
            with mock.patch.multiple(native,**options), mock.patch.object(native,'GUARDS',{}), \
                 mock.patch.object(native,'site_report',side_effect=AssertionError('repeat')), \
                 mock.patch.object(native,'direct_call_candidates',side_effect=AssertionError('scan')):
                r=native.inspect(p,q,include_resource_consumer=True)
                self.assertEqual(r['sites'],[])
                self.assertIn('resource_consumer',r)
            for extra in [dict(include_reader_details=True),dict(include_payload_details=True),
                          dict(dispatch_vtable=native.BASE+0x1800,dispatch_target=native.BASE+0x600)]:
                with self.assertRaises(ValueError): native.inspect(p,q,include_resource_consumer=True,**extra)
            self.assertEqual(p.read_bytes(),data); self.assertEqual(q.read_bytes(),patch)


if __name__ == '__main__':
    unittest.main()
