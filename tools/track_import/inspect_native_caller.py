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
# Target runtime: Windows x64 GetSystemInfo().dwPageSize, not PS4's 16 KiB page
# or Windows' 64 KiB allocation granularity. Used only for patch-range admission.
PATCH_PAGE_SIZE = 4096
RETURNS = (0x800591F1E, 0x80058BDF3, 0x8007AAFA2)
GUARDS = {0x800591E60: bytes.fromhex('554889e5'),
          0x800591F19: bytes.fromhex('e892c4ca00'),
          0x80058BDF0: bytes.fromhex('ff501884c0')}
READER_GUARDS = {
    0x8007aaf75: bytes.fromhex('488d05b48ce4004883c010488903'),
    0x8007aafa2: bytes.fromhex('817de442494720750b'),
    0x8007ab060: bytes.fromhex('4883c71889d2e9050ddeff'),
}
# Direct call encodings from the successful reader-details report. These anchor
# only the requested follow-up, not the semantics or runtime execution of a helper.
PAYLOAD_CALLS = ((0x80077c075, 0x8007ab640),
                 (0x80077c084, 0x8007601d0),
                 (0x80077c10c, 0x80077bec0))
# The returned payload-details report identifies these final failure-path calls.
# Do not assume they return into adjacent routines, or assume their callee is
# noreturn. End each reviewed window immediately after the guarded CALL instead.
PAYLOAD_END_CALLS = {0x8007ab640: 0x8007ab728,
                     0x8007601d0: 0x800760239,
                     0x80077bec0: 0x80077bfd8}
PAYLOAD_FAILURE_TARGET = 0x80123e420
CONSUMER_SLOT = 0x8015ed958
CONSUMER_TARGET = 0x800742db0
CONSUMER_HELPER_CALL = (0x800742de2, 0x800739c10)
CONSUMER_ID_RETURN_CANDIDATE = 0x8007730fa
CONSUMER_ID_HELPER = 0x800760240
VERTEX_PARSER_CALL = (0x800739c3e, 0x800759140)
VERTEX_BASE_PARSER = 0x8007730c0
VERTEX_BRIDGE_RETURN_CANDIDATE = 0x8007595ff
TRANSFER_VTABLE_LOAD = 0x80075916a
TRANSFER_VTABLE = 0x8015ede50
TRANSFER_VIRTUAL_SITES = ((0x80075920a, 0x30), (0x800759228, 0x40))
# Only direct dependencies visible in the returned vertex-parser report.
TRANSFER_CALLS = ((0x800759165, 0x800742b70, 512),
                  (0x8007591a8, 0x800753bf0, 512),
                  (0x80075921d, 0x8007abd00, 512),
                  (0x8007591ed, 0x800b4b2d0, 256),
                  (0x80075965a, 0x800760160, 128))


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
            need(at <= self.size and size <= self.size-at and flags <= 7 and
                 at % PATCH_PAGE_SIZE == 0, 'invalid segment')
            self.segments.append((at, size, flags))
        r.skip(ni*128)
        self.relocations = []
        self.relocation_records = []
        for _ in range(nr):
            at, kind, value, addend = (r.u64() for _ in range(4))
            need(self.mapped(at, 8) and kind <= 2 and (not kind or value < ni), 'invalid relocation')
            self.relocations.append(at)
            self.relocation_records.append((at, kind, value, addend))
        self.data_offset = stream.tell()
        need(r.size-self.data_offset == self.size, 'incorrect image length or trailing bytes')
        self.stream = stream
        self.patches = []

    def mapped(self, at, size, executable=False):
        return at >= 0 and size > 0 and sum(
            a <= at and size <= n-(at-a) and (not executable or flags & 1)
            for a, n, flags in self.segments) == 1

    def patchable(self, at, size):
        # src/probe.c patchable(): any single segment may contain the entire
        # patch, including its final page padding. Do not union adjacent ranges.
        # This does NOT make padding readable code or a valid relocation target.
        return at >= 0 and size > 0 and any(
            a <= at and size <= ((n+PATCH_PAGE_SIZE-1) & ~(PATCH_PAGE_SIZE-1))-(at-a)
            for a, n, _ in self.segments)

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
        for index in range(count):
            at, size = r.u64(), r.u64()
            need(0 < size <= 4096 and self.patchable(at, size),
                 f'invalid patch range: patch[{index}] offset={at:#x} '
                 f'guest-address={BASE+at:#x} size={size} ({size:#x}) '
                 f'page-size={PATCH_PAGE_SIZE}')
            self.patches.append((at, size))
            r.skip(size)
        need(stream.tell() == r.size, 'trailing patch bytes')

    def relative_function(self, address):
        """Interpret one kind-0 relocation as metadata, never patch the image.

        src/probe.c uses image_base + value for kind 0. Import kinds need runtime
        resolution and are deliberately unsupported here, as are patched slots.
        """
        raw = self.read(address, 8)
        need(not self.overlaps(address, 8)['patches'], 'pointer slot overlaps a patch')
        at = address-BASE
        records = [r for r in self.relocation_records if r[0] < at+8 and at < r[0]+8]
        need(len(records) == 1 and records[0][0] == at, 'missing/partial/ambiguous pointer relocation')
        _, kind, value, addend = records[0]
        need(kind == 0 and addend == 0, 'pointer needs unsupported import/addend resolution')
        need(self.mapped(value, 1, True), 'relative pointer target is not unambiguous executable code')
        return {'slot': hex(address), 'stored_bytes': raw.hex(), 'relocation_kind': kind,
                'image_relative_value': hex(value), 'target': hex(BASE+value),
                'scope': 'predicted from kind-0 relocation; not a live vtable observation'}


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


