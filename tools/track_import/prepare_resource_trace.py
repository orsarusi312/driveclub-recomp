#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare one private, byte-identical loose-RPK baseline and bounded trace inputs."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def manifest(row, source, relative):
    """Validate reported file spans against the actual source before tracing them."""
    parts = PurePosixPath(relative)
    if (not re.fullmatch(r'[a-z0-9_./-]+\.rpk', relative) or parts.is_absolute()
            or '..' in parts.parts or str(parts) != relative):
        raise ValueError('relative path must be a canonical lowercase app0 RPK path')
    spans = row['source_file_spans']
    expected = [row['mesh_resource'], row['position_buffer'], row['index_buffer']]
    if len(spans) != 3 or [s['id'] for s in spans] != expected or len(set(expected)) != 3:
        raise ValueError('expected distinct mesh, position and index spans')
    lines = ['rpk-trace-v1', '/app0/' + relative]
    size = source.stat().st_size
    with source.open('rb') as stream:
        for s in spans:
            rid, offset, length = s['id'], s['offset'], s['size']
            if (not re.fullmatch('[0-9a-f]{16}', rid) or type(offset) is not int or type(length) is not int
                    or offset < 0 or length < 20 or offset + length > min(size, 0xFFFFFFFF)):
                raise ValueError('invalid source span')
            stream.seek(offset)
            header = stream.read(20)
            if len(header) != 20 or header[:8] != b'EVOSLITL' or struct.unpack_from('<Q', header, 8)[0] != int(rid, 16):
                raise ValueError('span header/id does not match source RPK')
            for prior in spans[:spans.index(s)]:
                if offset < prior['offset'] + prior['size'] and prior['offset'] < offset + length:
                    raise ValueError('overlapping spans')
            lines.append(f'{rid} {offset} {length}')
        if 'draw_probe_signature' in row:
            fields = row['draw_probe_signature'].split(':')
            if len(fields) != 8 or fields[0] != 'v1':
                raise ValueError('invalid position signature')
            stride, width, samples, _, first = map(int, fields[1:6])
            prefix = bytes.fromhex(fields[6])
            offset = 28 + first * stride
            position = next(s for s in spans if s['id'] == row['position_buffer'])
            if (width not in (6, 12) or not width <= stride <= 4096 or not 3 <= samples <= 8
                    or len(prefix) != width * samples or first < 0
                    or offset + (samples-1)*stride + width > position['size']):
                raise ValueError('position signature outside source span')
            for i in range(samples):
                stream.seek(position['offset'] + offset + i*stride)
                if stream.read(width) != prefix[i*width:(i+1)*width]:
                    raise ValueError('position signature does not match source bytes')
            lines[0] = 'rpk-trace-v2'
            lines.append(f"prefix {position['id']} {offset} {stride} {width} {samples} {prefix.hex()}")
    text = '\n'.join(lines) + '\n'
    if len(text.encode('ascii')) >= 4096:
        raise ValueError('trace manifest too long')
    return text


# This is a deliberately fixed follow-up to the consumed-ID control, not a donor search.
CONSUMED_POSITION = '0003d12ccbd11945'
CONSUMED_PAIR = {'00087c1471eba534_0_polySurfaceShape1222': (28, 96),
                 '0008877f16d366b9_0_pPlaneShape147': (64, 186)}


