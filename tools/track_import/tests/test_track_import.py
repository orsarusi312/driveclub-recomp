# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'track_import.py'
spec = importlib.util.spec_from_file_location('track_import', TOOL)
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)


def mutate_glb(data, change):
    length = struct.unpack_from('<I', data, 12)[0]
    root = json.loads(data[20:20+length])
    change(root)
    js = json.dumps(root).encode()
    js += b' ' * (-len(js) % 4)
    tail = data[20+length:]
    return struct.pack('<III', 0x46546C67, 2, 20+len(js)+len(tail)) + struct.pack('<II', len(js), 0x4E4F534A) + js + tail


class TrackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        t.generate(self.root/'track')
        self.config = self.root/'track/track.json'
        self.mesh = self.root/'track/road.glb'
        self.road = t.read_glb(self.mesh)

    def test_original_loop_support_and_hole(self):
        report = t.load_track(self.config)[2]
        self.assertEqual((report['vertices'], report['triangles']), (8, 8))
        self.assertEqual(t.surface_height(self.road, 0, -25), 0)
        self.assertEqual(t.surface_height(self.road, -45, 0), 0)
        self.assertIsNone(t.surface_height(self.road, 0, 0))
        self.assertIsNone(t.surface_height(self.road, 100, 100))
        road = copy.deepcopy(self.road)
        road['indices'].reverse()
        self.assertIsNone(t.surface_height(road, 0, -25))

    def test_slope_and_highest_surface(self):
        road = {'positions': [[0,0,0],[0,0,10],[10,10,0], [0,20,0],[0,20,10],[10,20,0]], 'indices': [0,1,2,3,4,5]}
        self.assertAlmostEqual(t.surface_height(road, 2, 2), 20)
        road['indices'] = [0,1,2]
        self.assertAlmostEqual(t.surface_height(road, 2, 2), 2)

    def test_reject_bad_spawn_and_path(self):
        original = json.loads(self.config.read_text())
        for field, value in [('spawn', {'position':[0,1,0], 'heading_degrees':0}),
                             ('spawn', {'position':[0,-1,-25], 'heading_degrees':0}),
                             ('spawn', {'position':[0,1,-25], 'heading_degrees':float('nan')}),
                             ('mesh', '../escape.glb'), ('mesh', '/tmp/road.glb'), ('units', 'feet')]:
            with self.subTest(field=field, value=value):
                altered = dict(original, **{field: value})
                self.config.write_text(json.dumps(altered))
                with self.assertRaises(t.Invalid): t.load_track(self.config)

    def test_reject_unsupported_and_malformed_glb(self):
        original = self.mesh.read_bytes()
        mutations = [lambda r: r['nodes'][0].update(translation=[1,0,0]),
                     lambda r: r['nodes'][0].update(scale=[-1,1,1]),
                     lambda r: r.update(extensionsUsed=['KHR_draco_mesh_compression']),
                     lambda r: r['accessors'][0].update(sparse={}),
                     lambda r: r['accessors'][0].update(count=100000),
                     lambda r: r['bufferViews'][0].update(byteLength=4),
                     lambda r: r['buffers'][0].update(uri='road.bin'),
                     lambda r: r['meshes'][0]['primitives'][0].update(mode=5),
                     lambda r: r['meshes'][0]['primitives'].append(r['meshes'][0]['primitives'][0]),
                     lambda r: r['accessors'][0].update(componentType=5123)]
        for change in mutations:
            self.mesh.write_bytes(mutate_glb(original, change))
            with self.assertRaises(t.Invalid): t.read_glb(self.mesh)
        for data in [original[:10], original[:-1], original+b'xxxx']:
            self.mesh.write_bytes(data)
            with self.assertRaises(t.Invalid): t.read_glb(self.mesh)

    def test_reject_bad_triangles_and_nan(self):
        for kind in ['nan', 'degenerate', 'index']:
            road = copy.deepcopy(self.road)
            if kind == 'nan': road['positions'][0][0] = float('nan')
            if kind == 'degenerate': road['indices'][1] = road['indices'][0]
            if kind == 'index': road['indices'][0] = 500
            self.mesh.write_bytes(t.make_glb(road))
            with self.assertRaises(t.Invalid): t.read_glb(self.mesh)

    def test_no_overwrite_and_cli_failure(self):
        with self.assertRaises(t.Invalid): t.generate(self.root/'track')
        result = subprocess.run([os.sys.executable, str(TOOL), 'check', str(self.root/'missing.json')], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'track-import:', result.stderr)

    @unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'set DCMOD and TRACK_FIXTURE for real dcmod integration')
    def test_real_dcmod_stage_and_shared_buffer_isolation(self):
        exe = Path(os.environ['DCMOD']).resolve()
        template = Path(os.environ['TRACK_FIXTURE']).resolve()
        before = template.read_bytes()
        legacy_glb, legacy_rpk = self.root/'legacy.glb', self.root/'legacy.rpk'
        t.run_dcmod(exe, 'mesh', 'export', template, '-o', legacy_glb)
        t.run_dcmod(exe, 'mesh', 'import', template, legacy_glb, '-o', legacy_rpk)
        self.assertEqual(legacy_rpk.read_bytes(), before)
        bridge = self.root/'bridge.json'
        bridge.write_text(json.dumps({'schema':'dc-visual-bridge-1', 'template':str(template),
            'game_path':'worlds/test.rpk', 'target':'0008000000000001_0_road_slot', 'translation':[1,2,3]}))
        # 8 incoming vertices < 12 original: isolation must still allocate new buffers.
        report = t.stage(self.config, bridge, exe, self.root/'mod')
        self.assertIn('NOT a playable new track', report['status'])
        self.assertEqual(template.read_bytes(), before)
        output = self.root/'mod/files/worlds/test.rpk'
        untouched = self.root/'untouched.glb'
        original = self.root/'original.glb'
        t.run_dcmod(exe, 'mesh', 'export', output, '-o', untouched, '0008000000000001_1_road_slot')
        t.run_dcmod(exe, 'mesh', 'export', template, '-o', original, '0008000000000001_1_road_slot')
        # Fixture's untouched geometry need not be a road, so compare complete exported bytes.
        self.assertEqual(untouched.read_bytes(), original.read_bytes())
        for count in (12, 16):
            road = copy.deepcopy(self.road)
            for field in ('positions', 'normals', 'uvs'):
                road[field] += road[field][:count-8]
            self.mesh.write_bytes(t.make_glb(road))
            folder = self.root / f'mod-{count}'
            t.stage(self.config, bridge, exe, folder)
            t.run_dcmod(exe, 'mesh', 'export', folder/'files/worlds/test.rpk', '-o', untouched, '0008000000000001_1_road_slot')
            self.assertEqual(untouched.read_bytes(), original.read_bytes())
        self.mesh.write_bytes(t.make_glb(self.road))
        with self.assertRaises(t.Invalid): t.stage(self.config, bridge, exe, self.root/'mod')
        broken = json.loads(bridge.read_text()); broken['target']='0008000000000001_9_missing'
        bridge.write_text(json.dumps(broken))
        with self.assertRaises(t.Invalid): t.stage(self.config, bridge, exe, self.root/'failed')
        self.assertFalse((self.root/'failed').exists())


if __name__ == '__main__':
    unittest.main()
