#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""External road authoring and experimental dcmod visual-mesh bridge.

No DriveClub collision, spawn or race resources are generated. Python 3.10+;
standard library only. All coordinates are metres, right handed, Y up.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile


class Invalid(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise Invalid(message)


def integer(value, label, minimum=0):
    require(type(value) is int and value >= minimum, f'{label}: expected integer >= {minimum}')
    return value


def vector(value, n, label):
    require(isinstance(value, list) and len(value) == n, f'{label}: expected {n} numbers')
    require(all(type(x) in (int, float) and math.isfinite(x) for x in value), f'{label}: non-finite/non-numeric value')
    return value


def mapping(value, label):
    require(isinstance(value, dict), f'{label}: expected object')
    return value


def objects(value, label):
    require(isinstance(value, list) and all(isinstance(v, dict) for v in value), f'{label}: expected array of objects')
    return value


def read_glb(path, *, max_position=10000):
    data = Path(path).read_bytes()
    require(len(data) >= 20, 'truncated GLB header')
    magic, version, size = struct.unpack_from('<III', data)
    require(magic == 0x46546C67 and version == 2 and size == len(data), 'invalid GLB 2 header/length')
    chunks, at = [], 12
    while at < len(data):
        require(at + 8 <= len(data), 'truncated chunk header')
        length, kind = struct.unpack_from('<II', data, at)
        at += 8
        require(length % 4 == 0 and length <= len(data) - at, 'invalid chunk length')
        chunks.append((kind, data[at:at + length]))
        at += length
    require([c[0] for c in chunks] == [0x4E4F534A, 0x004E4942], 'expected JSON then BIN only')
    root = mapping(json.loads(chunks[0][1]), 'glTF root')
    binary = chunks[1][1]
    require(mapping(root.get('asset', {}), 'asset').get('version') == '2.0', 'expected glTF 2.0')
    require(not root.get('extensionsRequired') and not root.get('extensionsUsed'), 'extensions unsupported; export plain glTF')
    buffers = objects(root.get('buffers', []), 'buffers')
    require(len(buffers) == 1 and 'uri' not in buffers[0], 'one embedded buffer required')
    size = integer(buffers[0].get('byteLength'), 'buffer length')
    require(size <= len(binary) <= size + 3, 'buffer length mismatch')
    binary = binary[:size]

    def accessor(index, kind, types):
        integer(index, 'accessor index')
        accs = objects(root.get('accessors', []), 'accessors')
        require(index < len(accs), 'missing accessor')
        acc = accs[index]
        require(acc.get('type') == kind and acc.get('componentType') in types, 'unsupported accessor format')
        require('sparse' not in acc and not acc.get('normalized'), 'sparse/normalized accessors unsupported')
        vi = integer(acc.get('bufferView'), 'bufferView')
        views = objects(root.get('bufferViews', []), 'bufferViews')
        require(vi < len(views), 'missing bufferView')
        view = views[vi]
        require(view.get('buffer', 0) == 0 and not view.get('extensions'), 'unsupported bufferView')
        count = integer(acc.get('count'), 'accessor count', 1)
        require(count <= 10_000_000, 'accessor count exceeds prototype limit')
        fmt = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[acc['componentType']]
        components = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[kind]
        component_size = struct.calcsize('<' + fmt)
        width = component_size * components
        stride = integer(view.get('byteStride', width), 'stride', width)
        start = integer(view.get('byteOffset', 0), 'view offset')
        length = integer(view.get('byteLength'), 'view length')
        offset = integer(acc.get('byteOffset', 0), 'accessor offset')
        require(offset % component_size == 0 and (start + offset) % component_size == 0 and stride % component_size == 0, 'misaligned accessor')
        if 'byteStride' in view:
            require(4 <= stride <= 252 and stride % 4 == 0, 'invalid glTF byteStride')
        require(start + length <= len(binary) and offset + (count - 1) * stride + width <= length,
                'accessor outside its bufferView')
        return [list(struct.unpack_from('<' + fmt * components, binary, start + offset + i * stride)) for i in range(count)]

    # Deliberately narrow contract: do not silently ignore scene transforms or instances.
    nodes, meshes, scenes = [objects(root.get(key, []), key) for key in ('nodes', 'meshes', 'scenes')]
    require(len(nodes) == len(meshes) == len(scenes) == 1 and root.get('scene', 0) == 0,
            'export one joined road object and one scene')
    node = nodes[0]
    require(scenes[0].get('nodes') == [0] and node.get('mesh') == 0, 'invalid scene/mesh reference')
    require(not node.get('children') and 'skin' not in node and not root.get('animations') and not root.get('skins'),
            'hierarchy, skinning and animation unsupported')
    for key, identity in [('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]), ('scale', [1, 1, 1]),
                          ('matrix', [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])]:
        if key in node:
            require(vector(node[key], len(identity), key) == identity, 'bake object transforms before export')
    primitives = objects(meshes[0].get('primitives', []), 'primitives')
    require(len(primitives) == 1, 'one road primitive/material required; split large tracks into tiles later')
    prim = primitives[0]
    require(prim.get('mode', 4) == 4 and not prim.get('targets') and not prim.get('extensions'), 'triangle lists only')
    attributes = mapping(prim.get('attributes', {}), 'attributes')
    require(set(attributes) <= {'POSITION', 'NORMAL', 'TEXCOORD_0'} and 'POSITION' in attributes,
            'only POSITION, NORMAL and TEXCOORD_0 supported')
    positions = accessor(attributes['POSITION'], 'VEC3', [5126])
    require('NORMAL' in attributes, 'export explicit normals; generated normals are not supported by this bridge')
    require(3 <= len(positions) <= 65535, 'road needs 3..65535 vertices per mesh slot')
    for p in positions:
        vector(p, 3, 'position')
        require(max(map(abs, p)) <= max_position, f'position exceeds {max_position:g} m coordinate limit; rebase near origin')
    indices = [v[0] for v in accessor(prim['indices'], 'SCALAR', [5121, 5123, 5125])] if 'indices' in prim else list(range(len(positions)))
    require(indices and len(indices) % 3 == 0 and max(indices) < len(positions), 'invalid triangle indices')
    normals = accessor(attributes['NORMAL'], 'VEC3', [5126]) if 'NORMAL' in attributes else None
    uvs = accessor(attributes['TEXCOORD_0'], 'VEC2', [5126]) if 'TEXCOORD_0' in attributes else None
    if normals is not None:
        require(all(abs(sum(x*x for x in n) - 1) <= 0.02 for n in normals), 'normals must be unit length')
    for values, width in [(normals, 3), (uvs, 2)]:
        if values is not None:
            require(len(values) == len(positions), 'attribute count mismatch')
            for value in values:
                vector(value, width, 'attribute')
    for i in range(0, len(indices), 3):
        a, b, c = [positions[k] for k in indices[i:i + 3]]
        ab, ac = [b[k] - a[k] for k in range(3)], [c[k] - a[k] for k in range(3)]
        cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        require(sum(x*x for x in cross) > 1e-16, f'degenerate triangle {i // 3}')
    return {'positions': positions, 'indices': indices, 'normals': normals, 'uvs': uvs}


def make_glb(road, name='external_road'):
    binary, views, accs = bytearray(), [], []

    def add(values, fmt, kind, components):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        flat = [x for v in values for x in v] if components > 1 else values
        binary.extend(struct.pack('<' + fmt * len(flat), *flat))
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset})
        acc = {'bufferView': len(views) - 1, 'componentType': 5126 if fmt == 'f' else 5125,
               'count': len(values), 'type': kind}
        if kind == 'VEC3':
            acc.update(min=[min(v[k] for v in values) for k in range(3)], max=[max(v[k] for v in values) for k in range(3)])
        accs.append(acc)
        return len(accs) - 1

    attrs = {'POSITION': add(road['positions'], 'f', 'VEC3', 3)}
    for field, semantic, kind, n in [('normals', 'NORMAL', 'VEC3', 3), ('uvs', 'TEXCOORD_0', 'VEC2', 2)]:
        if road.get(field) is not None:
            attrs[semantic] = add(road[field], 'f', kind, n)
    indices = add(road['indices'], 'I', 'SCALAR', 1)
    root = {'asset': {'version': '2.0', 'generator': 'DriveClub external road prototype'}, 'scene': 0,
            'scenes': [{'nodes': [0]}], 'nodes': [{'name': name, 'mesh': 0}],
            'meshes': [{'primitives': [{'attributes': attrs, 'indices': indices, 'mode': 4}]}],
            'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accs}
    js = json.dumps(root, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    binary += b'\0' * (-len(binary) % 4)
    return (struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(binary)) +
            struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(binary), 0x004E4942) + binary)


