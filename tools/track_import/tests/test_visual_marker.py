import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import visual_marker as marker


@unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'requires synthetic fixture')
class MarkerTests(unittest.TestCase):
    def test_control_marker_and_shared_neighbors(self):
        exe, source = os.environ['DCMOD'], Path(os.environ['TRACK_FIXTURE'])
        with tempfile.TemporaryDirectory() as td:
            out=Path(td)/'marker'
            def scan(path):
                return json.loads(subprocess.check_output([exe,'mesh','scan',str(path)],text=True))['slots']
            before=scan(source)
            original=source.read_bytes()
            report=marker.prepare(source,before[0]['target'],[],exe,out)
            self.assertEqual(original,source.read_bytes())
            control, changed=scan(out/'control.rpk'),scan(out/'marker.rpk')
            for key in ('vertices','triangles','vertex_bounds','position_buffer','index_buffer',
                        'vertex_first','index_first','index_count','draw_probe_signature'):
                self.assertEqual(before[1][key],changed[1][key])
            for label, pack in [('before',source),('after',out/'marker.rpk')]:
                subprocess.check_call([exe,'mesh','export',str(pack),before[1]['target'],
                                       '-o',str(out/f'neighbor-{label}.glb')],stdout=subprocess.DEVNULL)
            self.assertEqual(marker.t.read_glb(out/'neighbor-before.glb'),
                             marker.t.read_glb(out/'neighbor-after.glb'))
            self.assertEqual(before[0]['vertex_bounds'],control[0]['vertex_bounds'])
            self.assertNotEqual(before[0]['vertex_bounds'],changed[0]['vertex_bounds'])
            self.assertEqual(control[0]['triangles'],changed[0]['triangles'])
            self.assertNotEqual(control[0]['draw_probe_signature'],changed[0]['draw_probe_signature'])
            self.assertTrue(changed[0]['draw_record_offsets'])
            self.assertNotEqual(before[0]['index_first'],changed[0]['index_first'])
            self.assertEqual(report['variants']['marker']['sha256'],marker.digest(out/'marker.rpk'))
            probe=Path(__file__).resolve().parents[3]/'out/tests/mesh-probe-1.exe'
            subprocess.check_call([str(probe),str(out/'marker-probe.txt')])
            with self.assertRaises(ValueError): marker.prepare(source,before[0]['target'],[],exe,out)

    def test_factor_and_exact_target(self):
        with self.assertRaises(ValueError): marker.inset({'positions':[[0,0,0]]},float('nan'))
        with self.assertRaises(ValueError): marker.inset({'positions':[[0,0,0]]},1)
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(ValueError):
                marker.prepare(os.environ['TRACK_FIXTURE'],'*',[],os.environ['DCMOD'],Path(td)/'out')
            self.assertFalse((Path(td)/'out').exists())
