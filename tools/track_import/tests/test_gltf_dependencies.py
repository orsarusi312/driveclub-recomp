# SPDX-License-Identifier: GPL-2.0-or-later
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from gltf_dependencies import inspect
from track_import import Invalid

class DependencyTests(unittest.TestCase):
    def test_local_embedded_missing_and_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); file=root/'scene.gltf'
            spec={'asset':{'version':'2.0'},'buffers':[{'uri':'mesh.bin','byteLength':4}], 'images':[{'uri':'albedo.png'}]}
            file.write_text(json.dumps(spec)); (root/'mesh.bin').write_bytes(b'1234')
            with self.assertRaisesRegex(Invalid,'missing image'): inspect(file)
            (root/'albedo.png').write_bytes(b'private original test bytes')
            self.assertEqual(len(inspect(file)),2)
            for bad in ['../outside.png','https://example.org/file.png','%2e%2e/outside.png','C:/outside.png']:
                spec['images'][0]['uri']=bad; file.write_text(json.dumps(spec))
                with self.assertRaises(Invalid): inspect(file)
            spec['images'][0]['uri']='data:image/png;base64,MTIzNA=='
            file.write_text(json.dumps(spec)); self.assertEqual(inspect(file)[1]['bytes'],4)

    def test_original_glb(self):
        self.assertEqual(inspect(Path(__file__).resolve().parents[1]/'assets/flat_loop/road.glb'),[])

if __name__=='__main__': unittest.main()
