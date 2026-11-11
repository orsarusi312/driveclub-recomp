#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Track package v2 and offline collision diagnostics; no guest physics calls.

Each visual/collision tile has its own metre/Y-up local origin. Collision is
explicit and independent of visuals. BVH rays are authoring validation only.
"""
import argparse
import json
import math
from pathlib import Path
import sys
from track_import import Invalid, require, read_glb, vector


def sub(a, b): return tuple(x-y for x,y in zip(a,b))
def dot(a, b): return sum(x*y for x,y in zip(a,b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a):
    length = math.sqrt(dot(a,a))
    require(length > 1e-12, 'zero length direction/normal')
    return tuple(x/length for x in a)


def local_file(root, name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name, 'invalid relative asset path')
    path = (root / name).resolve()
    require(not Path(name).is_absolute() and '..' not in Path(name).parts and path.is_relative_to(root), 'asset path escapes package')
    return path


def number(value, label, minimum, maximum):
    require(type(value) in (int, float) and math.isfinite(value) and minimum <= value <= maximum, f'invalid {label}')
    return value


class CollisionIndex:
    """Immutable triangle BVH. No simulation or DriveClub ABI implied."""
    def __init__(self, triangles):
        self.triangles = triangles
        self.nodes = []
        self.last_triangle_tests = 0
        def build(ids):
            lo = tuple(min(triangles[i]['points'][v][k] for i in ids for v in range(3)) for k in range(3))
            hi = tuple(max(triangles[i]['points'][v][k] for i in ids for v in range(3)) for k in range(3))
            node = len(self.nodes)
            self.nodes.append((lo,hi,None,None,None))
            if len(ids) <= 8:
                self.nodes[node] = (lo,hi,ids,None,None)
            else:
                axis = max(range(3), key=lambda k: hi[k]-lo[k])
                ids.sort(key=lambda i: sum(p[axis] for p in triangles[i]['points']))
                middle = len(ids)//2
                left, right = build(ids[:middle]), build(ids[middle:])
                self.nodes[node] = (lo,hi,None,left,right)
            return node
        if triangles:
            build(list(range(len(triangles))))

    def raycast(self, origin, direction, distance, two_sided=False):
        vector(list(origin),3,'ray origin'); vector(list(direction),3,'ray direction')
        number(distance,'ray distance',0,1e9)
        d = unit(direction)
        best, hit = distance, None
        stack = [0] if self.nodes else []
        self.last_triangle_tests = 0
        while stack:
            lo,hi,ids,left,right = self.nodes[stack.pop()]
            near,far = 0.,best
            for k in range(3):
                if abs(d[k]) < 1e-15:
                    if origin[k] < lo[k]-1e-9 or origin[k] > hi[k]+1e-9:
                        near,far = 1.,0.
                        break
                else:
                    a,b = (lo[k]-origin[k])/d[k],(hi[k]-origin[k])/d[k]
                    near,far = max(near,min(a,b)),min(far,max(a,b))
            if near > far+1e-9:
                continue
            if ids is None:
                stack.extend((right,left))
                continue
            for i in ids:
                self.last_triangle_tests += 1
                tri = self.triangles[i]
                a,b,c = tri['points']; e1,e2 = sub(b,a),sub(c,a)
                h = cross(d,e2); determinant = dot(e1,h)
                if abs(determinant) <= 1e-12 or (not two_sided and determinant < 0):
                    continue
                s = sub(origin,a); u = dot(s,h)/determinant
                q = cross(s,e1); v = dot(d,q)/determinant
                t = dot(e2,q)/determinant
                if u < -1e-9 or v < -1e-9 or u+v > 1+1e-9 or t < 0 or t > best:
                    continue
                # Deterministic ownership for shared edges and coplanar triangles.
                if hit is not None and abs(t-best) < 1e-9 and i > hit['triangle']:
                    continue
                best = t
                hit = {'distance':t, 'position':[origin[k]+t*d[k] for k in range(3)],
                       'normal':list(unit(cross(e1,e2))), 'surface':tri['surface'],
                       'tile':tri['tile'], 'triangle':i}
        return hit


class TrackPackage:
    def __init__(self, filename):
        self.tiles, self.surfaces, self.spawns = [], {}, []
        self.index = None
        self.closed = True
        self.root = Path(filename).resolve().parent
        spec = json.loads(Path(filename).read_text(encoding='utf-8'))
        require(isinstance(spec,dict) and spec.get('schema') == 'dc-external-track-2', 'expected dc-external-track-2 package')
        require(spec.get('units') == 'metres' and spec.get('axes') == 'RH_Y_UP', 'package requires metres / RH_Y_UP')
        self.id = spec.get('id')
        require(isinstance(self.id,str) and self.id, 'package needs id')
        surfaces = spec.get('surfaces')
        require(isinstance(surfaces,dict) and surfaces, 'explicit surfaces required')
        for key,value in surfaces.items():
            require(isinstance(value,dict), 'surface must be object')
            # Authoring values ONLY. No mapping to native friction enums is claimed.
            self.surfaces[key] = {'friction':number(value.get('friction'),'surface friction',0,5)}
        tiles = spec.get('tiles')
        require(isinstance(tiles,list) and tiles, 'explicit tiles required')
        cache, triangles, names = {}, [], set()
        for tile in tiles:
            require(isinstance(tile,dict), 'tile must be object')
            name = tile.get('id'); require(isinstance(name,str) and name and name not in names,'duplicate/invalid tile id')
            names.add(name)
            role = tile.get('role'); require(role in ('visual','collision'), 'tile role must be visual or collision')
            if role == 'collision':
                require(tile.get('surface') in self.surfaces, 'collision tile needs known surface')
            origin = vector(tile.get('origin',[0,0,0]),3,'tile origin')
            angle = math.radians(number(tile.get('yaw_degrees',0),'tile yaw',-360,360))
            scale = number(tile.get('scale',1),'tile scale',1e-6,1e6)
            path = local_file(self.root,tile.get('mesh'))
            if path not in cache:
                cache[path] = read_glb(path)
            mesh = cache[path]
            c,s = math.cos(angle),math.sin(angle)
            points = [[origin[0]+scale*(c*x+s*z),origin[1]+scale*y,origin[2]+scale*(-s*x+c*z)] for x,y,z in mesh['positions']]
            require(all(math.isfinite(x) and abs(x)<=1e7 for p in points for x in p), 'transformed coordinates exceed envelope')
            self.tiles.append({'id':name,'role':role,'positions':points,'indices':mesh['indices'], 'surface':tile.get('surface')})
            if role == 'collision':
                for at in range(0,len(mesh['indices']),3):
                    pts = [points[i] for i in mesh['indices'][at:at+3]]
                    require(dot(cross(sub(pts[1],pts[0]),sub(pts[2],pts[0])),cross(sub(pts[1],pts[0]),sub(pts[2],pts[0]))) > 1e-20, 'degenerate transformed collider')
                    triangles.append({'points':pts,'surface':tile['surface'],'tile':name})
                require(len(triangles)<=2_000_000,'offline collision budget exceeded')
        require(triangles, 'missing collision data; visuals do not implicitly become collision')
        self.index = CollisionIndex(triangles)
        spawns = spec.get('spawns')
        require(isinstance(spawns,list) and spawns,'at least one explicit spawn required')
        spawn_ids = set()
        for spawn in spawns:
            require(isinstance(spawn,dict),'spawn must be object')
            name = spawn.get('id'); require(isinstance(name,str) and name and name not in spawn_ids,'duplicate/invalid spawn id')
            spawn_ids.add(name)
            kind = spawn.get('kind','grid'); require(kind in ('grid','respawn','pit'),'invalid spawn kind')
            position = vector(spawn.get('position'),3,'spawn position')
            heading = math.radians(number(spawn.get('heading_degrees',0),'spawn heading',-360,360))
            hit = self.index.raycast(position,(0,-1,0),3)
            require(hit is not None and hit['distance']>=0.1 and hit['normal'][1]>0.1, f'spawn {name} lacks road support/clearance')
            forward = (math.sin(heading),0,math.cos(heading)); normal = hit['normal']
            forward = unit(sub(forward,tuple(dot(forward,normal)*n for n in normal)))
            right = unit(cross(normal,forward))
            self.spawns.append({'id':name,'kind':kind,'position':position,'forward':list(forward),
                                'right':list(right),'up':normal,'support':hit})
        self.closed = False

    def close(self):
        self.tiles.clear(); self.spawns.clear(); self.surfaces.clear()
        self.index = None
        self.closed = True

    def __enter__(self):
        require(not self.closed,'package is closed')
        return self

    def __exit__(self, *_): self.close()

    def report(self):
        require(not self.closed,'package is closed')
        return {'id':self.id,'tiles':len(self.tiles),'collision_triangles':len(self.index.triangles),
                'bvh_nodes':len(self.index.nodes),'surfaces':self.surfaces,'spawns':self.spawns,
                'runtime':'BLOCKED: no DriveClub collider/spawn adapter; offline diagnostics only'}

    def debug_obj(self, filename):
        """Original package geometry + normals, spawn frames; inspect with Blender OBJ import."""
        require(not self.closed,'package is closed')
        path = Path(filename)
        require(not path.exists(),'debug output already exists')
        lines = ['# Offline visual/collision/spawn diagnostic. No game physics.']
        base = 1
        for tile in self.tiles:
            lines.append('o '+tile['role']+'_'+''.join(c if c.isalnum() else '_' for c in tile['id']))
            lines.extend('v '+' '.join(map(str,p)) for p in tile['positions'])
            for at in range(0,len(tile['indices']),3):
                lines.append('f '+' '.join(str(i+base) for i in tile['indices'][at:at+3]))
            base += len(tile['positions'])
        for spawn in self.spawns:
            lines.append('o spawn_'+''.join(c if c.isalnum() else '_' for c in spawn['id']))
            for axis in ('right','up','forward'):
                points = [spawn['position'],[spawn['position'][k]+2*spawn[axis][k] for k in range(3)]]
                lines.extend('v '+' '.join(map(str,p)) for p in points)
                lines.append(f'l {base} {base+1}'); base += 2
        lines.append('o collision_face_normals')
        # Bound diagnostic size; triangle geometry itself is complete.
        for tri in self.index.triangles[:10000]:
            a,b,c = tri['points']; center = [(a[k]+b[k]+c[k])/3 for k in range(3)]
            normal = unit(cross(sub(b,a),sub(c,a)))
            lines.extend('v '+' '.join(map(str,p)) for p in (center,[center[k]+normal[k] for k in range(3)]))
            lines.append(f'l {base} {base+1}'); base += 2
        path.write_text('\n'.join(lines)+'\n',encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package'); parser.add_argument('--debug-obj')
    args = parser.parse_args()
    try:
        with TrackPackage(args.package) as package:
            if args.debug_obj: package.debug_obj(args.debug_obj)
            print(json.dumps(package.report(),indent=2))
        return 0
    except (Invalid,OSError,ValueError,TypeError,KeyError,IndexError) as error:
        print('track-package: '+str(error),file=sys.stderr); return 1


if __name__ == '__main__': sys.exit(main())