def surface_height(road, x, z):
    """Highest upward-facing triangle at XZ. Offline support probe, NOT game physics."""
    heights = []
    for i in range(0, len(road['indices']), 3):
        a, b, c = [road['positions'][k] for k in road['indices'][i:i + 3]]
        den = (b[2]-c[2])*(a[0]-c[0]) + (c[0]-b[0])*(a[2]-c[2])
        # Cross-product Y is -den: only upward winding supports a vehicle.
        if den >= -1e-10:
            continue
        u = ((b[2]-c[2])*(x-c[0]) + (c[0]-b[0])*(z-c[2])) / den
        v = ((c[2]-a[2])*(x-c[0]) + (a[0]-c[0])*(z-c[2])) / den
        if min(u, v, 1-u-v) >= -1e-7:
            heights.append(u*a[1] + v*b[1] + (1-u-v)*c[1])
    return max(heights) if heights else None


def load_track(config):
    config = Path(config).resolve()
    spec = mapping(json.loads(config.read_text(encoding='utf-8')), 'track config')
    require(spec.get('schema') == 'dc-external-track-1', 'unsupported track schema')
    require(isinstance(spec.get('id'), str) and re.fullmatch(r'[a-z0-9_-]+', spec['id']) is not None, 'invalid track id')
    require(spec.get('units') == 'metres' and spec.get('axes') == 'RH_Y_UP', 'expected metres and RH_Y_UP')
    require(isinstance(spec.get('mesh'), str), 'mesh: expected relative path string')
    relative = PurePosixPath(spec['mesh'])
    require(str(relative) != '.' and not relative.is_absolute() and '..' not in relative.parts and '\\' not in str(relative), 'mesh must be package relative')
    mesh = (config.parent / str(relative)).resolve()
    require(mesh.is_relative_to(config.parent), 'mesh escapes package')
    road = read_glb(mesh)
    spawn = mapping(spec.get('spawn', {}), 'spawn')
    p = vector(spawn.get('position'), 3, 'spawn.position')
    heading = spawn.get('heading_degrees')
    require(type(heading) in (int, float) and math.isfinite(heading), 'invalid spawn heading')
    height = surface_height(road, p[0], p[2])
    require(height is not None, 'spawn has no upward-facing road beneath it')
    require(0.1 <= p[1] - height <= 3.0, 'spawn needs 0.1..3 m clearance above road')
    return spec, road, {'id': spec['id'], 'vertices': len(road['positions']), 'triangles': len(road['indices']) // 3,
                        'bounds': [[min(p[k] for p in road['positions']) for k in range(3)],
                                   [max(p[k] for p in road['positions']) for k in range(3)]],
                        'mesh_sha256': hashlib.sha256(mesh.read_bytes()).hexdigest(), 'spawn_surface_y': height,
                        'status': 'authoring-only; no game collision or spawn integration'}


