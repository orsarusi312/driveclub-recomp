#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Classify terminal two-signature MeshProbe logs; incomplete logs fail closed."""
import argparse
import json
import re
from pathlib import Path

RESULT = re.compile(r'MeshProbe: pair-result lane=(\d+) count=(96|186) reason=(matched|budget|deadline) outcome=([a-z-]+) attempts=(\d+) index-readable=(\d+) index-matches=(\d+) paired-matches=(\d+);')

def summarize(lines):
    armed, results, bindings, recorded = set(), {}, set(), set()
    errors = []; configured = False
    for line in lines:
        if 'MeshProbe: pair configured counts=96,186;' in line: configured=True
        match = re.search(r'MeshProbe: lane=(\d+) offline armed;',line)
        if match: armed.add(int(match[1]))
        match = re.search(r'MeshProbe: lane=(\d+) offline-(?:exact|rebased) draw-recorded .*?count=(96|186) ',line)
        if match: recorded.add((int(match[1]),int(match[2])))
        match = re.search(r'MeshProbe: lane=(\d+) offline paired-binding serial=\d+ count=(96|186) ',line)
        if match: bindings.add((int(match[1]),int(match[2])))
        match = RESULT.search(line)
        if match:
            lane,count=int(match[1]),int(match[2]); key=(lane,count)
            if key in results: errors.append('duplicate terminal result')
            reason,outcome=match[3],match[4]; attempts,readable,indices,paired=map(int,match.group(5,6,7,8))
            expected='matched' if paired else 'no-coverage' if not attempts else 'unreadable-or-unbound' if not readable else 'sampled-unmatched'
            if not 0<=paired<=indices<=readable<=attempts<=4096 or paired>1 or outcome!=expected:
                errors.append('inconsistent terminal counters')
            results[key]=dict(lane=lane,count=count,reason=reason,outcome=outcome,attempts=attempts,
                              index_readable=readable,index_matches=indices,paired_matches=paired)
        elif 'MeshProbe: pair-result' in line: errors.append('malformed terminal result')
        if len(armed)>256 or len(results)>512 or len(bindings)>512 or len(recorded)>512 or len(errors)>32:
            errors.append('log observation bound exceeded'); break
    if not configured or not armed: errors.append('pair not configured/armed')
    if set(results)!={(lane,count) for lane in armed for count in (96,186)}:
        errors.append('missing or unarmed terminal lane results')
    for lane in armed:
        pair=[results.get((lane,count)) for count in (96,186)]
        if not all(pair): continue
        reasons={r['reason'] for r in pair}; attempts=sum(r['attempts'] for r in pair)
        matches=sum(r['paired_matches'] for r in pair)
        if len(reasons)!=1 or attempts>4096 or matches>1:
            errors.append('inconsistent shared lane budget/result')
        if 'budget' in reasons and attempts!=4096: errors.append('budget not exhausted')
        if ('matched' in reasons)!=(matches==1): errors.append('match/stop reason disagrees')
        for r in pair:
            key=(lane,r['count'])
            if bool(r['paired_matches']) != (key in bindings and key in recorded):
                errors.append('paired match lacks agreeing recorded/binding evidence')
    rows=list(results.values())
    if errors: status='INCOMPLETE'
    elif any(r['paired_matches'] for r in rows): status='MATCHED'
    elif all(r['attempts']==0 for r in rows): status='NO_COVERAGE'
    elif all(r['reason']=='budget' for r in rows): status='BUDGET_EXHAUSTED'
    else: status='SAMPLED_UNMATCHED'
    return dict(status=status,errors=sorted(set(errors)),lanes=sorted(armed),results=rows,
                scope='direct recorded draw byte signatures only; no visibility, ownership, GPU completion or collision claim')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stderr',type=Path); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    with args.stderr.open(encoding='utf-8-sig',errors='replace') as stream: result=summarize(stream)
    with args.output.open('x',encoding='utf-8') as stream: json.dump(result,stream,indent=2); stream.write('\n')
    print(result['status'])
    if result['status']=='INCOMPLETE': raise SystemExit(2)

if __name__=='__main__': main()
