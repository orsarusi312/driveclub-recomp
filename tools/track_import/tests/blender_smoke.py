# SPDX-License-Identifier: GPL-2.0-or-later
# Run in Blender to exercise evaluated mesh export, parent/world transforms and Y-up conversion.
from pathlib import Path
import runpy
import sys
import bpy

root = Path(__file__).resolve().parents[3]
out = root / 'work/blender-road.glb'
if out.exists():
    out.unlink()
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
mesh = bpy.data.meshes.new('road')
# Blender +Z normal, converts to glTF +Y; triangle at world X=10.
mesh.from_pydata([(0,0,0), (2,0,0), (0,2,0)], [], [(0,1,2)])
obj = bpy.data.objects.new('original_road', mesh)
bpy.context.collection.objects.link(obj)
obj.location = (10,0,0)
sys.argv = ['blender', '--', '--objects', obj.name, '--output', str(out)]
runpy.run_path(str(root / 'tools/track_import/blender_export_road.py'), run_name='__main__')
sys.path.insert(0, str(root / 'tools/track_import'))
from track_import import read_glb, surface_height
road = read_glb(out)
assert min(p[0] for p in road['positions']) == 10
assert max(p[0] for p in road['positions']) == 12
assert surface_height(road, 10.25, -0.25) == 0
print('Blender smoke checks passed')

# Round-trip two instances under a transformed parent through the glTF input path.
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
parent=bpy.data.objects.new('track_origin',None)
bpy.context.collection.objects.link(parent)
parent.location=(10,0,0)
mesh=bpy.data.meshes.new('shared_road')
mesh.from_pydata([(0,0,0),(2,0,0),(0,2,0)],[],[(0,1,2)])
for name,location,scale in [('road_a',(0,0,0),(1,1,1)),('road_b',(4,0,0),(-1,1,1))]:
    obj=bpy.data.objects.new(name,mesh)
    bpy.context.collection.objects.link(obj)
    obj.parent=parent; obj.location=location; obj.scale=scale
source=root/'work/hierarchy-input.glb'
output=root/'work/hierarchy-baked.glb'
if output.exists(): output.unlink()
bpy.ops.export_scene.gltf(filepath=str(source),export_format='GLB',export_materials='NONE')
sys.argv=['blender','--','--input',str(source),'--objects','road_a','road_b','--output',str(output)]
runpy.run_path(str(root/'tools/track_import/blender_export_road.py'),run_name='__main__')
road=read_glb(output)
assert len(road['indices'])==6
assert min(p[0] for p in road['positions'])==10
assert max(p[0] for p in road['positions'])==14
assert surface_height(road,10.25,-.25)==0
assert surface_height(road,13.75,-.25)==0
assert all(n[1]>.99 for n in road['normals'])
print('Blender hierarchy, instances and reflected winding checks passed')