def generate(destination):
    """Original 100 x 60 m rectangular loop, 10 m wide; centre is a real hole."""
    destination = Path(destination)
    require(not destination.exists(), 'output already exists')
    # Each quad is counterclockwise when seen from above (normal +Y).
    outer = [(-50, -30), (-50, 30), (50, 30), (50, -30)]
    inner = [(-40, -20), (-40, 20), (40, 20), (40, -20)]
    positions = [[x, 0, z] for x, z in outer + inner]
    indices = []
    for i in range(4):
        j = (i + 1) % 4
        indices += [i, j, j+4, i, j+4, i+4]
    road = {'positions': positions, 'indices': indices, 'normals': [[0, 1, 0]]*8,
            'uvs': [[x/10, z/10] for x, _, z in positions]}
    destination.mkdir(parents=True)
    (destination / 'road.glb').write_bytes(make_glb(road))
    spec = {'schema': 'dc-external-track-1', 'id': 'flat_loop', 'units': 'metres', 'axes': 'RH_Y_UP',
            'mesh': 'road.glb', 'spawn': {'position': [0, 0.75, -25], 'heading_degrees': 90}}
    (destination / 'track.json').write_text(json.dumps(spec, indent=2) + '\n', encoding='utf-8')
    return load_track(destination / 'track.json')[2]


def run_dcmod(executable, *args):
    result = subprocess.run([str(executable), *map(str, args)], text=True, capture_output=True)
    require(result.returncode == 0, f'dcmod failed: {result.stderr or result.stdout}')
    print(result.stdout, end='', file=sys.stderr)
    return result.stdout


