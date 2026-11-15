#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded, offline inspection of recorded return sites in a local BBPROBE6 image.

Format authority: launcher/prepare.cpp Link; src/probe.c main/apply_patches.
No process attachment, image writes, relocation application or function-name guesses.
Capstone is optional for the project, required only to run this standalone command.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

BASE = 0x800000000
RETURNS = (0x800591F1E, 0x80058BDF3, 0x8007AAFA2)
GUARDS = {0x800591E60: bytes.fromhex('554889e5'),
          0x800591F19: bytes.fromhex('e892c4ca00'),
          0x80058BDF0: bytes.fromhex('ff501884c0')}


def need(ok, message):
    if not ok:
        raise ValueError(message)


class Reader:
    def __init__(self, stream):
        self.stream = stream
        self.size = stream.seek(0, 2)
        stream.seek(0)

    def take(self, size):
        need(0 <= size <= self.size-self.stream.tell(), 'truncated metadata or image')
        data = self.stream.read(size)
        need(len(data) == size, 'short read')
        return data

    def skip(self, size):
        need(0 <= size <= self.size-self.stream.tell(), 'truncated table')
        self.stream.seek(size, 1)

    def u64(self):
        return struct.unpack('<Q', self.take(8))[0]


class Image:
    def __init__(self, stream):
        r = Reader(stream)
        need(r.take(8) == b'BBPROBE6', 'only prepared BBPROBE6 images are supported')
        self.size, entry, ns, nr, ni, flags = (r.u64() for _ in range(6))
        need(0 < self.size <= 512*1024*1024 and entry < self.size and
             0 < ns <= 64 and nr <= 1000000 and ni <= 100000 and flags <= 1,
             'image limits exceeded')
        r.skip(40)  # procparam, four main TLS values; not used for code decoding
        nm = r.u64()
        need(0 < nm <= 32, 'invalid module count')
        self.modules = []
        for _ in range(nm):
            fields = [r.u64() for _ in range(8)]
            name = r.take(32)
            at, size = fields[:2]
            need(b'\0' in name and fields[7] <= 1 and at < self.size and
                 0 < size <= self.size-at, 'invalid linked module span')
            self.modules.append((at, size, name.split(b'\0')[0].decode('ascii', errors='replace')))
        nb = r.u64()
        need(nb <= ni, 'invalid binding count')
        r.skip(nb*24)
        self.segments = []
        for _ in range(ns):
            at, size, flags = (r.u64() for _ in range(3))
            need(at <= self.size and size <= self.size-at and flags <= 7, 'invalid segment')
            self.segments.append((at, size, flags))
        r.skip(ni*128)
        self.relocations = []
        for _ in range(nr):
            at, kind, value, addend = (r.u64() for _ in range(4))
            need(self.mapped(at, 8) and kind <= 2 and (not kind or value < ni), 'invalid relocation')
            self.relocations.append(at)
        self.data_offset = stream.tell()
        need(r.size-self.data_offset == self.size, 'incorrect image length or trailing bytes')
        self.stream = stream
        self.patches = []

    def mapped(self, at, size, executable=False):
        return at >= 0 and size > 0 and sum(
            a <= at and size <= n-(at-a) and (not executable or flags & 1)
            for a, n, flags in self.segments) == 1

    def read(self, address, size, executable=False):
        at = address-BASE
        need(0 < size <= 1024 and self.mapped(at, size, executable), 'unmapped/ambiguous or excessive window')
        self.stream.seek(self.data_offset+at)
        data = self.stream.read(size)
        need(len(data) == size, 'image changed or short read')
        return data

    def overlaps(self, address, size):
        at = address-BASE
        return {'relocations': [hex(BASE+r) for r in self.relocations if r < at+size and at < r+8],
                'patches': [hex(BASE+p) for p, n in self.patches if p < at+size and at < p+n]}

    def code(self, address, size):
        overlap = self.overlaps(address, size)
        need(not any(overlap.values()), 'code window intersects unapplied patches/relocations: '+str(overlap))
        return self.read(address, size, True)

    def load_patches(self, stream):
        r = Reader(stream)
        need(r.size <= 16*1024*1024 and r.take(8) == b'BBPATCH2', 'invalid/oversized patch file')
        r.u64()  # patch pointer base; no patch bytes are applied by this tool
        count = r.u64()
        need(count <= 100000, 'patch count exceeds inspection limit')
        for _ in range(count):
            at, size = r.u64(), r.u64()
            need(0 < size <= 4096 and self.mapped(at, size), 'invalid patch range')
            self.patches.append((at, size))
            r.skip(size)
        need(stream.tell() == r.size, 'trailing patch bytes')