def probe_signature(row):
    """Parse the existing v1 contract; do not convert or round half-float bytes."""
    value = row['draw_probe_signature']
    if not isinstance(value, str) or len(value) > 400 or not re.fullmatch(
            r'v1:(?:[0-9]+:){5}[0-9a-f]+:[0-9a-f]+', value):
        raise ValueError('invalid probe signature')
    fields = value.split(':')
    stride, width, samples, count, first = map(int, fields[1:6])
    xyz, indices = bytes.fromhex(fields[6]), bytes.fromhex(fields[7])
    if (width not in (6, 12) or not width <= stride <= 4096 or not 3 <= samples <= 8
            or not 0 < count <= 1000000 or count % 3 or first > 65535
            or len(xyz) != samples*width or len(indices) != min(24, count)*2):
        raise ValueError('probe signature outside supported limits')
    return stride, width, samples, count, first, xyz, struct.unpack('<'+'H'*(len(indices)//2), indices)


def possible_signature_alias(a, b):
    """Conservative same-binding test, including the probe's constant rebasing.

    Different sample lengths are compared over their common prefix. Different
    sample starts cannot be excluded from prefixes alone. This is not ownership
    or uniqueness across different attributes/external/unrecognized resources.
    """
    sa, wa, na, ca, fa, pa, ia = probe_signature(a)
    sb, wb, nb, cb, fb, pb, ib = probe_signature(b)
    if (sa, wa, ca) != (sb, wb, cb):
        return False
    if tuple(i-ia[0] for i in ia) != tuple(i-ib[0] for i in ib):
        return False
    return fa-ia[0] != fb-ib[0] or pa[:min(na, nb)*wa] == pb[:min(na, nb)*wb]


def validate_consumed_row(row, source, relative):
    target = row['target']
    if target not in CONSUMED_PAIR:
        raise ValueError('not one of the two observed consumed-resource slots')
    vertices, count = CONSUMED_PAIR[target]
    required = dict(position_buffer=CONSUMED_POSITION, normal_buffer=CONSUMED_POSITION,
                    attribute_buffer='000380084a65ecc7', uv_buffer='000380084a65ecc7',
                    index_buffer='00042bcd5555ffa6', position_stride=12,
                    position_resource_vertices=92, vertices=vertices, index_count=count,
                    triangles=count//3, compatible=True, normal_decoded=True, uv_decoded=True,
                    draw_table_found=True, position_half_float=True, skinned=False, partial_layout=False)
    for key, value in required.items():
        if type(row.get(key)) is not type(value) or row[key] != value:
            raise ValueError(f'{target}: unexpected {key}')
    stride, width, samples, sig_count, first, xyz, indices = probe_signature(row)
    lo, hi, start = row['vertex_first'], row['vertex_last'], row['index_first']
    if (any(type(x) is not int for x in (lo, hi, start)) or
            not 0 <= lo <= hi < 92 or hi-lo+1 != vertices or start < 0 or
            (stride, width, samples, sig_count, first) != (12, 6, 8, count, lo)):
        raise ValueError(f'{target}: signature/range mismatch')
    # Existing span/header/position-prefix validation remains authoritative.
    manifest(row, source, relative)
    spans = {s['id']: s for s in row['source_file_spans']}
    pos, ix, mesh = (spans[row[k]] for k in ('position_buffer', 'index_buffer', 'mesh_resource'))
    if pos['size'] != 28+92*12:
        raise ValueError('consumed position resource size differs from 1132 bytes')
    with source.open('rb') as stream:
        def read(span, at, size):
            if at < 0 or size < 0 or at+size > span['size']:
                raise ValueError('reported range outside resource')
            stream.seek(span['offset']+at)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError('short resource read')
            return data
        if struct.unpack('<HI', read(pos, 22, 6)) != (12, 92):
            raise ValueError('position count/stride header mismatch')
        # Check every XYZ in this small 92-record buffer, never normal/padding bytes.
        import math
        for v in range(92):
            if not all(math.isfinite(x) for x in struct.unpack('<3e', read(pos, 28+v*12, 6))):
                raise ValueError('nonfinite half-float position')
        ix_stride, ix_count = struct.unpack('<HI', read(ix, 22, 6))
        if ix_stride != 2 or 28+ix_count*2 > ix['size'] or start+count > ix_count:
            raise ValueError('index range/stride mismatch')
        raw = read(ix, 28+start*2, count*2)
        actual = struct.unpack('<'+'H'*count, raw)
        if any(i < lo or i > hi for i in actual) or tuple(actual[:len(indices)]) != indices:
            raise ValueError('index bytes/range disagree with signature')
        for key, expected in (
                ('mesh_record_offsets', b'\x02\x03'+struct.pack('<4I', lo, hi, start, count)),
                ('draw_record_offsets', struct.pack('<5I', 3, lo, 0, start, count))):
            offsets = row[key]
            if (not isinstance(offsets, list) or not 1 <= len(offsets) <= 64 or
                    any(type(at) is not int for at in offsets) or len(set(offsets)) != len(offsets)):
                raise ValueError('missing, duplicate or excessive record offsets')
            if any(read(mesh, at, len(expected)) != expected for at in offsets):
                raise ValueError(f'{key}: native record differs from report')
    return dict(target=target, vertex_range_inclusive=[lo, hi],
                index_range_half_open=[start, start+count],
                position_byte_range_in_payload=[28+lo*12, 28+(hi+1)*12],
                index_byte_range_in_payload=[28+start*2, 28+(start+count)*2],
                draw_record_offsets=row['draw_record_offsets'], mesh_record_offsets=row['mesh_record_offsets'],
                draw_record_file_offsets=[mesh['offset']+at for at in row['draw_record_offsets']],
                source_file_spans=row['source_file_spans'], draw_probe_signature=row['draw_probe_signature'],
                position_layout=[v for v in row.get('vertex_streams', []) if v['id'] == CONSUMED_POSITION],
                index_count=count, position_stride=12, xyz_sample_bytes=6,
                source_position_and_index_bytes_verified=True)


def scan_target(dcmod, source, dependencies, target):
    command = [str(dcmod), 'mesh', 'scan', str(source), '--target', target]
    for dependency in dependencies:
        command += ['--dependency', str(Path(dependency).resolve())]
    result = subprocess.run(command, text=True, capture_output=True, check=True)
    scan = json.loads(result.stdout)
    if len(scan['slots']) != 1 or scan['slots'][0]['target'] != target:
        raise ValueError('target must select exactly one slot by full name')
    return scan


def inspect_consumed_pair(dcmod, source, dependencies, full_report, relative, output):
    """Two exact-target read-only checks; no RPK copies, signatures selected, or launch."""
    source, output = Path(source).resolve(), Path(output).resolve()
    if output.exists():
        raise FileExistsError(output)
    before = {str(p.resolve()): digest(p) for p in (source, Path(full_report), *map(Path, dependencies))}
    with Path(full_report).open(encoding='utf-8-sig') as stream:
        report = json.load(stream)
    rows = report['slots']
    if (report.get('schema') != 'dc-mesh-compatibility-1' or not rows or
            report.get('scanned_slots') != len(rows) or report.get('reported_slots') != len(rows)):
        raise ValueError('need the existing complete mesh report, not selection.json or a filtered scan')
    selected = [r for r in rows if r['position_buffer'] == CONSUMED_POSITION]
    if len(selected) != 2 or {r['target'] for r in selected} != set(CONSUMED_PAIR):
        raise ValueError('expected exactly the two reported consumed-resource slots; no automatic substitute')
    checked = []
    for old in selected:
        fresh = scan_target(dcmod, source, dependencies, old['target'])['slots'][0]
        for key in ('draw_probe_signature', 'vertex_first', 'vertex_last', 'index_first',
                    'index_count', 'mesh_record_offsets', 'draw_record_offsets', 'source_file_spans'):
            if old[key] != fresh[key]:
                raise ValueError(f"{old['target']}: stale full report ({key}); no launch")
        evidence = validate_consumed_row(fresh, source, relative)
        evidence['possible_signature_aliases_in_report'] = [r['target'] for r in rows
            if r['target'] != fresh['target'] and possible_signature_alias(fresh, r)]
        checked.append(evidence)
    left, right = checked
    def overlaps(a, b):
        return max(a[0], b[0]) < min(a[1], b[1])
    vertex_ranges = [[c['vertex_range_inclusive'][0], c['vertex_range_inclusive'][1]+1] for c in checked]
    for path, original in before.items():
        if digest(Path(path)) != original:
            raise ValueError('input changed during inspection; no result published')
    result = dict(schema='dc-consumed-pair-1', input_sha256=before, candidates=checked,
                  pair_distinguished_by_index_count=True,
                  shared_position_resource=CONSUMED_POSITION,
                  shared_index_resource='00042bcd5555ffa6',
                  vertex_span_ranges_overlap=overlaps(*vertex_ranges),
                  index_ranges_overlap=overlaps(left['index_range_half_open'], right['index_range_half_open']),
                  signatures_distinct_in_report=all(not c['possible_signature_aliases_in_report'] for c in checked),
                  selected_target=None, launch_prepared=False, runtime_verified=False,
                  scope='verified offline signatures only; no runtime ownership, draw match or visible pixels')
    output.mkdir(parents=True, exist_ok=False)
    (output/'donor-pair.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


def prepare(dcmod, source, dependencies, target, relative, output):
    source = Path(source).resolve()
    output = Path(output).resolve()
    before = digest(source)
    scan = scan_target(dcmod, source, dependencies, target)
    row = scan['slots'][0]
    text = manifest(row, source, relative)
    target_spec = row['mesh_search_target']
    expected = f"t1:{row['position_buffer']}:{row['position_resource_vertices']}:{row['vertices']}"
    if target_spec != expected or not re.fullmatch(r't1:[0-9a-f]{16}:[0-9]+:[0-9]+', target_spec):
        raise ValueError('invalid search target metadata')
    # No import, export or RPK rewrite. An existing output is always an error.
    output.mkdir(parents=True, exist_ok=False)
    copy = output / 'mods' / relative
    copy.parent.mkdir(parents=True)
    shutil.copyfile(source, copy)
    if digest(copy) != before or digest(source) != before:
        raise ValueError('source changed during preparation or copy differs; do not launch')
    (output / 'rpk-trace.txt').write_bytes(text.encode('ascii'))
    (output / 'stock-probe.txt').write_text(row['draw_probe_signature'] + '\n', encoding='ascii')
    (output / 'mesh-search-target.txt').write_text(target_spec + '\n', encoding='ascii')
    (output / 'mesh-scan.json').write_text(json.dumps(scan, indent=2) + '\n', encoding='utf-8')
    report = dict(source_sha256=before, copy_sha256=before, relative=relative, target=target,
                  mesh_search_target=target_spec,
                  scope='byte-identical stock loose-file baseline; no modified geometry',
                  position_prefix_file_offset=next(s['offset'] for s in row['source_file_spans']
                                                  if s['id'] == row['position_buffer'])
                  + 28 + row['vertex_first'] * row['position_stride'])
    (output / 'preparation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dcmod', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--dependency', type=Path, action='append', default=[])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--target')
    mode.add_argument('--inspect-consumed-pair', type=Path, metavar='FULL_REPORT',
                      help='offline validation of the two observed half-float donor slots; no copies or launch')
    parser.add_argument('--relative', default='worlds/canada_road_circuit_02_n.rpk')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.inspect_consumed_pair:
            report = inspect_consumed_pair(args.dcmod.resolve(), args.source, args.dependency,
                                           args.inspect_consumed_pair, args.relative, args.output)
        else:
            report = prepare(args.dcmod.resolve(), args.source, args.dependency, args.target, args.relative, args.output)
    except (OSError, ValueError, KeyError, TypeError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f'resource-trace: {error}\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
