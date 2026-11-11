# SPDX-License-Identifier: GPL-2.0-or-later
import json
import math
from pathlib import Path
import random
import shutil
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from track_package import CollisionIndex, TrackPackage, Invalid, dot


class CollisionTests(unittest.TestCase):
    def test_slope_wall_backface_nearest_and_distance(self):
        index = CollisionIndex([
            {'points':[(0,0,0),(0,0,10),(10,10,0)],'surface':'bank','tile':'road'},
            {'points':[(0,20,0),(0,20,10),(10,20,0)],'surface':'upper','tile':'bridge'},
            {'points':[(5,0,0),(5,10,0),(5,0,10)],'surface':'wall','tile':'barrier'}])
        hit = index.raycast((2,5,2),(0,-1,0),10)
        self.assertAlmostEqual(hit['position'][1],2)
        self.assertAlmostEqual(hit['normal'][0],-math.sqrt(.5))
        self.assertAlmostEqual(hit['normal'][1],math.sqrt(.5))
        self.assertEqual(index.raycast((2,30,2),(0,-1,0),50)['surface'],'upper')
        self.assertIsNone(index.raycast((2,30,2),(0,-1,0),5))
        self.assertIsNone(index.raycast((2,1,2),(0,1,0),10))
        self.assertIsNotNone(index.raycast((2,1,2),(0,1,0),10,two_sided=True))
        self.assertEqual(index.raycast((7,2,2),(-1,0,0),10)['surface'],'wall')

    def test_bvh_matches_bruteforce_and_prunes_large_spaced_scene(self):
        tris = [{'points':[(x,0,z),(x,0,z+1),(x+1,0,z)],'surface':'s','tile':'road'}
                for x in range(0,100,2) for z in range(0,100,2)]
        index=CollisionIndex(tris)
        rng=random.Random(123)
        for _ in range(20):
            x,z=rng.randrange(50)*2+.1,rng.randrange(50)*2+.1
            hit=index.raycast((x,3,z),(0,-1,0),10)
            self.assertIsNotNone(hit)
            self.assertLess(index.last_triangle_tests,len(tris)//10)
            expected=next(i for i,t in enumerate(tris) if t['points'][0][0]<=x<t['points'][0][0]+1 and t['points'][0][2]<=z<t['points'][0][2]+1)
            self.assertEqual(hit['triangle'],expected)
        self.assertIsNone(index.raycast((101,3,101),(0,-1,0),10))

    def test_invalid_ray(self):
        index=CollisionIndex([])
        for direction in [(0,0,0),(0,float('nan'),0)]:
            with self.assertRaises(Invalid): index.raycast((0,0,0),direction,10)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)
        source=Path(__file__).resolve().parents[1]/'assets/flat_loop'
        shutil.copytree(source,self.root/'assets')
        self.path=self.root/'assets/package.json'
        self.spec=json.loads(self.path.read_text())

    def write(self): self.path.write_text(json.dumps(self.spec))

    def test_explicit_collision_spawns_debug_and_lifetime(self):
        for _ in range(30):
            with TrackPackage(self.path) as package:
                self.assertEqual(package.report()['collision_triangles'],8)
                spawn=package.spawns[0]
                self.assertAlmostEqual(dot(spawn['up'],spawn['forward']),0)
                self.assertEqual(spawn['support']['surface'],'asphalt')
            self.assertTrue(package.closed)
            self.assertIsNone(package.index)
            self.assertEqual(package.tiles,[])
            with self.assertRaises(Invalid): package.report()
        with TrackPackage(self.path) as package:
            package.debug_obj(self.root/'debug.obj')
            self.assertIn('o collision_road_contact',(self.root/'debug.obj').read_text())
            with self.assertRaises(Invalid): package.debug_obj(self.root/'debug.obj')

    def test_instance_transform_large_origin_and_scale(self):
        # Road is tiny in local coordinates, translated 30 km without half-float positions.
        for tile in self.spec['tiles']:
            tile.update(origin=[30000,2,0],yaw_degrees=90,scale=2)
        self.spec['spawns']=[{'id':'grid','position':[29950,3,0],'heading_degrees':180}]
        self.write()
        with TrackPackage(self.path) as package:
            self.assertAlmostEqual(package.spawns[0]['support']['position'][1],2)
            self.assertAlmostEqual(package.spawns[0]['support']['position'][0],29950)

    def test_missing_collision_unknown_surface_and_bad_spawn(self):
        original=json.loads(json.dumps(self.spec))
        for bad in ('collision','surface','spawn','path','duplicate','scale'):
            self.spec=json.loads(json.dumps(original))
            if bad=='collision': self.spec['tiles']=self.spec['tiles'][:1]
            if bad=='surface': self.spec['tiles'][1]['surface']='unknown'
            if bad=='spawn': self.spec['spawns'][0]['position']=[0,1,0]
            if bad=='path': self.spec['tiles'][1]['mesh']='../escape.glb'
            if bad=='duplicate': self.spec['tiles'].append(self.spec['tiles'][0])
            if bad=='scale': self.spec['tiles'][0]['scale']=-1
            self.write()
            with self.subTest(bad=bad), self.assertRaises(Invalid): TrackPackage(self.path)


if __name__=='__main__': unittest.main()
