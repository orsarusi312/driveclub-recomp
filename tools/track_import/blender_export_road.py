# SPDX-License-Identifier: GPL-2.0-or-later
"""Blender 4.3+: bake named meshes from an already imported .blend into one GLB.

blender --background private.blend --python blender_export_road.py -- \
    --objects ROAD_1 ROAD_2 --output road.glb
KN5 import is a separate, user-supplied/licensed Blender add-on step.
"""
import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Matrix


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', help='optional local glTF/GLB; imported into this disposable Blender process')
    parser.add_argument('--objects', nargs='+', required=True, help='exact mesh object names; road only')
    parser.add_argument('--output', required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    output = Path(args.output).resolve()
    if output.exists():
        raise RuntimeError('output already exists')
    if args.input:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from gltf_dependencies import inspect
        dependencies = inspect(args.input)
        print(f'Validated {len(dependencies)} glTF buffer/image dependencies')
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.object.delete(use_global=False)
        bpy.ops.import_scene.gltf(filepath=str(Path(args.input).resolve()))
    objects = [bpy.data.objects.get(name) for name in args.objects]
    if any(obj is None or obj.type != 'MESH' for obj in objects):
        raise RuntimeError('every named object must exist and be a mesh')
    bpy.ops.object.select_all(action='DESELECT')
    baked = []
    graph = bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        # Bake evaluated modifiers and the complete world transform into vertex data.
        evaluated = obj.evaluated_get(graph)
        mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=graph)
        mesh.transform(evaluated.matrix_world)
        # Reflections invert orientation; compensate so normals/winding remain consistent.
        if evaluated.matrix_world.determinant() < 0:
            mesh.flip_normals()
        mesh.materials.clear()
        for face in mesh.polygons:
            face.material_index = 0
        fresh = bpy.data.objects.new('baked_road', mesh)
        bpy.context.collection.objects.link(fresh)
        fresh.matrix_world = Matrix.Identity(4)
        fresh.select_set(True)
        baked.append(fresh)
    bpy.context.view_layer.objects.active = baked[0]
    if len(baked) > 1:
        bpy.ops.object.join()
    road = bpy.context.view_layer.objects.active
    road.name = 'external_road'
    modifier = road.modifiers.new(name='Triangles', type='TRIANGULATE')
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    # Export selection only. No inherited textures/materials, rig, cameras or lights.
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB', use_selection=True,
        export_yup=True, export_apply=True, export_normals=True, export_texcoords=True,
        export_materials='NONE', export_animations=False, export_skins=False, export_morph=False,
        export_extras=False, export_cameras=False, export_lights=False)
    # Use the same strict parser as the staging tool, before anyone builds an RPK.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from track_import import read_glb
    imported = read_glb(output)
    print(f"Road export validated: {len(imported['positions'])} vertices, {len(imported['indices']) // 3} triangles")


if __name__ == '__main__':
    main()
