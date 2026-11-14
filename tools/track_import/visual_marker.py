#!/usr/bin/env python3
"""Private donor-local visual experiment. Never writes input packs or game archives."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import subprocess

import track_import as t


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def inset(road, factor):
    t.require(0.05 <= factor <= 0.8, 'factor must be in 0.05..0.8')
    center = [(min(p[k] for p in road['positions']) + max(p[k] for p in road['positions'])) / 2
              for k in range(3)]
    # Positive uniform scaling preserves winding, normals, UVs, vertex order,
    # triangle topology and donor-record correspondence (including opaque fields).
    return dict(road, positions=[[center[k] + factor * (p[k] - center[k]) for k in range(3)]
                                 for p in road['positions']])


def prepare(source, target, dependencies, dcmod, out, factor=0.25):
    source = Path(source).resolve(strict=True)
    dependencies = [Path(p).resolve(strict=True) for p in dependencies]
    out = Path(out).resolve()
    t.require(not out.exists(), 'output directory must be new')
    args = [item for p in dependencies for item in ('--dependency', str(p))]
    inputs = {p: digest(p) for p in [source, *dependencies]}

    def run(*arguments):
        result = subprocess.run([str(dcmod), *map(str, arguments)], text=True, capture_output=True)
        t.require(result.returncode == 0, result.stderr or result.stdout)
        return result.stdout

    def scan(pack):
        slots = json.loads(run('mesh', 'scan', pack, '--target', target, *args))['slots']
        t.require(len(slots) == 1 and slots[0]['target'] == target, 'specify one exact target name, not a wildcard')
        t.require(slots[0]['compatible'], 'incompatible target: ' + ', '.join(slots[0]['blockers']))
        return slots[0]

    before = scan(source)
    # Half quantization can collapse small features after shrinking. Start with
    # float positions; the native prefix probe itself also supports half streams.
    t.require(not before['position_half_float'], 'marker experiment currently requires float32 positions')
    t.require('draw_probe_signature' in before, 'rebuild dcmod for draw-probe signatures')
    out.mkdir(parents=True)
    (out/'stock-probe.txt').write_text(before['draw_probe_signature']+'\n', encoding='ascii')
    run('mesh', 'export', source, target, '-o', out/'donor.glb', *args)
    road = t.read_glb(out/'donor.glb', max_position=1e6)
    changed = inset(road, factor)
    report = {'schema': 'dc-visual-marker-1', 'target': target, 'factor': factor,
              'space': 'donor mesh-local; native scene transform unknown',
              'input_sha256': {str(p): h for p, h in inputs.items()}, 'variants': {},
              'limitations': ['draw records are recognized byte patterns, not proven scene bindings',
                              'LOD selection, culling, shader visibility and occlusion are unverified',
                              'no material, collision, spawn or scene-bound changes']}
    report['source_slot']={key:before[key] for key in
        ('vertices','triangles','vertex_bounds','position_buffer','index_buffer','vertex_first',
         'index_first','mesh_record_offsets','draw_record_offsets','local_resource_consumers')}
    for label, geometry in [('control', road), ('marker', changed)]:
        incoming = out/f'{label}.glb'
        incoming.write_bytes(t.make_glb(geometry, target))
        t.read_glb(incoming, max_position=1e6)  # same strict geometric checks as the track bridge
        pack = out/f'{label}.rpk'
        run('mesh', 'import', source, incoming, '--isolate-buffers', '-o', pack, *args)
        after = scan(pack)
        t.require(after['vertices'] == before['vertices'] and after['triangles'] == before['triangles'],
                  'marker must preserve vertex/triangle counts')
        t.require(after['vertex_first'] == 0 and after['position_buffer'] != before['position_buffer'],
                  'expected an independently rebound position stream starting at zero')
        for k in range(3):
            t.require(after['vertex_bounds'][0][k] >= before['vertex_bounds'][0][k] and
                      after['vertex_bounds'][1][k] <= before['vertex_bounds'][1][k],
                      'quantized marker leaves donor bounds')
        run('mesh', 'export', pack, target, '-o', out/f'{label}-roundtrip.glb', *args)
        exported = t.read_glb(out/f'{label}-roundtrip.glb', max_position=1e6)
        t.require(exported['indices'] == road['indices'], 'triangle topology changed during import')
        for actual, expected in zip(exported['positions'], geometry['positions']):
            t.require(all(abs(a-b) <= 1e-6 * max(1, abs(b)) for a, b in zip(actual, expected)),
                      'position roundtrip mismatch')
        (out/f'{label}-probe.txt').write_text(after['draw_probe_signature']+'\n', encoding='ascii')
        report['variants'][label] = {key: after[key] for key in
            ('vertex_bounds', 'vertices', 'triangles', 'position_buffer', 'index_buffer',
             'vertex_first', 'index_first', 'index_count', 'mesh_record_offsets', 'draw_record_offsets')}
        report['variants'][label]['sha256'] = digest(pack)
    t.require(report['variants']['marker']['vertex_bounds'] != before['vertex_bounds'], 'marker did not change bounds')
    t.require(all(digest(p) == h for p, h in inputs.items()), 'read-only input changed during preparation')
    (out/'marker-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--target', required=True)
    p.add_argument('--dependency', action='append', default=[], type=Path)
    p.add_argument('--dcmod', required=True)
    p.add_argument('--factor', type=float, default=0.25)
    p.add_argument('-o', '--output', required=True, type=Path)
    a = p.parse_args()
    try:
        prepare(a.source, a.target, a.dependency, a.dcmod, a.output, a.factor)
        print(f'Prepared control and inset marker: {a.output}. In-game visibility is not established.')
    except (ValueError, OSError, KeyError) as e:
        print(f'visual-marker: {e}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
