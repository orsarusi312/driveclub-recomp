# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare_trace', Path(__file__).resolve().parents[1]/'prepare_resource_trace.py')
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)

class TraceManifestTests(unittest.TestCase):
    def test_bad_spans_paths_and_headers(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'source.rpk'
            source.write_bytes(b''.join(b'EVOSLITL'+struct.pack('<QI',i,1) for i in (1,2,3)))
            row=dict(mesh_resource='0000000000000001',position_buffer='0000000000000002',index_buffer='0000000000000003',
                     source_file_spans=[dict(id=f'{i:016x}',offset=(i-1)*20,size=20) for i in (1,2,3)])
            self.assertTrue(trace.manifest(row,source,'worlds/test.rpk').startswith('rpk-trace-v1\n'))
            for path in ('../test.rpk','/test.rpk','worlds//test.rpk','worlds\\test.rpk','worlds/../test.rpk'):
                with self.assertRaises(ValueError): trace.manifest(row,source,path)
            for field,value in [('offset',-1),('size',999),('size',False),('offset',1)]:
                bad=json.loads(json.dumps(row)); bad['source_file_spans'][0][field]=value
                with self.assertRaises(ValueError): trace.manifest(bad,source,'worlds/test.rpk')
            source.write_bytes(bytes(60))
            with self.assertRaises(ValueError): trace.manifest(row,source,'worlds/test.rpk')

    @unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'),'requires dcmod fixture')
    def test_preparation_is_byte_identical_and_refuses_overwrite(self):
        fixture=Path(os.environ['TRACK_FIXTURE']); dcmod=Path(os.environ['DCMOD'])
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'trace'
            import subprocess
            scan=json.loads(subprocess.check_output([str(dcmod),'mesh','scan',str(fixture)],text=True))
            target=scan['slots'][0]['target']; before=fixture.read_bytes()
            bad=dict(scan['slots'][0]); fields=bad['draw_probe_signature'].split(':')
            fields[6]=('1' if fields[6][0]!='1' else '2')+fields[6][1:]
            bad['draw_probe_signature']=':'.join(fields)
            with self.assertRaises(ValueError): trace.manifest(bad,fixture,'worlds/test.rpk')
            report=trace.prepare(dcmod,fixture,[],target,'worlds/test.rpk',output)
            self.assertEqual((output/'mods/worlds/test.rpk').read_bytes(),before)
            self.assertEqual(fixture.read_bytes(),before)
            self.assertEqual(report['source_sha256'],report['copy_sha256'])
            self.assertIn('/app0/worlds/test.rpk\n',(output/'rpk-trace.txt').read_text())
            self.assertTrue((output/'rpk-trace.txt').read_text().startswith('rpk-trace-v2\n'))
            self.assertIn('\nprefix ',(output/'rpk-trace.txt').read_text())
            self.assertEqual((output/'mesh-search-target.txt').read_text().strip(),report['mesh_search_target'])
            with self.assertRaises(FileExistsError): trace.prepare(dcmod,fixture,[],target,'worlds/test.rpk',output)
            self.assertEqual((output/'mods/worlds/test.rpk').read_bytes(),before)
