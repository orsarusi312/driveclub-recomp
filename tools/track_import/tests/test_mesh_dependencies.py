# SPDX-License-Identifier: GPL-2.0-or-later
"""Real dcmod processes with synthetic, split RPKs; no game assets."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'set DCMOD and TRACK_FIXTURE')
class MeshDependencyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.exe = str(Path(os.environ['DCMOD']).resolve())
        self.fixture = Path(os.environ['TRACK_FIXTURE']).resolve()
        self.cli('rpk', 'extract', self.fixture, '-o', self.root/'target-src')
        shutil.copytree(self.root/'target-src', self.root/'dep-src')
        manifest = self.root/'target-src/pack.json'
        doc = json.loads(manifest.read_text())
        doc['resources'] = [r for r in doc['resources'] if r['type'] != 'stream_format']
        doc['required'] = ['global.rpk', 'worlds/canada_common.rpk']
        manifest.write_text(json.dumps(doc))
        self.target = self.root/'target.rpk'
        self.dep = self.root/'global.rpk'
        self.cli('rpk', 'build', self.root/'target-src', '-o', self.target)
        self.build_dep()
        self.target_name = '0008000000000001_0_road_slot'

    def cli(self, *args, ok=True):
        result = subprocess.run([self.exe, *map(str, args)], capture_output=True, text=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def build_dep(self):
        self.cli('rpk', 'build', self.root/'dep-src', '-o', self.dep)

    def scan(self, *deps):
        args = [arg for dep in deps for arg in ('--dependency', dep)]
        return json.loads(self.cli('mesh', 'scan', self.target, *args).stdout)

    def test_resolution_and_no_dependency_mesh_enumeration(self):
        before = [p.read_bytes() for p in (self.target, self.dep)]
        missing = self.scan()
        self.assertFalse(missing['candidates'])
        self.assertEqual(missing['slots'][0]['normal_status'], 'attribute_format_resource_missing_from_pack')
        report = self.scan(self.dep)
        self.assertEqual(len(report['slots']), 2)
        self.assertEqual(len(report['candidates']), 2)
        slot = report['slots'][0]
        self.assertTrue(slot['normal_decoded']); self.assertTrue(slot['uv_decoded'])
        selected = next(s for s in slot['vertex_streams'] if s['selected_attributes'])
        self.assertEqual(selected['format_sources'], [str(self.dep)])
        self.assertEqual(report['required_packs'], ['global.rpk', 'worlds/canada_common.rpk'])
        self.assertEqual([p.read_bytes() for p in (self.target, self.dep)], before)

    def test_identical_duplicates_and_conflicts(self):
        duplicate = self.root/'duplicate.rpk'
        shutil.copyfile(self.dep, duplicate)
        self.assertEqual(len(self.scan(self.dep, duplicate)['candidates']), 2)
        fmt = self.root/'dep-src/stream_format/Unnamed.res'
        data = bytearray(fmt.read_bytes()); data[22] = 14; fmt.write_bytes(data)
        self.build_dep()
        for target, deps in [(self.target, [self.dep, duplicate]), (duplicate, [self.dep])]:
            args = [a for d in deps for a in ('--dependency', d)]
            result = self.cli('mesh', 'scan', target, *args, ok=False)
            self.assertIn('conflicting stream_format 0002000000000001', result.stderr)
            self.assertIn(str(self.dep), result.stderr)

    def test_external_tangent_layout(self):
        fmt = self.root/'dep-src/stream_format/Unnamed.res'
        data = bytearray(fmt.read_bytes()); data[20] = 3
        data.extend(bytes([5, 15, 6, 0, 0])); fmt.write_bytes(data)
        self.build_dep()
        manifest = json.loads((self.root/'target-src/pack.json').read_text())
        record = next(r for r in manifest['resources'] if r['id'] == '0003000000000002')
        attr = self.root/'target-src'/record['file']
        old = attr.read_bytes(); data = bytearray(old[:28]); struct.pack_into('<H', data, 22, 16)
        for start in range(28, len(old), 12):
            data.extend(old[start:start+12]); data.extend(struct.pack('<I', 511))
        attr.write_bytes(data)
        self.cli('rpk', 'build', self.root/'target-src', '-o', self.target)
        slot = self.scan(self.dep)['slots'][0]
        self.assertTrue(slot['compatible']); self.assertTrue(slot['tangent_decoded'])

    def test_unknown_field_width_does_not_guess_four_bytes(self):
        fmt = self.root/'dep-src/stream_format/Unnamed.res'
        data = bytearray(fmt.read_bytes()); data[27] = 42; fmt.write_bytes(data)
        self.build_dep()
        manifest = json.loads((self.root/'target-src/pack.json').read_text())
        record = next(r for r in manifest['resources'] if r['id'] == '0003000000000002')
        attr = self.root/'target-src'/record['file']
        data = bytearray(attr.read_bytes()); struct.pack_into('<H', data, 22, 8); attr.write_bytes(data)
        self.cli('rpk', 'build', self.root/'target-src', '-o', self.target)
        slot = self.scan(self.dep)['slots'][0]
        self.assertFalse(slot['normal_decoded'])
        self.assertEqual(slot['normal_status'], 'attribute_format_contains_unsupported_field_type')

    def test_malformed_direct_glb_is_rejected_before_output(self):
        from test_track_import import mutate_glb
        glb = self.root/'input.glb'; output = self.root/'invalid.rpk'
        self.cli('mesh', 'export', self.target, '--dependency', self.dep, '-o', glb)
        original = glb.read_bytes()
        mutations = [lambda r: r.pop('accessors'), lambda r: r.pop('bufferViews'),
                     lambda r: r['meshes'][0].pop('primitives'),
                     lambda r: r['meshes'][0]['primitives'][0].pop('attributes'),
                     lambda r: r['meshes'][0]['primitives'][0]['attributes'].pop('POSITION'),
                     lambda r: r['meshes'][0]['primitives'].append(r['meshes'][0]['primitives'][0]),
                     lambda r: r['accessors'][0].update(count=1.5),
                     lambda r: r['accessors'][0].update(byteOffset=-1),
                     lambda r: r['accessors'][0].update(count=4294967295),
                     lambda r: r['accessors'][0].update(componentType=5122),
                     lambda r: r['accessors'][0].update(normalized=True),
                     lambda r: r['accessors'][0].update(sparse={}),
                     lambda r: r['bufferViews'][0].update(byteLength=4),
                     lambda r: r['bufferViews'][0].update(byteStride=4),
                     lambda r: r['bufferViews'][0].update(buffer=1)]
        for index, mutation in enumerate(mutations):
            with self.subTest(index=index):
                glb.write_bytes(mutate_glb(original, mutation))
                result = self.cli('mesh', 'import', self.target, glb, '--dependency', self.dep, '-o', output, ok=False)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertNotIn('AddressSanitizer', result.stderr)
                self.assertNotIn('runtime error:', result.stderr)
                self.assertFalse(output.exists())

    def test_missing_corrupt_and_malformed_dependency(self):
        missing = self.root/'absent.rpk'
        result = self.cli('mesh', 'scan', self.target, '--dependency', missing, ok=False)
        self.assertIn(str(missing), result.stderr)
        missing.write_bytes(b'not an RPK')
        self.cli('mesh', 'scan', self.target, '--dependency', missing, ok=False)
        fmt = self.root/'dep-src/stream_format/Unnamed.res'
        original = fmt.read_bytes()
        for offset, value, reason in [(20, 99, 'attribute_format_descriptor_truncated'),
                                      (8, 88, 'attribute_format_header_invalid'),
                                      (22, 14, 'normal_semantic_present_but_type_unsupported')]:
            with self.subTest(reason=reason):
                data = bytearray(original); data[offset] = value; fmt.write_bytes(data)
                self.build_dep()
                report = self.scan(self.dep)
                self.assertFalse(report['candidates'])
                self.assertEqual(report['slots'][0]['normal_status'], reason)

    def test_export_and_legacy_roundtrip_without_merging(self):
        glb = self.root/'all.glb'; output = self.root/'roundtrip.rpk'
        self.cli('mesh', 'export', self.target, '--dependency', self.dep, '-o', glb)
        self.cli('mesh', 'import', self.target, glb, '--dependency', self.dep, '-o', output)
        self.assertEqual(output.read_bytes(), self.target.read_bytes())

    def test_output_aliases_and_unresolved_isolated_import(self):
        glb = self.root/'mesh.glb'
        self.cli('mesh', 'export', self.target, '--dependency', self.dep, '-o', glb)
        alias = self.root/'alias.rpk'; os.link(self.dep, alias)
        before = [p.read_bytes() for p in (self.target, self.dep)]
        for output in (self.target, self.dep, alias):
            self.cli('mesh', 'export', self.target, '--dependency', self.dep, '-o', output, ok=False)
            result = self.cli('mesh', 'import', self.target, glb, '--dependency', self.dep, '-o', output, ok=False)
            self.assertIn('output aliases read-only input', result.stderr)
        output = self.root/'unsafe.rpk'
        result = self.cli('mesh', 'import', self.target, glb, '--isolate-buffers', '-o', output, ok=False)
        self.assertIn('decoded normal stream', result.stderr)
        self.assertFalse(output.exists())
        self.assertEqual([p.read_bytes() for p in (self.target, self.dep)], before)

    def test_stage_roundtrip_isolation_and_dependency_validation(self):
        tool = Path(__file__).resolve().parents[1]/'track_import.py'
        bridge = self.root/'bridge.json'
        spec = {'schema': 'dc-visual-bridge-1', 'template': str(self.target),
                'game_path': 'worlds/test.rpk', 'target': self.target_name, 'dependencies': ['global.rpk']}
        bridge.write_text(json.dumps(spec))
        output = self.root/'staged'
        before = [p.read_bytes() for p in (self.target, self.dep)]
        def stage():
            return subprocess.run([sys.executable, str(tool), 'stage', str(tool.parent/'assets/flat_loop/track.json'),
                                   '--bridge', str(bridge), '--dcmod', self.exe, '-o', str(output)], capture_output=True, text=True)
        result = stage(); self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([p.read_bytes() for p in (self.target, self.dep)], before)
        report = json.loads((output/'import-report.json').read_text())
        self.assertEqual(len(report['dependencies'][0]['sha256']), 64)
        staged = output/'files/worlds/test.rpk'
        self.cli('rpk', 'extract', staged, '-o', self.root/'staged-src')
        manifest = json.loads((self.root/'staged-src/pack.json').read_text())
        self.assertFalse(any(r['type'] == 'stream_format' for r in manifest['resources']))
        self.assertEqual(manifest['required'], ['global.rpk', 'worlds/canada_common.rpk'])
        for pack, name in ((self.target, 'before'), (staged, 'after')):
            self.cli('mesh', 'export', pack, '--dependency', self.dep, '-o', self.root/(name+'.glb'),
                     '0008000000000001_1_road_slot')
        self.assertEqual((self.root/'before.glb').read_bytes(), (self.root/'after.glb').read_bytes())
        # Controlled translation in the existing mesh-local frame, no guessed scene transform.
        spec.update(placement='donor_bounds_center', translation=[1,0.25,2])
        bridge.write_text(json.dumps(spec)); output = self.root/'centered'
        result = stage(); self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((output/'import-report.json').read_text())
        placement = report['placement']
        self.assertEqual(placement['applied_translation'], [6.5,0.25,2.5])
        self.assertTrue(placement['extends_donor_vertex_bounds'])
        self.assertEqual(placement['replacement_bounds'], [[-43.5,0.25,-27.5],[56.5,0.25,32.5]])
        self.assertTrue((output/'placement-preview.glb').is_file())
        output = self.root/'invalid-placement'
        spec['placement'] = 'world_transform'; bridge.write_text(json.dumps(spec))
        result = stage(); self.assertNotEqual(result.returncode, 0)
        self.assertIn('placement:', result.stderr); self.assertFalse(output.exists())
        spec['placement'] = 'explicit'
        output = self.root/'rejected'
        for dependencies in ('global.rpk', [None], ['missing.rpk'], ['@global.rpk']):
            spec['dependencies'] = dependencies; bridge.write_text(json.dumps(spec))
            result = stage(); self.assertNotEqual(result.returncode, 0)
            self.assertIn('dependenc', result.stderr)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