def direct_call_candidates(image, target, decoder, byte_budget=32*1024*1024, limit=8):
    """Bounded static E8 search. Pattern hits can be embedded data/other instructions.
    Scan only declared executable segments, never heap/GPU memory or asset data.
    """
    need(0 < byte_budget <= 32*1024*1024 and 0 < limit <= 8, 'invalid call-search budget')
    result = {'target': hex(target), 'scanned_bytes': 0, 'complete': False, 'candidates': [],
              'scope': 'E8 byte-pattern candidates, not verified instruction boundaries or executed calls'}
    for start, size, flags in sorted(image.segments):
        if not flags & 1:
            continue
        done, tail = 0, b''
        while done < size:
            left = byte_budget-result['scanned_bytes']
            if not left:
                result['stop'] = 'byte-budget'; return result
            n = min(1024, size-done, left)
            block = image.read(BASE+start+done, n, True)
            data, address = tail+block, BASE+start+done-len(tail)
            result['scanned_bytes'] += n
            pos = 0
            while True:
                pos = data.find(b'\xe8', pos)
                if pos < 0 or pos+5 > len(data):
                    break
                call = address+pos
                displacement = struct.unpack_from('<i', data, pos+1)[0]
                pos += 1
                if call+5+displacement != target:
                    continue
                row = {'call_candidate': hex(call), 'return_candidate': hex(call+5)}
                try:
                    # Strict code() still refuses patches/relocations. No inferred
                    # function entry or recursive call graph is constructed.
                    row['call'] = instructions(decoder, image.code(call, 5), call)
                    row['forward_from_return'] = instructions(decoder, image.code(call+5, 192), call+5)
                except ValueError as exc:
                    row['blocked'] = str(exc)
                result['candidates'].append(row)
                if len(result['candidates']) >= limit:
                    result['stop'] = 'candidate-budget'; return result
            tail = data[-4:]
            done += n
    result['complete'] = True
    result['stop'] = 'declared-executable-segments-exhausted'
    return result


