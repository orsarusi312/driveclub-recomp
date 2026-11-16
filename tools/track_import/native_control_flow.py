# SPDX-License-Identifier: GPL-2.0-or-later
"""Small static block walk inside an already validated code window.

No image access, execution, call following or inferred function names. Direct
branches are followed only within the supplied bytes; limits and unknown edges
are reported. A CALL's fallthrough is a syntactic possibility, not proof it returns.
"""


def walk(code, entry, decoder, instruction_limit=256, block_limit=32):
    import capstone as cs
    if not 0 < len(code) <= 1024 or not 0 < instruction_limit <= 256 or not 0 < block_limit <= 32:
        raise ValueError('invalid static block-walk bounds')
    pending, rows, occupied = [entry], {}, {}
    edges, calls, stops = [], [], []
    blocks = 0
    end = entry+len(code)

    def target_of(ins):
        return ins.operands[0].imm if ins.operands and ins.operands[0].type == cs.x86.X86_OP_IMM else None

    def edge(at, target, kind):
        edges.append({'from': hex(at), 'to': hex(target) if target is not None else None, 'kind': kind})

    def stop(at, reason):
        stops.append({'address': hex(at), 'reason': reason})

    while pending:
        at = pending.pop()
        if at in rows:
            continue
        if blocks == block_limit:
            stop(at, 'block-budget'); break
        blocks += 1
        while at < end:
            if at in rows:
                break
            if at in occupied:
                stop(at, 'branch-into-instruction'); break
            if len(rows) == instruction_limit:
                stop(at, 'instruction-budget'); pending.clear(); break
            ins = next(decoder.disasm(code[at-entry:at-entry+15], at, count=1), None)
            if ins is None:
                stop(at, 'undecodable-or-truncated'); break
            if any(byte in occupied for byte in range(at, at+ins.size)):
                stop(at, 'overlapping-instructions'); break
            rows[at] = {'address': hex(at), 'bytes': ins.bytes.hex(),
                        'mnemonic': ins.mnemonic, 'operands': ins.op_str}
            for byte in range(at, at+ins.size):
                occupied[byte] = at
            following = at+ins.size
            if ins.group(cs.CS_GRP_RET) or ins.group(cs.CS_GRP_IRET):
                edge(at, None, 'return'); break
            if ins.group(cs.CS_GRP_INT) or ins.mnemonic in ('ud2', 'hlt'):
                stop(at, 'trap-or-halt'); break
            if ins.group(cs.CS_GRP_CALL):
                target = target_of(ins)
                calls.append({'at': hex(at), 'target': hex(target) if target is not None else None,
                              'operands': ins.op_str, 'scope': 'not followed; return/execution unproven'})
            # Capstone 5 classifies LOOP* as branch-relative but not JUMP.
            if ins.group(cs.CS_GRP_JUMP) or ins.mnemonic in ('loop', 'loope', 'loopne'):
                target = target_of(ins)
                inside = target is not None and entry <= target < end
                edge(at, target, 'in-window-branch' if inside else 'external-or-indirect-branch')
                if inside:
                    pending.append(target)
                if ins.mnemonic in ('jmp', 'ljmp'):
                    break
                edge(at, following, 'conditional-fallthrough')
            at = following
        else:
            stop(at, 'window-end-fallthrough')
    return {'entry': hex(entry), 'window_bytes': len(code), 'instructions': [rows[a] for a in sorted(rows)],
            'decoded_bytes': len(occupied), 'blocks_started': blocks, 'edges': edges, 'calls': calls,
            'stops': stops, 'scope': 'bounded static paths; no execution or complete-function claim'}
