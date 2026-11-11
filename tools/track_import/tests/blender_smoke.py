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