def reader_details(image, decoder):
    # Evidence: uploaded report's RIP-relative LEA at 0x7aaf75, then add 0x10.
    # Only the observed object's first four slots are inspected; no table size claim.
    for address, expected in READER_GUARDS.items():
        need(image.code(address, len(expected)) == expected,
             'reader-details guard mismatch at '+hex(address))
    vtable = 0x8007aaf75+7+0xe48cb4+0x10
    result = {'schema': 'dc-reader-details-v1', 'vtable_address_point': hex(vtable),
              'scope': 'static initializer, reader helpers and callers; flag meanings and resource selection not inferred',
              'limits': 'three fixed code windows; four slots/256-byte previews; at most 32 MiB executable bytes and eight E8 candidates/192-byte continuations; no recursion',
              'windows': [], 'slots': []}
    for address, size in [(0x8007aaf40, 0x8d), (0x80058bc90, 0xe0), (0x8007ab060, 0xb)]:
        result['windows'].append({'address': hex(address),
                                  'instructions': instructions(decoder, image.code(address, size), address)})
    for offset in (0, 8, 0x10, 0x18):
        row = {'slot_offset': hex(offset)}
        try:
            pointer = image.relative_function(vtable+offset)
            row.update(pointer)
            target = int(pointer['target'], 16)
            row['instructions'] = instructions(decoder, image.code(target, 256), target)
        except ValueError as exc:
            row['blocked'] = str(exc)
        result['slots'].append(row)
    result['initializer_call_candidates'] = direct_call_candidates(image, 0x8007aaf40, decoder)
    return result


def payload_details(image, decoder):
    from native_control_flow import walk
    result = {'schema': 'dc-payload-details-v2',
              'scope': 'three exact helper targets; field meaning and target-resource consumption remain unproven',
              'limits': 'reviewed entry windows (at most 1024 bytes), 256 instructions and 32 blocks per helper; no call following or image search',
              'helpers': []}
    for call, target in PAYLOAD_CALLS:
        expected = b'\xe8'+struct.pack('<i', target-(call+5))
        need(image.code(call, 5) == expected, 'payload-details call guard mismatch at '+hex(call))
        row = {'call_evidence': hex(call), 'target': hex(target)}
        try:
            end_call = PAYLOAD_END_CALLS[target]
            size = end_call+5-target
            need(0 < size <= 1024, 'invalid reviewed helper window')
            expected_end = b'\xe8'+struct.pack('<i', PAYLOAD_FAILURE_TARGET-(end_call+5))
            need(image.code(end_call, 5) == expected_end,
                 'payload-details end-call guard mismatch at '+hex(end_call))
            row['boundary'] = {'end_exclusive': hex(end_call+5), 'last_call': hex(end_call),
                               'scope': 'reviewed entry window ends after failure-path call; callee return behavior unproven; adjacent routines excluded'}
            row.update(walk(image.code(target, size), target, decoder))
        except ValueError as exc:
            row['blocked'] = str(exc)
        result['helpers'].append(row)
    return result


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def dispatch_details(image, decoder, vtable, observed_target):
    """Check exactly the live-observed slot +8, never search for candidate tables."""
    import capstone as cs
    need(0 < vtable <= 2**64-16 and vtable % 8 == 0, 'invalid dispatch address point')
    need(0 < observed_target < 2**64, 'invalid observed dispatch target')
    pointer = image.relative_function(vtable+8)
    target = int(pointer['target'], 16)
    need(target == observed_target, 'dispatch target differs from linked slot: '
         f'observed={observed_target:#x} linked={target:#x}')
    result = {'schema': 'dc-observed-dispatch-v1', 'address_point': hex(vtable),
              'slot_offset': '0x8', 'observed_target': hex(observed_target),
              'linked_slot': pointer,
              'scope': 'user-supplied runtime addresses agree with one linked slot; no object identity, resource consumption or lifetime inference',
              'entry_scope': 'first basic block only; no call following or function-boundary claim',
              'instructions': []}
    # Refuse uncertain code as before, including overlaps after a short preview.
    data = image.code(target, 256)
    offset = 0
    while offset < len(data) and len(result['instructions']) < 64:
        ins = next(decoder.disasm(data[offset:offset+15], target+offset, count=1), None)
        if ins is None:
            result['stop'] = 'undecodable-or-truncated'; break
        result['instructions'].extend(instructions(decoder, bytes(ins.bytes), ins.address))
        offset += ins.size
        if any(ins.group(g) for g in (cs.CS_GRP_CALL, cs.CS_GRP_JUMP, cs.CS_GRP_RET,
                                     cs.CS_GRP_IRET, cs.CS_GRP_INT)) or \
                ins.mnemonic in ('loop', 'loope', 'loopne', 'ud2', 'hlt'):
            result['stop'] = 'first-control-transfer'; break
    else:
        result['stop'] = 'instruction-budget' if len(result['instructions']) == 64 else 'window-end'
    return result