def stage(config, bridge_path, dcmod, output):
    """Replace one explicitly selected visual slot in an extracted LOCAL template.

    Output must be new, is never a dump directory, and contains a loose-file mod.
    No archive writes. Never claims to update road physics or scene bounds.
    """
    spec, road, report = load_track(config)
    bridge_path = Path(bridge_path).resolve()
    bridge = mapping(json.loads(bridge_path.read_text(encoding='utf-8')), 'bridge config')
    require(bridge.get('schema') == 'dc-visual-bridge-1', 'unsupported bridge schema')
    require(isinstance(bridge.get('template'), str), 'template: expected local path string')
    template = (bridge_path.parent / bridge['template']).resolve()
    require(template.is_file() and template.suffix.lower() == '.rpk', 'expected local extracted template .rpk')
    game_path = bridge['game_path']
    require(isinstance(game_path, str) and re.fullmatch(r'(worlds|data/leveldata)/[A-Za-z0-9_-]+\.rpk', game_path), 'invalid game RPK path')
    target = bridge['target']
    require(isinstance(target, str) and re.fullmatch(r'[0-9a-fA-F]{16}_[0-9]+_.+', target), 'use full exact dcmod mesh info target name')
    offset = vector(bridge.get('translation', [0, 0, 0]), 3, 'translation')
    road = dict(road, positions=[[p[k] + offset[k] for k in range(3)] for p in road['positions']])
    require(max(abs(x) for p in road['positions'] for x in p) < 65000, 'translation exceeds half-float range')
    output = Path(output).resolve()
    require(not output.exists(), 'output already exists; choose a new staging folder')
    require(not template.is_relative_to(output), 'output contains template')
    # Export the exact slot to prove the name is present before the legacy importer is called.
    with tempfile.TemporaryDirectory(prefix='dc-road-') as tmp:
        tmp = Path(tmp)
        run_dcmod(dcmod, 'mesh', 'export', template, '-o', tmp / 'slot.glb', target)
        data = (tmp / 'slot.glb').read_bytes()
        length = struct.unpack_from('<I', data, 12)[0]
        slot = json.loads(data[20:20+length])
        require(len(slot.get('nodes', [])) == 1 and slot['nodes'][0].get('name') == target, 'target must match exactly one mesh slot')
        slot_attrs = slot['meshes'][0]['primitives'][0]['attributes']
        require('NORMAL' in slot_attrs, 'target needs a decoded normal stream')
        require(road['uvs'] is None or 'TEXCOORD_0' in slot_attrs, 'target cannot preserve supplied UVs')
        # The existing importer ignores node transforms; write already-baked coordinates.
        (tmp / 'mapped.glb').write_bytes(make_glb(road, target))
        run_dcmod(dcmod, 'mesh', 'import', template, tmp / 'mapped.glb', '--isolate-buffers', '-o', tmp / 'visual.rpk')
        # Read it back through the real exporter, checking that the new geometry survived quantization.
        run_dcmod(dcmod, 'mesh', 'export', tmp / 'visual.rpk', '-o', tmp / 'verify.glb', target)
        actual = read_glb(tmp / 'verify.glb', max_position=65504)
        require(actual['indices'] == road['indices'], 'RPK triangle round-trip mismatch')
        require(len(actual['positions']) == len(road['positions']), 'RPK vertex count mismatch')
        require(all(abs(a-b) <= max(0.002, abs(b)*0.001) for av, bv in zip(actual['positions'], road['positions']) for a, b in zip(av, bv)), 'RPK position round-trip mismatch')
        for field, tolerance in [('normals', 0.005), ('uvs', 0.00001)]:
            if road[field] is not None:
                require(actual[field] is not None and len(actual[field]) == len(road[field]), f'RPK {field} count mismatch')
                require(all(abs(a-b) <= tolerance for av, bv in zip(actual[field], road[field]) for a,b in zip(av,bv)), f'RPK {field} round-trip mismatch')
        manifest = {'id': 'experimental.' + spec['id'], 'name': spec['id'] + ' VISUAL SLOT TEST', 'version': '0.1.0',
                    'category': 'Track', 'files': 'files', 'description': 'Experimental original mesh replacement. Existing collision, route and spawn are unchanged.'}
        report.update(status='visual slot staged; in-game rendering unverified; NOT a playable new track',
                      game_path=game_path, target=target, translation=offset,
                      template_sha256=hashlib.sha256(template.read_bytes()).hexdigest())
        # Assemble beside the destination so every write succeeds before publication.
        # Claim a new directory (mkdir cannot overwrite an existing directory/symlink)
        # and clean it up if publication fails; never leave a half-written mod.
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.dc-road-stage-', dir=output.parent) as pending:
            pending = Path(pending)
            target_path = pending / 'files' / game_path
            target_path.parent.mkdir(parents=True)
            shutil.copyfile(tmp / 'visual.rpk', target_path)
            (pending / 'mod.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            (pending / 'import-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            output.mkdir()
            try:
                for item in pending.iterdir():
                    item.rename(output / item.name)
            except BaseException:
                shutil.rmtree(output)
                raise
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    gen = sub.add_parser('generate', help='create original flat loop assets in a NEW directory')
    gen.add_argument('-o', '--output', required=True)
    check = sub.add_parser('check', help='validate road and offline spawn support (no game data)')
    check.add_argument('config')
    export = sub.add_parser('stage', help='experimental visual RPK replacement; NOT game physics')
    export.add_argument('config')
    export.add_argument('--bridge', required=True)
    export.add_argument('--dcmod', required=True)
    export.add_argument('-o', '--output', required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'generate':
            report = generate(args.output)
        elif args.command == 'check':
            report = load_track(args.config)[2]
        else:
            report = stage(args.config, args.bridge, args.dcmod, args.output)
        print(json.dumps(report, indent=2))
        return 0
    except (Invalid, OSError, ValueError, KeyError, TypeError, IndexError, struct.error) as exc:
        print(f'track-import: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
