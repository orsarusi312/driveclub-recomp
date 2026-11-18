# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from test_resource_trace import trace
import test_consumed_pair
spec=importlib.util.spec_from_file_location('pair_summary',Path(__file__).resolve().parents[1]/'summarize_mesh_probe_pair.py')
summary=importlib.util.module_from_spec(spec); spec.loader.exec_module(summary)

class PairPreparationTests(unittest.TestCase):
    def setUp(self):
        self.fixture=test_consumed_pair.ConsumedPairTests(); self.fixture.setUp(); self.addCleanup(self.fixture.doCleanups)
        f=self.fixture; f.full=f.root/'mesh-scan.json'; f.save(); f.inspect()
        self.pair=f.output/'donor-pair.json'; self.output=f.root/'run'
    def prepare(self):
        return trace.prepare_consumed_probe(self.pair,self.fixture.source,'worlds/test.rpk',self.output)
    def test_stock_copy_and_both_signatures_without_rescan(self):
        before=self.fixture.source.read_bytes(); report=self.prepare()
        self.assertEqual((self.output/'mods/worlds/test.rpk').read_bytes(),before)
        for row in self.fixture.rows:
            self.assertEqual((self.output/f"probe-{row['index_count']}.txt").read_text().strip(),row['draw_probe_signature'])
        self.assertEqual(len(report['candidates']),2); self.assertEqual(self.fixture.source.read_bytes(),before)
        with self.assertRaises(FileExistsError): self.prepare()
    def test_changed_input_and_pair_claim_rejected(self):
        original=self.pair.read_text(); data=json.loads(original)
        data['candidates'][0]['draw_probe_signature']='tampered'; self.pair.write_text(json.dumps(data))
        with self.assertRaises(ValueError): self.prepare()
        self.assertFalse(self.output.exists()); self.pair.write_text(original)
        self.fixture.source.write_bytes(b'changed')
        with self.assertRaises(ValueError): self.prepare()
        self.assertFalse(self.output.exists())
    def test_failed_alias_or_path_gate(self):
        for field,value in [('signatures_distinct_in_report',False),('vertex_span_ranges_overlap',True),('runtime_verified',True)]:
            with self.subTest(field=field):
                original=self.pair.read_text(); data=json.loads(original); data[field]=value; self.pair.write_text(json.dumps(data))
                with self.assertRaises(ValueError): self.prepare()
                self.pair.write_text(original)
        with self.assertRaises(ValueError): trace.prepare_consumed_probe(self.pair,self.fixture.source,'../bad.rpk',self.output)
        self.assertFalse(self.output.exists())

class PairSummaryTests(unittest.TestCase):
    def log(self,reason='deadline',a=0,b=0,match=False):
        lines=['MeshProbe: pair configured counts=96,186;', 'MeshProbe: lane=0 offline armed;']
        if match: lines+=['MeshProbe: lane=0 offline-exact draw-recorded #1 delta=0 count=96 instances=1',
                          'MeshProbe: lane=0 offline paired-binding serial=7 count=96 vb=0x1000']
        for count,n in [(96,a),(186,b)]:
            hit=int(match and count==96); outcome='matched' if hit else 'sampled-unmatched' if n else 'no-coverage'
            lines.append(f'MeshProbe: pair-result lane=0 count={count} reason={reason} outcome={outcome} attempts={n} index-readable={n} index-matches={hit} paired-matches={hit};')
        return lines
    def test_terminal_classifications(self):
        for lines,expected in [(self.log(),'NO_COVERAGE'),(self.log(a=2),'SAMPLED_UNMATCHED'),
                (self.log('budget',2048,2048),'BUDGET_EXHAUSTED'),(self.log('matched',1,0,True),'MATCHED')]:
            self.assertEqual(summary.summarize(lines)['status'],expected)
    def test_incomplete_and_inconsistent_evidence(self):
        valid=self.log('matched',1,0,True)
        for lines in [[],valid[:-1],valid[:2]+valid[3:],valid+valid[-1:],self.log('budget',2,2),
                      valid+['MeshProbe: lane=1 offline armed;'],['MeshProbe: pair-result invalid']]:
            self.assertEqual(summary.summarize(lines)['status'],'INCOMPLETE')
