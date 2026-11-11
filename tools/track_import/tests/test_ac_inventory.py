# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'ac_inventory.py'
spec = importlib.util.spec_from_file_location('ac_inventory', TOOL)
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


class InventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.track = self.base/'private'
        self.track.mkdir()
        self.model = self.track/'road.kn5'
        self.model.write_bytes(b'original synthetic fixture; NOT a KN5 model')
        self.ini = self.track/'models.ini'
        self.ini.write_text('[MODEL_0]\nFILE=road.kn5\nPOSITION=1,2,3\nROTATION=0,90,0\n')

    def test_complete_inventory_records_raw_data_without_claiming_decode(self):
        before = self.model.read_bytes()
        report = a.write_inventory(self.track, self.base/'out', licence_note='Private permission record')
        self.assertEqual(report['status'], 'inventory_ready')
        self.assertEqual(report['layouts'][0]['models'][0]['placement_raw']['POSITION'], '1,2,3')
        self.assertFalse(report['kn5_files'][0]['format_validated'])
        self.assertEqual(self.model.read_bytes(), before)
        manifest = json.loads((self.base/'out/blender-preparation.json').read_text())
        self.assertEqual(manifest['mesh_roles']['road_collision'], [])
        self.assertFalse(manifest['spawn']['surface_verified'])
        self.assertIsNone(manifest['spawn']['position_metres'])
        self.assertEqual(manifest['licence']['note'], 'Private permission record')
        self.assertEqual(set(p.name for p in (self.base/'out').iterdir()), {'inventory.json', 'blender-preparation.json'})

    def test_missing_reference_and_multiple_layouts_require_review(self):
        (self.track/'models_tc1000.ini').write_text('[MODEL_0]\nFILE=missing.kn5\n')
        report, _ = a.inventory(self.track)
        self.assertIsNone(report['selected_layout'])
        self.assertEqual(report['status'], 'needs_layout_or_dependencies')
        report, _ = a.inventory(self.track, 'models_tc1000.ini')
        self.assertEqual(report['layouts'][1]['models'][0]['status'], 'missing')
        report, _ = a.inventory(self.track, 'models.ini')
        self.assertEqual(report['status'], 'inventory_ready')

    def test_reject_escape_and_absolute_references(self):
        for reference in ['../secret.kn5', '..\\secret.kn5', '/secret.kn5', 'C:\\secret.kn5', '\\\\server\\secret.kn5']:
            with self.subTest(reference=reference):
                self.ini.write_text('[MODEL_0]\nFILE='+reference+'\n')
                with self.assertRaises(a.InventoryError): a.inventory(self.track)

    def test_case_resolution_and_ambiguity(self):
        self.ini.write_text('[MODEL_0]\nFILE=ROAD.KN5\n')
        report, _ = a.inventory(self.track)
        self.assertTrue(report['layouts'][0]['models'][0]['case_mismatch'])
        upper = self.track/'ROAD.KN5'
        if upper.exists():
            self.skipTest('case-insensitive filesystem cannot represent ambiguous fixture')
        upper.write_bytes(b'another fixture')
        report, _ = a.inventory(self.track)
        self.assertEqual(report['layouts'][0]['models'][0]['status'], 'ambiguous_case')

    def test_no_source_writes_or_output_overwrite(self):
        with self.assertRaises(a.InventoryError): a.write_inventory(self.track, self.track/'out')
        a.write_inventory(self.track, self.base/'out')
        with self.assertRaises(a.InventoryError): a.write_inventory(self.track, self.base/'out')
        self.assertFalse((self.track/'out').exists())

    def test_symlinks_are_not_consumed(self):
        private = self.base/'outside.kn5'
        private.write_bytes(b'outside private file')
        self.model.unlink()
        try:
            self.model.symlink_to(private)
        except OSError:
            self.skipTest('symlink creation unavailable')
        report, _ = a.inventory(self.track)
        self.assertEqual(report['skipped_symlinks'], ['road.kn5'])
        self.assertEqual(report['kn5_files'], [])
        self.assertEqual(report['layouts'][0]['models'][0]['status'], 'missing')
        output_link = self.base/'linked-output'
        output_link.symlink_to(self.base/'not-created', target_is_directory=True)
        with self.assertRaises(a.InventoryError):
            a.write_inventory(self.track, output_link)
        self.assertFalse((self.base/'not-created').exists())

    def test_missing_file_field_and_duplicate_ini(self):
        self.ini.write_text('[DEFAULT]\nFILE=road.kn5\n[MODEL_0]\nPOSITION=0,0,0\n')
        report, _ = a.inventory(self.track)
        self.assertEqual(report['layouts'][0]['models'][0]['status'], 'missing_file_field')
        self.ini.write_text('[MODEL_0]\nFILE=road.kn5\nFILE=other.kn5\n')
        with self.assertRaises(a.InventoryError): a.inventory(self.track)

    def test_cli_incomplete_exit_two_and_invalid_exit_one(self):
        self.model.unlink()
        result = subprocess.run([sys.executable, str(TOOL), str(self.track), '-o', str(self.base/'out')], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertTrue((self.base/'out/inventory.json').is_file())
        result = subprocess.run([sys.executable, str(TOOL), str(self.track), '-o', str(self.base/'out')], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'ac-inventory:', result.stderr)


if __name__ == '__main__':
    unittest.main()
