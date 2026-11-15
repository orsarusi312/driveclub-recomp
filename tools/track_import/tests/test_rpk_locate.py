# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'set DCMOD and TRACK_FIXTURE')
class LocateTests(unittest.TestCase):
    def setUp(self):
        self.exe=str(Path(os.environ['DCMOD']).resolve())
        self.pack=Path(os.environ['TRACK_FIXTURE']).resolve()

    def runcli(self, *args):
        return subprocess.run([self.exe,*map(str,args)],text=True,capture_output=True)

    def test_spans_and_read_only(self):
        before=hashlib.sha256(self.pack.read_bytes()).hexdigest()
        scan=json.loads(self.runcli('mesh','scan',self.pack).stdout)
        for span in scan['slots'][0]['source_file_spans']:
            for delta in [0,8,span['size']-1]:
                result=self.runcli('rpk','locate',self.pack,span['offset']+delta)
                self.assertEqual(result.returncode,0,result.stderr)
                row=json.loads(result.stdout)['resource']
                self.assertEqual(row['id'],span['id'])
                self.assertEqual(row['offset_within_payload'],delta)
                self.assertEqual(row['size'],span['size'])
                self.assertTrue(row['payload_id_matches_toc'])
            end=json.loads(self.runcli('rpk','locate',self.pack,span['offset']+span['size']).stdout)['resource']
            self.assertTrue(end is None or end['id']!=span['id'])
        self.assertEqual(hashlib.sha256(self.pack.read_bytes()).hexdigest(),before)

    def test_non_payload_and_invalid_offsets(self):
        for offset in ['0','0x0',str(self.pack.stat().st_size),'18446744073709551615']:
            result=self.runcli('rpk','locate',self.pack,offset)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIsNone(json.loads(result.stdout)['resource'])
        for offset in ['-1','+1','0x','3garbage','18446744073709551616']:
            self.assertNotEqual(self.runcli('rpk','locate',self.pack,offset).returncode,0)
        with tempfile.TemporaryDirectory() as temp:
            bad=Path(temp)/'bad.rpk';bad.write_bytes(b'EVOSLITL')
            self.assertNotEqual(self.runcli('rpk','locate',bad,0).returncode,0)

    def test_header_is_checked_independently_of_toc(self):
        scan=json.loads(self.runcli('mesh','scan',self.pack).stdout)
        span=scan['slots'][0]['source_file_spans'][0]
        with tempfile.TemporaryDirectory() as temp:
            pack=Path(temp)/'fixture.rpk'
            for field in [0,8]:
                data=bytearray(self.pack.read_bytes());data[span['offset']+field]^=1;pack.write_bytes(data)
                result=self.runcli('rpk','locate',pack,span['offset'])
                self.assertEqual(result.returncode,0,result.stderr)
                row=json.loads(result.stdout)['resource']
                self.assertEqual(row['id'],span['id'])
                self.assertFalse(row['payload_id_matches_toc'])
                self.assertEqual(row['header_magic_valid'],field==8)


if __name__ == '__main__':
    unittest.main()
