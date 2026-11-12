# SPDX-License-Identifier: GPL-2.0-or-later
"""Original synthetic layouts based on metadata, never game payloads."""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('DCMOD') and os.environ.get('TRACK_FIXTURE'), 'set DCMOD and TRACK_FIXTURE')
class MultiStreamTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.exe = str(Path(os.environ['DCMOD']).resolve())
        self.cli('rpk', 'extract', Path(os.environ['TRACK_FIXTURE']).resolve(), '-o', self.root/'src')
        self.doc = json.loads((self.root/'src/pack.json').read_text())
        self.pos = 0x0003000000000001; self.attr = 0x0003000000000002
        self.posfmt = 0x0002ea626d3b6d17; self.attrfmt = 0x0002000000000001
        pos = self.resource(self.pos)
        self.format(self.posfmt, [(1,9,0),(2,15,0)])
        self.format(self.attrfmt, [(5,8,0),(6,19,0)])
        data = bytearray(pos.read_bytes()[:28]); struct.pack_into('<H', data, 22, 16)
        for i in range(12): data.extend(struct.pack('<fffI', i, 0, i%2, 511 << 10))
        pos.write_bytes(data)
        data = bytearray(self.resource(self.attr).read_bytes()[:28])
        for i in range(12): data.extend(struct.pack('<ff4B', i/12, 0.25, i, 77, 123, 201))
        self.resource(self.attr).write_bytes(data)
        self.pack = self.root/'input.rpk'
        self.build()

    def cli(self, *args, ok=True):
        result = subprocess.run([self.exe, *map(str,args)], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stderr)
        return result

    def resource(self, resource_id):
        record = next(r for r in self.doc['resources'] if r['id'] == f'{resource_id:016x}')
        return self.root/'src'/record['file']

    def format(self, resource_id, fields):
        found = next((r for r in self.doc['resources'] if r['id'] == f'{resource_id:016x}'), None)
        if not found:
            found = {'id':f'{resource_id:016x}', 'type':'stream_format', 'order':len(self.doc['resources']),
                     'names':[], 'sources':[], 'deps':[], 'content':'raw', 'file':f'stream_format/{resource_id:016x}.res'}
            self.doc['resources'].append(found)
        payload = b'EVOSLITL' + struct.pack('<QI',resource_id,1) + bytes([len(fields)])
        payload += b''.join(bytes([kind,typ,index,0,0]) for kind,typ,index in fields)
        (self.root/'src'/found['file']).write_bytes(payload)

    def build(self):
        (self.root/'src/pack.json').write_text(json.dumps(self.doc))
        self.cli('rpk','build',self.root/'src','-o',self.pack)

    def scan(self):
        before = self.pack.read_bytes()
        report = json.loads(self.cli('mesh','scan',self.pack).stdout)
        self.assertEqual(before,self.pack.read_bytes())
        return report

    def test_position_normals_and_prefix_uvs(self):
        slot = self.scan()['slots'][0]
        self.assertTrue(slot['compatible']); self.assertTrue(slot['partial_layout'])
        self.assertEqual(slot['normal_buffer'],f'{self.pos:016x}')
        self.assertEqual(slot['uv_buffer'],f'{self.attr:016x}')
        pos = next(v for v in slot['vertex_streams'] if v['id']==f'{self.pos:016x}')
        attr = next(v for v in slot['vertex_streams'] if v['id']==f'{self.attr:016x}')
        self.assertEqual(pos['normal_offset'],12); self.assertEqual(attr['uv_offset'],0)
        self.assertEqual(attr['opaque_suffix_offset'],8); self.assertEqual(attr['opaque_suffix_bytes'],4)
        self.assertFalse(attr['fields'][1]['byte_width_supported'])
        self.assertIn('unknown_suffix', ' '.join(slot['limitations']))

    def test_isolated_stage_preserves_neighbor_and_opaque_bytes(self):
        from test_track_import import t
        tool = Path(__file__).resolve().parents[1]/'track_import.py'
        bridge = self.root/'bridge.json'
        bridge.write_text(json.dumps({'schema':'dc-visual-bridge-1','template':str(self.pack),
            'game_path':'worlds/test.rpk','target':'0008000000000001_0_road_slot'}))
        source = self.pack.read_bytes()
        # Smaller/equal/larger replacements all relocate every bound stream.
        t.generate(self.root/'track')
        road = t.read_glb(self.root/'track/road.glb')
        for count in (8,12,16):
            with self.subTest(count=count):
                incoming = copy.deepcopy(road)
                for key in ('positions','normals','uvs'): incoming[key] += incoming[key][:count-8]
                (self.root/'track/road.glb').write_bytes(t.make_glb(incoming))
                out = self.root/f'stage-{count}'
                report = t.stage(self.root/'track/track.json',bridge,self.exe,out)
                self.assertTrue(report['slot_compatibility']['partial_layout'])
                staged = out/'files/worlds/test.rpk'
                for pack,label in ((self.pack,'before'),(staged,'after')):
                    self.cli('mesh','export',pack,'-o',self.root/(label+'.glb'),'0008000000000001_1_road_slot')
                self.assertEqual((self.root/'before.glb').read_bytes(),(self.root/'after.glb').read_bytes())
                folder=self.root/f'extract-{count}'
                self.cli('rpk','extract',staged,'-o',folder)
                doc=json.loads((folder/'pack.json').read_text())
                scan=json.loads(self.cli('mesh','scan',staged).stdout)['slots'][0]
                self.assertNotEqual(scan['normal_buffer'],f'{self.pos:016x}')
                self.assertNotEqual(scan['uv_buffer'],f'{self.attr:016x}')
                record=next(r for r in doc['resources'] if r['id']==scan['uv_buffer'])
                data=(folder/record['file']).read_bytes()
                record=next(r for r in doc['resources'] if r['type']=='index_buffer')
                original_indices=self.resource(0x0004000000000001).read_bytes()[28:]
                self.assertEqual((folder/record['file']).read_bytes()[28:28+len(original_indices)],original_indices)
                for i in range(min(count,12)):
                    self.assertEqual(data[28+i*12+8:28+i*12+12],bytes([i,77,123,201]))
                # Added rows retain one donor's complete opaque suffix.
                for i in range(12,count):
                    self.assertIn(data[28+i*12+8:28+i*12+12],[bytes([k,77,123,201]) for k in range(12)])
        self.assertEqual(source,self.pack.read_bytes())

    def test_no_uv_is_not_filled_from_unrelated_stream(self):
        # A position+normal / opaque-only pair models part 1's metadata.
        self.format(self.attrfmt,[(6,19,0)])
        attr=self.resource(self.attr); header=bytearray(attr.read_bytes()[:28]); struct.pack_into('<H',header,22,4)
        attr.write_bytes(header+bytes([1,2,3,4])*12)
        # Unmentioned same-count UV stream must not be borrowed from the mesh deps.
        self.add_stream(0x0003000000000009,0x0002000000000009,[(5,8,0)],8,mention=False)
        self.build(); slot=self.scan()['slots'][0]
        self.assertTrue(slot['normal_decoded']); self.assertFalse(slot['uv_decoded'])
        self.assertIn('no_decoded_uv_for_flat_loop',slot['blockers'])

    def add_stream(self, sid, fmt, fields, stride, mention=True):
        self.format(fmt,fields)
        record={'id':f'{sid:016x}','type':'vertex_buffer','order':len(self.doc['resources']),
                'names':[],'sources':[],'deps':[f'{fmt:016x}'],'content':'raw','file':f'vertex_buffer/{sid:016x}.res'}
        self.doc['resources'].append(record)
        (self.root/'src'/record['file']).write_bytes(b'EVOSLITL'+struct.pack('<QIHHI',sid,1,0,stride,12)+bytes(stride*12))
        mesh=next(r for r in self.doc['resources'] if r['type']=='mesh'); mesh['deps'].append(f'{sid:016x}')
        if mention:
            path=self.root/'src'/mesh['file']; data=path.read_bytes()
            # Insert the extra ID into both original reference groups, from back to front.
            for at in (86,44): data=data[:at]+struct.pack('<Q',sid)+data[at:]
            path.write_bytes(data)

    def test_separate_normal_stream_and_ambiguous_provider(self):
        self.format(self.posfmt,[(1,9,0),(6,19,0)]) # position tail opaque; no normal
        self.add_stream(0x0003000000000003,0x0002000000000003,[(2,15,0)],4)
        self.build(); slot=self.scan()['slots'][0]
        self.assertTrue(slot['compatible']); self.assertEqual(slot['normal_buffer'],'0003000000000003')
        # Restore a second normal declaration: selecting one arbitrarily is forbidden.
        self.format(self.posfmt,[(1,9,0),(2,15,0)]); self.build()
        slot=self.scan()['slots'][0]
        self.assertFalse(slot['normal_decoded']); self.assertIn('ambiguous_normal_streams',slot['blockers'])

    def test_ambiguous_uv_and_mismatched_bound_stream(self):
        self.add_stream(0x0003000000000003,0x0002000000000003,[(5,8,0)],8)
        self.build(); slot=self.scan()['slots'][0]
        self.assertFalse(slot['uv_decoded']); self.assertIn('ambiguous_uv_streams',slot['blockers'])
        path=self.resource(0x0003000000000003); data=bytearray(path.read_bytes())
        struct.pack_into('<I',data,24,11); path.write_bytes(data); self.build()
        self.assertIn('unmatched_vertex_stream_in_draw_block',self.scan()['slots'][0]['blockers'])

    def test_separate_tangent_buffer_is_rebuilt(self):
        from test_track_import import t
        self.add_stream(0x0003000000000003,0x0002000000000003,[(5,15,6)],4)
        self.build(); self.assertTrue(self.scan()['slots'][0]['tangent_decoded'])
        t.generate(self.root/'track')
        bridge=self.root/'bridge.json'
        bridge.write_text(json.dumps({'schema':'dc-visual-bridge-1','template':str(self.pack),
            'game_path':'worlds/test.rpk','target':'0008000000000001_0_road_slot'}))
        t.stage(self.root/'track/track.json',bridge,self.exe,self.root/'stage')
        staged=self.root/'stage/files/worlds/test.rpk'
        slot=json.loads(self.cli('mesh','scan',staged).stdout)['slots'][0]
        self.assertNotEqual(slot['tangent_buffer'],'0003000000000003')
        self.cli('rpk','extract',staged,'-o',self.root/'result')
        doc=json.loads((self.root/'result/pack.json').read_text())
        record=next(r for r in doc['resources'] if r['id']==slot['tangent_buffer'])
        data=(self.root/'result'/record['file']).read_bytes()
        # Planar x/z UVs yield a +X tangent, orthogonal to the supplied +Y normals.
        self.assertEqual(list(struct.unpack('<8I',data[28:])),[511]*8)

    def test_isolated_uv_write_replaces_sanitized_nan(self):
        from test_track_import import t
        import math
        path=self.resource(self.attr); data=bytearray(path.read_bytes())
        for i in range(12): struct.pack_into('<ff',data,28+i*12,float('nan'),1)
        path.write_bytes(data); self.build()
        t.generate(self.root/'track'); road=t.read_glb(self.root/'track/road.glb')
        road['uvs']=[[0,0]]*8; (self.root/'track/road.glb').write_bytes(t.make_glb(road))
        bridge=self.root/'bridge.json'
        bridge.write_text(json.dumps({'schema':'dc-visual-bridge-1','template':str(self.pack),
            'game_path':'worlds/test.rpk','target':'0008000000000001_0_road_slot'}))
        t.stage(self.root/'track/track.json',bridge,self.exe,self.root/'stage')
        staged=self.root/'stage/files/worlds/test.rpk'
        slot=json.loads(self.cli('mesh','scan',staged).stdout)['slots'][0]
        self.cli('rpk','extract',staged,'-o',self.root/'result')
        doc=json.loads((self.root/'result/pack.json').read_text())
        record=next(r for r in doc['resources'] if r['id']==slot['uv_buffer'])
        data=(self.root/'result'/record['file']).read_bytes()
        for i in range(8): self.assertTrue(math.isfinite(struct.unpack_from('<f',data,28+i*12)[0]))

    def test_unknown_prefix_and_malformed_suffix_are_blocked(self):
        for fields in ([(6,19,0),(5,8,0)], [(5,8,0),(6,19,0),(1,9,0)]):
            with self.subTest(fields=fields):
                self.format(self.attrfmt,fields); self.build()
                slot=self.scan()['slots'][0]
                self.assertFalse(slot['uv_decoded']); self.assertFalse(slot['compatible'])
        # Truncated descriptor must never yield a prefix.
        self.format(self.attrfmt,[(5,8,0),(6,19,0)])
        path=self.resource(self.attrfmt); path.write_bytes(path.read_bytes()[:-1]); self.build()
        self.assertFalse(self.scan()['slots'][0]['uv_decoded'])

    def test_shared_reference_owner_is_also_blocked(self):
        mesh=self.resource(0x0008000000000001); data=bytearray(mesh.read_bytes()); data[62:78]=bytes(16)
        mesh.write_bytes(data); self.build()
        slots=self.scan()['slots']; self.assertFalse(slots[0]['compatible']); self.assertFalse(slots[1]['compatible'])
        self.assertFalse(slots[0]['independent_buffer_mentions'])


if __name__=='__main__': unittest.main()