def resource_consumer_details(image, decoder):
    from native_control_flow import walk
    slot = image.relative_function(CONSUMER_SLOT)
    need(int(slot['target'], 16) == CONSUMER_TARGET, 'confirmed consumer slot target mismatch')
    call, helper = CONSUMER_HELPER_CALL
    need(image.code(call, 5) == b'\xe8'+struct.pack('<i', helper-(call+5)),
         'confirmed consumer helper call mismatch at '+hex(call))
    result = {'schema': 'dc-resource-consumer-v1', 'linked_slot': slot,
              'scope': 'fixed confirmed dispatch and direct callee; ID-reader caller remains a static candidate; no Road742 consumption or output ownership claim',
              'limits': 'two entry windows (256/1024 bytes); one 384-byte candidate return window with at most two prologue leads; no image search or recursive call following',
              'reviewed_call_boundary': hex(PAYLOAD_FAILURE_TARGET),
              'boundary_caveat': 'stop at shared failure-path call without assigning a noreturn symbol',
              'windows': []}
    for target, size in ((CONSUMER_TARGET, 256), (helper, 1024)):
        row = {'target': hex(target)}
        try:
            row.update(walk(image.code(target, size), target, decoder,
                            stop_at_call_targets=(PAYLOAD_FAILURE_TARGET,)))
        except ValueError as exc:
            row['blocked'] = str(exc)
        result['windows'].append(row)
    result['id_reader_caller_candidate'] = bounded_call_candidate(
        image, decoder, CONSUMER_ID_RETURN_CANDIDATE, CONSUMER_ID_HELPER, 'ID-helper')
    return result


def bounded_call_candidate(image, decoder, ret, expected_target, label):
    from native_control_flow import walk
    candidate = {'return_candidate': hex(ret),
                 'scope': 'lead from raw kv stack display, not a verified frame return or executed call'}
    try:
        expected = b'\xe8'+struct.pack('<i', expected_target-ret)
        need(image.code(ret-5, 5) == expected, 'candidate is not the expected direct '+label+' call')
        data = image.code(ret-128, 384)
        candidate['call_encoding_matches'] = True
        candidate['forward_from_return'] = walk(data[128:], ret, decoder,
                                                stop_at_call_targets=(PAYLOAD_FAILURE_TARGET,))
        anchors = [i for i in range(124) if data[i:i+4] == b'\x55\x48\x89\xe5']
        candidate['prologue_leads_truncated'] = len(anchors) > 2
        candidate['prologue_leads'] = []
        for i in anchors[-2:]:
            lead = walk(data[i:], ret-128+i, decoder, instruction_limit=128, block_limit=16,
                        stop_at_call_targets=(PAYLOAD_FAILURE_TARGET,))
            lead['call_boundary_matches'] = any(r['address'] == hex(ret-5) and r['bytes'] == expected.hex()
                                                for r in lead['instructions'])
            lead['boundary_status'] = 'prologue byte-pattern lead only, not a proven function entry'
            candidate['prologue_leads'].append(lead)
    except ValueError as exc:
        candidate['blocked'] = str(exc)
    return candidate


