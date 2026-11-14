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
    text = '\n'.join(lines) + '\n'
    if len(text.encode('ascii')) >= 4096:
        raise ValueError('trace manifest too long')
    return text


def prepare(dcmod, source, dependencies, target, relative, output):
    source = Path(source).resolve()
    output = Path(output).resolve()
    before = digest(source)
    command = [str(dcmod), 'mesh', 'scan', str(source), '--target', target]
    for dependency in dependencies:
        command += ['--dependency', str(Path(dependency).resolve())]
    result = subprocess.run(command, text=True, capture_output=True, check=True)
    scan = json.loads(result.stdout)
    if len(scan['slots']) != 1 or scan['slots'][0]['target'] != target:
        raise ValueError('target must select exactly one slot by full name')
    row = scan['slots'][0]
    text = manifest(row, source, relative)
    # No import, export or RPK rewrite. An existing output is always an error.
    output.mkdir(parents=True, exist_ok=False)
    copy = output / 'mods' / relative
    copy.parent.mkdir(parents=True)
    shutil.copyfile(source, copy)
    if digest(copy) != before or digest(source) != before:
        raise ValueError('source changed during preparation or copy differs; do not launch')
    (output / 'rpk-trace.txt').write_bytes(text.encode('ascii'))
    (output / 'stock-probe.txt').write_text(row['draw_probe_signature'] + '\n', encoding='ascii')
    (output / 'mesh-scan.json').write_text(json.dumps(scan, indent=2) + '\n', encoding='utf-8')
    report = dict(source_sha256=before, copy_sha256=before, relative=relative, target=target,
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
    parser.add_argument('--target', required=True)
    parser.add_argument('--relative', default='worlds/canada_road_circuit_02_n.rpk')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = prepare(args.dcmod.resolve(), args.source, args.dependency, args.target, args.relative, args.output)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'resource-trace: {error}\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
