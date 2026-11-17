# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import inspect_native_caller as native
from native_control_flow import walk


@unittest.skipUnless(importlib.util.find_spec('capstone'), 'optional offline decoder needs capstone==5.0.6')
class BlockWalkTests(unittest.TestCase):
    def run_code(self, data, **kwargs):
        return walk(bytes.fromhex(data), 0x1000, native.disassembler(), **kwargs)

    def test_flag_branch_and_no_decode_after_return(self):
        r=self.run_code('807f0c00 7402 0fc8 c3 0f')
        self.assertEqual([i['mnemonic'] for i in r['instructions']],['cmp','je','bswap','ret'])
        self.assertEqual(r['decoded_bytes'],9)
        self.assertEqual(r['stops'],[])
        self.assertEqual(r['edges'][0]['to'],'0x1008')

    def test_jump_over_invalid_bytes_and_loop(self):
        r=self.run_code('eb02 0f0f c3')
        self.assertEqual([i['address'] for i in r['instructions']],['0x1000','0x1004'])
        r=self.run_code('ebfe')
        self.assertEqual(len(r['instructions']),1)
        self.assertEqual(r['edges'][0]['to'],'0x1000')

    def test_unresolved_edges_calls_and_limits(self):
        r=self.run_code('e800010000 c3')
        self.assertEqual(r['calls'][0]['target'],'0x1105')
        self.assertEqual(len(r['instructions']),2) # callee never followed
        for code in ['ffe0','e900010000']:
            r=self.run_code(code)
            self.assertEqual(r['edges'][0]['kind'],'external-or-indirect-branch')
        self.assertEqual(self.run_code('90')['stops'][0]['reason'],'window-end-fallthrough')
        self.assertEqual(self.run_code('0f')['stops'][0]['reason'],'undecodable-or-truncated')
        self.assertEqual(self.run_code('cc')['stops'][0]['reason'],'trap-or-halt')
        self.assertEqual(self.run_code('9090c3',instruction_limit=1)['stops'][0]['reason'],'instruction-budget')
        self.assertEqual(self.run_code('7401c3c3',block_limit=1)['stops'][0]['reason'],'block-budget')
        with self.assertRaises(ValueError): self.run_code('c3',instruction_limit=257)

    def test_misaligned_branch_is_reported(self):
        r=self.run_code('74ff c3') # target inside the conditional jump itself
        self.assertEqual(r['stops'][0]['reason'],'branch-into-instruction')
        self.assertEqual(len(r['instructions']),2)

    def test_loop_family_and_interrupt_return(self):
        for opcode in ('e0', 'e1', 'e2', 'e3'):
            r=self.run_code(opcode+'02 c3 90 c3')
            self.assertEqual([i['address'] for i in r['instructions']],
                             ['0x1000','0x1002','0x1004'])
            self.assertEqual(r['edges'][0]['to'],'0x1004')
        r=self.run_code('cf 90')
        self.assertEqual(len(r['instructions']),1)
        self.assertEqual(r['edges'][0]['kind'],'return')

    def test_reviewed_call_boundary_does_not_infer_return(self):
        r=self.run_code('e801000000 c3 c3',stop_at_call_targets=(0x1006,))
        self.assertEqual(len(r['instructions']),1)
        self.assertEqual(r['stops'][0]['reason'],'reviewed-call-boundary-return-unproven')
        self.assertEqual(r['calls'][0]['target'],'0x1006')
        self.assertEqual(len(self.run_code('e801000000 c3 c3')['instructions']),2)
        for values in [tuple(range(9)),(-1,),(2**64,),('0x1006',)]:
            with self.assertRaises(ValueError): self.run_code('c3',stop_at_call_targets=values)


if __name__ == '__main__':
    unittest.main()