def vertex_parser_details(image, decoder):
    from native_control_flow import walk
    call, target = VERTEX_PARSER_CALL
    need(image.code(call, 5) == b'\xe8'+struct.pack('<i', target-(call+5)),
         'vertex-parser caller guard mismatch at '+hex(call))
    result = {'schema': 'dc-vertex-parser-v1', 'call_evidence': hex(call), 'target': hex(target),
              'scope': 'static parser candidate for the observed selector-3 path; no Road742 consumption, allocation success or GPU claim',
              'limits': 'one 1024-byte entry window and one guarded 384-byte caller-lead window; no executable search or recursive call following',
              'entry': {'target': hex(target)}}
    try:
        result['entry'].update(walk(image.code(target, 1024), target, decoder,
                                    stop_at_call_targets=(PAYLOAD_FAILURE_TARGET,)))
    except ValueError as exc:
        result['entry']['blocked'] = str(exc)
    bridge = bounded_call_candidate(image, decoder, VERTEX_BRIDGE_RETURN_CANDIDATE,
                                    VERTEX_BASE_PARSER, 'base-parser')
    result['bridge_candidate'] = bridge
    calls = result['entry'].get('calls', [])
    result['direct_base_parser_calls'] = [c for c in calls if c['target'] == hex(VERTEX_BASE_PARSER)]
    result['calls_to_bridge_leads'] = [
        {'call': c, 'entry_candidate': lead['entry'], 'scope': 'static edge to a prologue lead; execution unproven'}
        for lead in bridge.get('prologue_leads', []) if lead['call_boundary_matches']
        for c in calls if c['target'] == lead['entry']]
    result['reviewed_call_boundary'] = hex(PAYLOAD_FAILURE_TARGET)
    result['boundary_caveat'] = 'walk stops at this failure-path call without assuming it cannot return'
    return result


def vertex_transfer_details(image, decoder):
    from native_control_flow import walk
    displacement = TRANSFER_VTABLE-0x10-(TRANSFER_VTABLE_LOAD+7)
    expected = b'\x48\x8d\x05'+struct.pack('<i', displacement)+bytes.fromhex('4883c010488903')
    need(image.code(TRANSFER_VTABLE_LOAD, len(expected)) == expected,
         'vertex-transfer vtable-store guard mismatch at '+hex(TRANSFER_VTABLE_LOAD))
    for call, target, size in TRANSFER_CALLS:
        need(image.code(call, 5) == b'\xe8'+struct.pack('<i', target-call-5),
             'vertex-transfer direct-call guard mismatch at '+hex(call))
    for call, offset in TRANSFER_VIRTUAL_SITES:
        need(image.code(call, 3) == bytes((0xff, 0x50, offset)),
             'vertex-transfer virtual-call guard mismatch at '+hex(call))
    result = {'schema': 'dc-vertex-transfer-v1', 'stored_address_point': hex(TRANSFER_VTABLE),
              'vtable_store_guard': hex(TRANSFER_VTABLE_LOAD),
              'scope': 'static slots conditional on the object retaining its stored vptr; no live dispatch, transfer, GPU ownership or Road742 claim',
              'limits': 'five fixed direct targets (512/512/512/256/128 bytes) and two exact slots (512-byte targets); no scans or recursive following',
              'direct_helpers': [], 'virtual_slots': [],
              'reviewed_call_boundary': hex(PAYLOAD_FAILURE_TARGET)}
    cache = {}
    def preview(target, size):
        key = (target, size)
        if key not in cache:
            try:
                cache[key] = walk(image.code(target, size), target, decoder,
                                  stop_at_call_targets=(PAYLOAD_FAILURE_TARGET,))
            except ValueError as exc:
                cache[key] = {'blocked': str(exc)}
        return cache[key]
    for call, target, size in TRANSFER_CALLS:
        result['direct_helpers'].append({'call_evidence': hex(call), 'target': hex(target),
                                         'code': preview(target, size)})
    for call, offset in TRANSFER_VIRTUAL_SITES:
        row = {'call_evidence': hex(call), 'offset': hex(offset)}
        try:
            row.update(image.relative_function(TRANSFER_VTABLE+offset))
            row['code'] = preview(int(row['target'], 16), 512)
        except ValueError as exc:
            row['blocked'] = str(exc)
        result['virtual_slots'].append(row)
    return result


