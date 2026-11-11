# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'inspect_resources.py'
spec = importlib.util.spec_from_file_location('inspect_resources', SCRIPT)
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


class ResourceEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.rid = '002f000000000001'
        (self.root/'spawn.res').write_bytes(b'EVOSLITL' + struct.pack('<QI', int(self.rid,16), 7) + b'private payload')
        self.manifest = {'format':'dcmod-rpk', 'info':{'id':'0001000000000001','version':0x40002}, 'root':self.rid,
                         'required':['common'], 'resources':[{'id':self.rid,'type':'spawn_point','content':'raw',
                         'file':'spawn.res','deps':['002b000000000001'], 'names':['PRIVATE_NAME'], 'sources':['D:/private/path']} ]}

    def test_header_and_no_content_export(self):
        report = evidence.summarize_manifest(self.manifest, self.root)
        self.assertEqual(report['resources'][0]['payload_version'], 7)
        self.assertTrue(report['resources'][0]['payload_id_matches'])
        self.assertEqual(report['types']['spawn_point']['count'], 1)
        text = json.dumps(report)
        for private in ('PRIVATE_NAME', 'private payload', 'D:/private/path'):
            self.assertNotIn(private, text)

    def test_invalid_headers_are_reported_not_guessed(self):
        (self.root/'spawn.res').write_bytes(b'too short')
        item = evidence.summarize_manifest(self.manifest,self.root)['resources'][0]
        self.assertIsNone(item['payload_version'])
        self.assertFalse(item['payload_header_valid'])

    def test_reject_escape_duplicate_and_missing_file(self):
        for name in ('../outside.res', '/tmp/outside.res', 'missing.res'):
            manifest = copy.deepcopy(self.manifest)
            manifest['resources'][0]['file'] = name
            with self.assertRaises(evidence.EvidenceError): evidence.summarize_manifest(manifest,self.root)
        self.manifest['resources'].append(self.manifest['resources'][0])
        with self.assertRaises(evidence.EvidenceError): evidence.summarize_manifest(self.manifest,self.root)

    def test_dependency_resolution_does_not_call_external_dependency_corrupt(self):
        first = evidence.summarize_manifest(self.manifest,self.root)
        first['label']='a.rpk'
        report = evidence.assemble([first])
        self.assertEqual(len(report['unresolved_in_supplied_packs']),1)
        second = copy.deepcopy(first)
        second['label']='common.rpk'
        second['resources'][0].update(id='002b000000000001',dependencies=[])
        report = evidence.assemble([first,second])
        self.assertFalse(report['unresolved_in_supplied_packs'])
        self.assertEqual(report['dependency_links'][0]['providers'], ['common.rpk'])

    @unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'real dcmod fixture not configured')
    def test_real_dcmod_metadata_and_no_overwrite(self):
        source = Path(os.environ['TRACK_FIXTURE']).resolve()
        before = source.read_bytes()
        out = self.root/'report.json'
        arguments = [str(source),'--dcmod',os.environ['DCMOD'],'-o',str(out)]
        self.assertEqual(evidence.main(arguments),0)
        report = json.loads(out.read_text())
        self.assertEqual(report['packs'][0]['types']['mesh']['count'],1)
        self.assertTrue(all(r['payload_id_matches'] for r in report['packs'][0]['resources']))
        saved = out.read_bytes()
        self.assertEqual(evidence.main(arguments),1)
        self.assertEqual(saved,out.read_bytes())
        self.assertEqual(before,source.read_bytes())
        self.assertEqual(set(p.name for p in self.root.iterdir()), {'spawn.res','report.json'})


if __name__ == '__main__':
    unittest.main()