def disassembler():
    try:
        import capstone
    except ImportError as exc:
        raise ValueError('install capstone==5.0.6 in a dedicated Python venv; see native-caller-static guide') from exc
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    return decoder


def instructions(decoder, data, address):
    return [{'address': hex(i.address), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic,
             'operands': i.op_str} for i in decoder.disasm(data, address)]


def site_report(image, address, decoder):
    """Return boundary is supplied evidence; backward instruction starts are candidates."""
    import capstone
    result = {'return_address': hex(address), 'offset': hex(address-BASE),
              'function_name': None, 'function_boundary': 'not established',
              'scope': 'static bytes; no execution, vertex transfer or resource ownership claim'}
    try:
        window = image.code(address-256, 512)
    except ValueError as exc:
        return dict(result, status='blocked', reason=str(exc))
    result['status'] = 'decoded'
    result['forward_from_return'] = instructions(decoder, window[256:], address)
    result['forward_decoded_bytes'] = sum(len(bytes.fromhex(i['bytes'])) for i in result['forward_from_return'])
    result['call_ending_at_return_candidates'] = []
    targets = set()
    for size in range(2, 16):
        decoded = list(decoder.disasm(window[256-size:256], address-size))
        if len(decoded) != 1 or decoded[0].size != size or decoded[0].id != capstone.x86.X86_INS_CALL:
            continue
        ins = decoded[0]
        row = instructions(decoder, bytes(ins.bytes), ins.address)[0]
        row['boundary_status'] = 'candidate only; backward decoding is ambiguous'
        if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            target = ins.operands[0].imm
            row['direct_target'] = hex(target)
            targets.add(target)
        result['call_ending_at_return_candidates'].append(row)
    # Prologue bytes are leads, never inferred function starts or symbols.
    anchors = [i for i in range(253) if window[i:i+4] == b'\x55\x48\x89\xe5']
    result['prologue_candidates_truncated'] = len(anchors) > 4
    result['prologue_candidates'] = [
        {'address': hex(address-256+i), 'status': 'byte-pattern candidate, not a function boundary',
         'instructions': instructions(decoder, window[i:256+64], address-256+i)} for i in anchors[-4:]]
    result['direct_target_previews'] = []
    for target in sorted(targets)[:4]:
        row = {'address': hex(target)}
        try:
            row['instructions'] = instructions(decoder, image.code(target, 128), target)
        except ValueError as exc:
            row['blocked'] = str(exc)
        result['direct_target_previews'].append(row)
    return result


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def inspect(image_path, patch_path):
    before = [digest(image_path), digest(patch_path)]
    with image_path.open('rb') as stream, patch_path.open('rb') as patches:
        image = Image(stream)
        image.load_patches(patches)
        for address, expected in GUARDS.items():
            need(image.code(address, len(expected)) == expected,
                 'prepared code differs from recorded guard at '+hex(address))
        decoder = disassembler()
        report = {'schema': 'dc-native-caller-static-v1', 'image_sha256': before[0],
                  'patch_sha256': before[1], 'image_format': 'BBPROBE6', 'guest_base': hex(BASE),
                  'image_data_file_offset': image.data_offset,
                  'limits': 'three return windows, 512 bytes each; at most four 128-byte direct targets per site',
                  'caveat': 'linked bytes before runtime fixups; supplied patch/relocation overlaps blocked; guards do not prove whole-image runtime equivalence',
                  'sites': [site_report(image, address, decoder) for address in RETURNS]}
    need(before == [digest(image_path), digest(patch_path)], 'input changed during inspection; discard report')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True, type=Path)
    parser.add_argument('--patches', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        need(not args.output.exists(), 'output already exists; use a fresh report path')
        report = inspect(args.image, args.patches)
        with args.output.open('x', encoding='utf-8') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
    except (OSError, ValueError) as exc:
        print('native-caller-static: '+str(exc), file=sys.stderr)
        return 1
    print('native-caller-static: offline report saved; function identity and execution not inferred')
    return 0


if __name__ == '__main__':
    sys.exit(main())