def inspect(image_path, patch_path, include_reader_details=False, include_payload_details=False,
            dispatch_vtable=None, dispatch_target=None, include_resource_consumer=False,
            include_vertex_parser=False, include_vertex_transfer=False):
    need((dispatch_vtable is None) == (dispatch_target is None), 'supply dispatch vtable and target together')
    include_dispatch = dispatch_vtable is not None
    need(sum((include_reader_details, include_payload_details, include_dispatch, include_resource_consumer,
              include_vertex_parser, include_vertex_transfer)) <= 1,
         'select one follow-up profile')
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
                  'patch_page_size': PATCH_PAGE_SIZE,
                  'image_data_file_offset': image.data_offset,
                  'limits': 'three return windows, 512 bytes each; at most four 128-byte direct targets per site',
                  'caveat': 'linked bytes before runtime fixups; supplied patch/relocation overlaps blocked; guards do not prove whole-image runtime equivalence',
                  'sites': [] if include_payload_details or include_dispatch or include_resource_consumer or include_vertex_parser or include_vertex_transfer else [site_report(image, address, decoder) for address in RETURNS]}
        if include_reader_details:
            report['reader_details'] = reader_details(image, decoder)
        if include_payload_details:
            report['limits'] = 'three fixed helper windows only; prior return/vtable/caller search not repeated'
            report['payload_details'] = payload_details(image, decoder)
        if include_dispatch:
            report['limits'] = 'one supplied vtable slot +8 and a 256-byte/64-instruction first-block preview; no search'
            report['dispatch_details'] = dispatch_details(image, decoder, dispatch_vtable, dispatch_target)
        if include_resource_consumer:
            report['limits'] = 'confirmed resource consumer follow-up only; no earlier profiles or image searches repeated'
            report['resource_consumer'] = resource_consumer_details(image, decoder)
        if include_vertex_parser:
            report['limits'] = 'vertex-parser follow-up only; successful earlier profiles not repeated'
            report['vertex_parser'] = vertex_parser_details(image, decoder)
        if include_vertex_transfer:
            report['limits'] = 'vertex transfer dependencies only; no previous profiles or image searches repeated'
            report['vertex_transfer'] = vertex_transfer_details(image, decoder)
    need(before == [digest(image_path), digest(patch_path)], 'input changed during inspection; discard report')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True, type=Path)
    parser.add_argument('--patches', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    profile = parser.add_mutually_exclusive_group()
    profile.add_argument('--reader-details', action='store_true',
                        help='bounded static reader-vtable and initializer-caller follow-up')
    profile.add_argument('--payload-details', action='store_true',
                        help='inspect three known helper targets; do not repeat earlier searches')
    profile.add_argument('--dispatch-vtable', type=lambda value: int(value, 0),
                        help='address point captured at 0x80077bf2b; inspect only slot +8')
    profile.add_argument('--resource-consumer', action='store_true',
                        help='inspect confirmed 0x742db0 -> 0x739c10 and one ID-helper caller lead offline')
    profile.add_argument('--vertex-parser', action='store_true',
                        help='inspect only 0x759140 and one guarded lead toward the common header parser')
    profile.add_argument('--vertex-transfer', action='store_true',
                        help='resolve two stored-vtable slots and inspect five fixed buffer/read dependencies')
    parser.add_argument('--dispatch-target', type=lambda value: int(value, 0),
                        help='target observed in that same stop (required with --dispatch-vtable)')
    args = parser.parse_args()
    try:
        need(not args.output.exists(), 'output already exists; use a fresh report path')
        report = inspect(args.image, args.patches, args.reader_details, args.payload_details,
                         args.dispatch_vtable, args.dispatch_target, args.resource_consumer, args.vertex_parser,
                         args.vertex_transfer)
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
