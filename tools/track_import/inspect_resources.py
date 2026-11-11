#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Collect metadata evidence from local RPKs via dcmod; never export game payloads.

Payloads exist only inside an automatically removed temporary directory while
headers are inspected. Final JSON omits payload bytes, resource names, original
source paths, and absolute local paths. Dependencies unresolved within supplied
packs are leads, not errors: country/common packs may provide them.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


class EvidenceError(ValueError):
    pass


def need(condition, message):
    if not condition:
        raise EvidenceError(message)


def resource_id(value):
    need(isinstance(value, str) and re.fullmatch('[0-9a-fA-F]{16}', value), 'invalid resource identifier')
    return value.lower()


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def summarize_manifest(manifest, folder):
    """Translate dcmod's extracted representation into a metadata-only report."""
    need(manifest.get('format') == 'dcmod-rpk', 'expected a dcmod RPK manifest')
    folder = Path(folder).resolve()
    resources = []
    seen = set()
    for entry in manifest['resources']:
        rid = resource_id(entry['id'])
        need(rid not in seen, 'duplicate resource identifier in pack')
        seen.add(rid)
        filename = entry['file']
        need(isinstance(filename, str) and '\\' not in filename, 'invalid temporary resource path')
        relative = Path(filename)
        need(not relative.is_absolute() and '..' not in relative.parts, 'resource path escapes temporary extraction')
        path = (folder / relative).resolve()
        need(path.is_relative_to(folder) and path.is_file(), 'resource missing or outside temporary extraction')
        size = path.stat().st_size
        content = entry['content']
        if content == 'raw':
            with path.open('rb') as stream:
                header = stream.read(20)
            header_valid = len(header) == 20 and header[:8] == b'EVOSLITL'
            payload_id = f'{struct.unpack_from("<Q", header, 8)[0]:016x}' if header_valid else None
            version = struct.unpack_from('<I', header, 16)[0] if header_valid else None
            stored_size = size
        elif content == 'block':
            # dcmod strips the 8+8+4+4 byte wrapper for binary/XML blobs.
            version = entry['version']
            need(type(version) is int and 0 <= version <= 0xffffffff, 'invalid block payload version')
            payload_id, header_valid, stored_size = rid, True, size + 24
        else:
            raise EvidenceError('unknown extracted payload representation')
        resources.append({'id': rid, 'type': entry['type'], 'type_number': int(rid[:4], 16),
                          'stored_bytes': stored_size, 'payload_version': version,
                          'payload_header_valid': header_valid,
                          'payload_id_matches': payload_id == rid,
                          'header_evidence': 'raw-header' if content == 'raw' else 'dcmod-decoded-block',
                          'dependencies': [resource_id(dep) for dep in entry['deps']]})
    by_type = Counter(r['type'] for r in resources)
    bytes_by_type = Counter()
    for item in resources:
        bytes_by_type[item['type']] += item['stored_bytes']
    return {'toc': manifest['info'], 'root': resource_id(manifest['root']),
            'required_packs': manifest['required'],
            'types': {kind: {'count': by_type[kind], 'stored_bytes': bytes_by_type[kind]} for kind in sorted(by_type)},
            'resources': sorted(resources, key=lambda item: item['id'])}


def inspect(pack, dcmod):
    pack = Path(pack).resolve()
    need(pack.is_file() and pack.suffix.lower() == '.rpk', 'input must be an existing local .rpk')
    before = digest(pack)
    with tempfile.TemporaryDirectory(prefix='dc-track-evidence-') as temp:
        # dcmod remains the format authority. No hand-written serializer or game writes.
        command = [str(dcmod), 'rpk', 'extract', str(pack), '-o', temp]
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        need(result.returncode == 0, f'dcmod extraction failed: {(result.stderr or result.stdout).strip()}')
        manifest = json.loads((Path(temp) / 'pack.json').read_text(encoding='utf-8'))
        report = summarize_manifest(manifest, temp)
    need(digest(pack) == before, 'input changed during inspection; discard this report')
    report.update(label=pack.name, source_sha256=before, source_bytes=pack.stat().st_size)
    return report


def assemble(packs):
    # Labels are caller-visible stable basenames; duplicates would make reports ambiguous.
    labels = [p['label'] for p in packs]
    need(len(set(labels)) == len(labels), 'input pack basenames must be unique')
    providers = defaultdict(list)
    for pack in packs:
        for item in pack['resources']:
            providers[item['id']].append(pack['label'])
    links = []
    unresolved = []
    for pack in packs:
        for item in pack['resources']:
            for dependency in item['dependencies']:
                link = {'pack': pack['label'], 'resource': item['id'], 'dependency': dependency,
                        'providers': providers.get(dependency, [])}
                links.append(link)
                if not link['providers']:
                    unresolved.append(link)
    return {'schema': 'dc-track-resource-evidence-1',
            'scope': 'metadata inspection only; no collision schema or runtime compatibility inferred',
            'packs': packs, 'dependency_links': links, 'unresolved_in_supplied_packs': unresolved,
            'duplicate_providers': {rid: labels for rid, labels in sorted(providers.items()) if len(labels) > 1}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packs', nargs='+', help='local extracted .rpk files, not @archive paths')
    parser.add_argument('--dcmod', required=True)
    parser.add_argument('-o', '--output', required=True, help='new JSON report file; existing paths are refused')
    args = parser.parse_args(argv)
    output = Path(args.output)
    try:
        need(not output.exists(), 'output already exists')
        dcmod = Path(args.dcmod).resolve()
        need(dcmod.is_file(), '--dcmod must name the built executable')
        report = assemble([inspect(path, dcmod) for path in args.packs])
        report['dcmod_sha256'] = digest(dcmod)
        output.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation also rejects a dangling symlink or a racing writer.
        with output.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2, sort_keys=True)
            stream.write('\n')
        print(f"Inspected {len(report['packs'])} packs; {len(report['unresolved_in_supplied_packs'])} dependencies unresolved in supplied set")
        return 0
    except (EvidenceError, OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as exc:
        print(f'track-evidence: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
