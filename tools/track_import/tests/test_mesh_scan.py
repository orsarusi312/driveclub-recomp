# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

@unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'set DCMOD and TRACK_FIXTURE')
class MeshScanTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        self.exe=str(Path(os.environ['DCMOD']).resolve())
        self.fixture=Path(os.environ['TRACK_FIXTURE']).resolve()
        self.runcli('rpk','extract',self.fixture,'-o',self.root/'unpacked')
        self.manifest=self.root/'unpacked/pack.json'
        self.doc=json.loads(self.manifest.read_text())
        self.fmt=self.root/'unpacked/stream_format/Unnamed.res'
        self.original=self.fmt.read_bytes()
    def runcli(self,*args):
        return subprocess.check_output([self.exe,*map(str,args)],text=True)
    def scan(self):
        self.runcli('rpk','build',self.root/'unpacked','-o',self.root/'input.rpk')
        before=(self.root/'input.rpk').read_bytes()
        result=json.loads(self.runcli('mesh','scan',self.root/'input.rpk'))
        self.assertEqual((self.root/'input.rpk').read_bytes(),before)
        return result
    def test_candidates_and_shared_buffers(self):
        r=self.scan(); self.assertEqual(len(r['candidates']),2)
        a,b=r['slots']; self.assertTrue(a['compatible']); self.assertEqual(a['vertices'],12)
        self.assertEqual(a['triangles'],1); self.assertTrue(a['normal_decoded']); self.assertFalse(a['tangent_decoded'])
        self.assertEqual(a['shared_buffer_slots'],[b['target']]); self.assertTrue(a['independent_buffer_mentions'])
        self.assertEqual(a['shared_index_buffer_slots'],[b['target']])
        self.assertEqual(a['vertex_bounds'], [[0,0,0], [11,0,1]])
        self.assertEqual(a['vertex_first'],0)
        self.assertEqual(a['index_count'],3)
        self.assertTrue(a['draw_record_offsets'])
        self.assertTrue(a['mesh_record_offsets'])
        self.assertTrue(a['draw_probe_signature'].startswith('v1:12:12:8:3:0:'))
        data=(self.root/'input.rpk').read_bytes()
        spans=a['source_file_spans']
        self.assertEqual([s['id'] for s in spans],[a['mesh_resource'],a['position_buffer'],a['index_buffer']])
        for span in spans:
            offset,size=span['offset'],span['size']
            self.assertGreaterEqual(size,20)
            self.assertLessEqual(offset+size,len(data))
            self.assertEqual(data[offset:offset+8],b'EVOSLITL')
            self.assertEqual(struct.unpack_from('<Q',data,offset+8)[0],int(span['id'],16))

    def test_position_stride_rejected_before_prefix_read(self):
        record=next(r for r in self.doc['resources'] if r['id']=='0003000000000001')
        path=self.root/'unpacked'/record['file']
        data=bytearray(path.read_bytes()); struct.pack_into('<H',data,22,4); path.write_bytes(data)
        self.runcli('rpk','build',self.root/'unpacked','-o',self.root/'bad.rpk')
        r=subprocess.run([self.exe,'mesh','scan',str(self.root/'bad.rpk')],capture_output=True,text=True)
        self.assertEqual(r.returncode,1)
        self.assertIn('position stride is smaller than XYZ encoding',r.stderr)

    def test_consumer_ids_are_metadata_not_lod_inference(self):
        resource_id=0x0014000000000011
        path=self.root/'unpacked'/'scene_object'/'consumer.res'; path.parent.mkdir(exist_ok=True)
        path.write_bytes(b'EVOSLITL'+struct.pack('<QI',resource_id,1))
        self.doc['resources'].append({'id':f'{resource_id:016x}','type':'scene_object',
            'order':len(self.doc['resources']),'names':['synthetic_consumer'],'sources':[],
            'deps':['0008000000000001'],'content':'raw','file':'scene_object/consumer.res'})
        self.manifest.write_text(json.dumps(self.doc))
        row=self.scan()['slots'][0]
        self.assertEqual(row['local_resource_consumers'],[{'id':f'{resource_id:016x}',
            'type':'scene_object','name':'synthetic_consumer'}])

    def test_nonfinite_and_unexportable_donor_positions(self):
        record=next(r for r in self.doc['resources'] if r['id']=='0003000000000001')
        path=self.root/'unpacked'/record['file']; original=path.read_bytes()
        for value,reason in [(float('nan'),'nonfinite_donor_positions'),
                             (float('inf'),'nonfinite_donor_positions'),
                             (1000000.0,'donor_positions_exceed_legacy_export_range')]:
            with self.subTest(value=value):
                data=bytearray(original); struct.pack_into('<f',data,28,value); path.write_bytes(data)
                report=self.scan(); self.assertFalse(report['candidates'])
                self.assertIn(reason,report['slots'][0]['blockers'])
                if reason=='nonfinite_donor_positions': self.assertIsNone(report['slots'][0]['vertex_bounds'])
    def test_normal_evidence(self):
        cases=[(22,14,'normal_semantic_present_but_type_unsupported'),
               (21,1,'no_normal_semantic_in_selected_descriptor'),
               (27,9,'normal_present_but_decoded_layout_does_not_match_stride'),
               (20,99,'attribute_format_descriptor_truncated')]
        for at,value,reason in cases:
            with self.subTest(reason=reason):
                data=bytearray(self.original); data[at]=value; self.fmt.write_bytes(data)
                r=self.scan(); self.assertFalse(r['candidates']); self.assertEqual(r['slots'][0]['normal_status'],reason)
        self.fmt.write_bytes(self.original)
        self.doc['resources']=[r for r in self.doc['resources'] if r['type']!='stream_format']
        self.manifest.write_text(json.dumps(self.doc))
        r=self.scan(); self.assertEqual(r['slots'][0]['normal_status'],'attribute_format_resource_missing_from_pack')
    def test_stage_reports_reason_without_publishing(self):
        tool=Path(__file__).resolve().parents[1]/'track_import.py'
        data=bytearray(self.original); data[22]=14; self.fmt.write_bytes(data)
        report=self.scan()
        template=self.root/'input.rpk'; before=template.read_bytes()
        bridge=self.root/'bridge.json'
        bridge.write_text(json.dumps({'schema':'dc-visual-bridge-1', 'template':str(template),
            'game_path':'worlds/test.rpk', 'target':report['slots'][0]['target']}))
        output=self.root/'staged'
        result=subprocess.run([os.sys.executable,str(tool),'stage',str(tool.parent/'assets/flat_loop/track.json'),
            '--bridge',str(bridge),'--dcmod',self.exe,'-o',str(output)],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('normal_semantic_present_but_type_unsupported',result.stderr)
        self.assertIn('vertex_streams',result.stderr)
        self.assertNotIn('Traceback',result.stderr)
        self.assertFalse(output.exists()); self.assertEqual(template.read_bytes(),before)
    def test_shared_mentions_and_draw_table(self):
        mesh=self.root/'unpacked/mesh/road_slot.res'; data=bytearray(mesh.read_bytes())
        # Fixture has header(20), then two (24-byte refs + 18-byte record) groups.
        data[62:78]=bytes(16); mesh.write_bytes(data)
        r=self.scan(); self.assertFalse(r['slots'][0]['compatible']); self.assertFalse(r['slots'][1]['independent_buffer_mentions'])
        struct.pack_into('<I',data,104,9); mesh.write_bytes(data)
        r=self.scan(); self.assertFalse(r['slots'][0]['draw_table_found']); self.assertFalse(r['candidates'])
    def test_refuses_output_argument(self):
        result=subprocess.run([self.exe,'mesh','scan',str(self.fixture),'-o',str(self.root/'out')],capture_output=True)
        self.assertEqual(result.returncode,1); self.assertFalse((self.root/'out').exists())
    def test_missing_attribute_buffer(self):
        self.doc['resources']=[r for r in self.doc['resources'] if r['id']!='0003000000000002']
        self.manifest.write_text(json.dumps(self.doc))
        report=self.scan()
        self.assertFalse(report['candidates'])
        self.assertEqual(report['slots'][0]['normal_status'],'no_attribute_stream_with_matching_vertex_count')
    def test_truncated_buffer_fails_read_only(self):
        # Locate by resource ID rather than relying on generated duplicate names.
        record=next(r for r in self.doc['resources'] if r['id']=='0003000000000002')
        attr=self.root/'unpacked'/record['file']
        attr.write_bytes(attr.read_bytes()[:24])
        self.runcli('rpk','build',self.root/'unpacked','-o',self.root/'input.rpk')
        before=(self.root/'input.rpk').read_bytes()
        result=subprocess.run([self.exe,'mesh','scan',str(self.root/'input.rpk')],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('truncated mesh buffer',result.stderr)
        self.assertEqual((self.root/'input.rpk').read_bytes(),before)

if __name__=='__main__': unittest.main()
