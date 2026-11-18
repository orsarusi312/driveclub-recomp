# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic raw payloads only. No original game bytes or real slot offsets."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from test_resource_trace import trace


class ConsumedPairTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name); self.source = self.root/'source.rpk'
        self.data = bytearray(); self.rows = []
        def resource(rid, payload):
            span = dict(id=rid, offset=len(self.data), size=20+len(payload))
            self.data += b'EVOSLITL'+struct.pack('<QI', int(rid,16),1)+payload
            return span
        self.pos = resource(trace.CONSUMED_POSITION, struct.pack('<HHI',0,12,92)+b''.join(
            struct.pack('<3eHI',v/8,0,1,0xFFFF,0xFFFFFFFF) for v in range(92)))
        self.ix = resource('00042bcd5555ffa6', struct.pack('<HHI',0,2,282)+b''.join(
            struct.pack('<H',i%28) for i in range(96))+b''.join(struct.pack('<H',28+i%64) for i in range(186)))
        for (target,(nv,count)),lo,start in zip(trace.CONSUMED_PAIR.items(),(0,28),(0,96)):
            rec = b'\x02\x03'+struct.pack('<4I',lo,lo+nv-1,start,count)
            draw = struct.pack('<5I',3,lo,0,start,count)
            mesh = resource(target.split('_')[0],rec+draw)
            xyz = b''.join(self.data[self.pos['offset']+28+v*12:self.pos['offset']+34+v*12] for v in range(lo,lo+8))
            ix = self.data[self.ix['offset']+28+start*2:self.ix['offset']+28+start*2+48]
            row = dict(target=target, mesh_resource=mesh['id'], position_buffer=trace.CONSUMED_POSITION,
                normal_buffer=trace.CONSUMED_POSITION, attribute_buffer='000380084a65ecc7',
                uv_buffer='000380084a65ecc7', index_buffer=self.ix['id'],
                position_stride=12,position_resource_vertices=92,vertices=nv,triangles=count//3,
                vertex_first=lo,vertex_last=lo+nv-1,index_first=start,index_count=count,
                normal_decoded=True,uv_decoded=True,compatible=True,draw_table_found=True,
                position_half_float=True,skinned=False,partial_layout=False,
                mesh_record_offsets=[20],draw_record_offsets=[38],
                source_file_spans=[mesh,self.pos,self.ix],
                draw_probe_signature=f'v1:12:6:8:{count}:{lo}:{xyz.hex()}:{ix.hex()}')
            self.rows.append(row)
        self.source.write_bytes(self.data)
        self.full = self.root/'full.json'; self.output=self.root/'result'
        self.report = dict(schema='dc-mesh-compatibility-1',scanned_slots=2,reported_slots=2,slots=self.rows)
        self.save()

    def save(self): self.full.write_text(json.dumps(self.report))
    def validate(self,row=None): return trace.validate_consumed_row(row or self.rows[0],self.source,'worlds/test.rpk')
    def inspect(self):
        # Orchestration mock only; raw-source validation below reads actual fixture bytes.
        def scan(dcmod,source,deps,target):
            return {'slots':[copy.deepcopy(next(r for r in self.rows if r['target']==target))]}
        with patch.object(trace,'scan_target',side_effect=scan) as scanner:
            result=trace.inspect_consumed_pair('dcmod',self.source,[],self.full,'worlds/test.rpk',self.output)
            self.assertEqual(scanner.call_count,2)
        return result

    def test_pair_ranges_raw_bytes_and_no_selection_or_asset_copy(self):
        before=self.source.read_bytes(); result=self.inspect()
        self.assertTrue(result['signatures_distinct_in_report']); self.assertIsNone(result['selected_target'])
        self.assertFalse(result['launch_prepared']); self.assertFalse(result['runtime_verified'])
        self.assertEqual(result['candidates'][0]['index_range_half_open'],[0,96])
        self.assertEqual(result['candidates'][1]['vertex_range_inclusive'],[28,91])
        self.assertEqual(result['candidates'][1]['index_byte_range_in_payload'],[220,592])
        self.assertEqual(self.source.read_bytes(),before)
        self.assertEqual([p.name for p in self.output.iterdir()],['donor-pair.json'])
        with self.assertRaises(FileExistsError): self.inspect()

    def test_prefix_flag_false_does_not_reject_complete_half_layout(self):
        row=copy.deepcopy(self.rows[0]); row['vertex_streams']=[dict(id=trace.CONSUMED_POSITION,known_prefix_decodable=False,layout_decode_status='complete')]
        self.assertTrue(self.validate(row)['source_position_and_index_bytes_verified'])

    def test_corrupt_position_indices_headers_and_records(self):
        for at in (self.pos['offset']+22,self.pos['offset']+28,self.ix['offset']+22,
                   self.ix['offset']+28,self.rows[0]['source_file_spans'][0]['offset']+20,
                   self.rows[0]['source_file_spans'][0]['offset']+38):
            with self.subTest(at=at):
                data=bytearray(self.data); data[at]^=1; self.source.write_bytes(data)
                with self.assertRaises(ValueError): self.validate()
        self.source.write_bytes(self.data)
        # NaN outside the sampled prefix is still rejected in this tiny source buffer.
        data=bytearray(self.data); struct.pack_into('<H',data,self.pos['offset']+28+91*12,0x7e00)
        self.source.write_bytes(data)
        with self.assertRaisesRegex(ValueError,'nonfinite'): self.validate()

    def test_malformed_ranges_and_metadata(self):
        for key,value in [('vertex_last',92),('index_first',-1),('index_count',True),
                          ('draw_record_offsets',[99999]),('draw_record_offsets',[]),
                          ('mesh_record_offsets',[20,20]),('position_half_float',False),('partial_layout',True)]:
            with self.subTest(key=key):
                row=copy.deepcopy(self.rows[0]); row[key]=value
                with self.assertRaises(ValueError): self.validate(row)

    def test_alias_comparison_accounts_for_rebasing_and_prefix_length(self):
        a=self.rows[0]; b=copy.deepcopy(a); f=b['draw_probe_signature'].split(':')
        indices=trace.probe_signature(a)[6]
        f[5]='5'; f[7]=struct.pack('<24H',*(i+5 for i in indices)).hex()
        b['draw_probe_signature']=':'.join(f)
        self.assertTrue(trace.possible_signature_alias(a,b))
        f[3]='3'; f[6]=f[6][:36]; b['draw_probe_signature']=':'.join(f)
        self.assertTrue(trace.possible_signature_alias(a,b))
        self.assertFalse(trace.possible_signature_alias(a,self.rows[1]))
        f[7]='ffff'+f[7][4:]; b['draw_probe_signature']=':'.join(f)
        self.assertFalse(trace.possible_signature_alias(a,b))

    def test_alias_reported_not_silently_selected(self):
        alias=copy.deepcopy(self.rows[0]); alias['target']='another_slot'; alias['position_buffer']='000300000000ffff'
        self.report['slots']=self.rows+[alias]; self.report['scanned_slots']=self.report['reported_slots']=3; self.save()
        result=self.inspect(); self.assertFalse(result['signatures_distinct_in_report'])
        self.assertEqual(result['candidates'][0]['possible_signature_aliases_in_report'],['another_slot'])

    def test_incomplete_stale_or_unexpected_slot_report_refused(self):
        for change in ('incomplete','unexpected','stale'):
            with self.subTest(change=change):
                report=copy.deepcopy(self.report)
                if change=='incomplete': report['scanned_slots']=3
                if change=='unexpected': report['slots'][0]['target']='unexpected'
                if change=='stale': report['slots'][0]['index_first']=2
                self.full.write_text(json.dumps(report))
                with self.assertRaises(ValueError): self.inspect()
                self.assertFalse(self.output.exists())

    def test_input_change_refuses_publication(self):
        calls=0; original=trace.digest
        def changed(path):
            nonlocal calls
            calls+=1
            return 'changed' if calls>2 else original(path)
        with patch.object(trace,'digest',side_effect=changed):
            with self.assertRaisesRegex(ValueError,'input changed'): self.inspect()
        self.assertFalse(self.output.exists())

    @unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'requires synthetic dcmod fixture')
    def test_real_dcmod_pair_inspection(self):
        exe=str(Path(os.environ['DCMOD']).resolve()); folder=self.root/'pack'
        def cli(*args):
            return subprocess.check_output([exe,*map(str,args)],text=True)
        cli('rpk','extract',Path(os.environ['TRACK_FIXTURE']).resolve(),'-o',folder)
        doc=json.loads((folder/'pack.json').read_text()); doc['resources']=[]
        posfmt='0002874b47ba4c22'; uvfmt='00020000000000f0'; uv='000380084a65ecc7'
        def header(rid): return b'EVOSLITL'+struct.pack('<QI',int(rid,16),1)
        def add(rid,typ,data,deps=(),name=''):
            filename=f'pair-{rid}.res'; (folder/filename).write_bytes(data)
            doc['resources'].append(dict(id=rid,type=typ,order=len(doc['resources']),
                names=[name] if name else [],sources=[],deps=list(deps),content='raw',file=filename))
        add(posfmt,'stream_format',header(posfmt)+bytes([2,1,6,0,0,0,2,15,0,0,0]))
        add(uvfmt,'stream_format',header(uvfmt)+bytes([1,5,8,0,0,0]))
        data=bytearray(self.data[self.pos['offset']:self.pos['offset']+self.pos['size']])
        for v in range(92): struct.pack_into('<I',data,28+v*12+8,511<<10)
        add(trace.CONSUMED_POSITION,'vertex_buffer',data,[posfmt])
        add(uv,'vertex_buffer',header(uv)+struct.pack('<HHI',0,8,92)+struct.pack('<ff',0,0)*92,[uvfmt])
        add(self.ix['id'],'index_buffer',self.data[self.ix['offset']:self.ix['offset']+self.ix['size']])
        deps=[trace.CONSUMED_POSITION,uv,self.ix['id']]
        for row in self.rows:
            span=row['source_file_spans'][0]; rid=row['mesh_resource']
            payload=header(rid)+b''.join(struct.pack('<Q',int(d,16)) for d in deps)
            payload+=self.data[span['offset']+20:span['offset']+span['size']]
            add(rid,'mesh',payload,deps,row['target'].split('_',2)[2])
        doc['root']=self.rows[0]['mesh_resource']; (folder/'pack.json').write_text(json.dumps(doc))
        pack=self.root/'pair.rpk'; cli('rpk','build',folder,'-o',pack)
        self.full.write_text(cli('mesh','scan',pack)); before=pack.read_bytes()
        result=trace.inspect_consumed_pair(exe,pack,[],self.full,'worlds/test.rpk',self.output)
        self.assertTrue(result['signatures_distinct_in_report'])
        self.assertFalse(result['vertex_span_ranges_overlap']); self.assertFalse(result['index_ranges_overlap'])
        self.assertEqual(result['candidates'][0]['position_layout'][0]['layout_decode_status'],'complete')
        self.assertEqual(pack.read_bytes(),before)

if __name__=='__main__': unittest.main()
