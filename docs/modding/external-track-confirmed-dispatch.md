# Road742 investigation: the confirmed caller and its selected reader consumer

Continued clean local/remote `feat/external-track-foundation` at
`f1f39b2749eae042dcf71de97488bf9ebadde672`, matching uploaded commit.txt.
Read all three logs: stdout 1,361 lines, stderr 65 lines, WinDbg retry 104,081
lines. WinDbg contains 103,521 first-chance mentions and no second-chance mention;
those handled events are not new crash evidence. Stdout ends with
`Runtime: the process ended normally`. No exit-code file was supplied, so no
numeric process exit code is asserted or needed to interpret the captured edge.

## What was actually observed

The selected request copied eight verified bytes `EVOSLITL` from
`0xf0026a9340` to `0x7a7fdfd0`, return `0x800591f1e`, while the Road742 prefix
at `0xf0026af78f` still matched. Context file offset was 113877454, belonging to
vertex payload **0003d12ccbd11945**, not Road742's **0003a8908986f62b**.

The single execution breakpoint then stopped at `0x8007aafa2` on the same
`148c.62ac` / `async_1` thread. Captured frame and object values agree:

```text
initializer RBP       0x7a7fdff0
local [RBP-0x20]      0x7a7fdfd0 = the verified eight-byte destination
[RBP]                 0x7a7fe0a0 = saved caller frame
[RBP+8]               0x80077bd9b = actual initializer return
RBX = R13             0x7a7fe020 = caller frame - 0x80
reader vptr           0x8015f3c40
reader+0x18           0xf0026a9300 = underlying pointer in this snapshot
```

The call bytes at **0x80077bd96** target **0x8007aaf40**. Thus that path is now
tied to the request, rather than just being a static search candidate. The log
shows one `ba0`, its removal, an empty final breakpoint list and normal resume.
The path at 0x80077c05e and its skip/two-dword/slot-0x30 selection are **not this
initializer invocation**. This does not rule them out elsewhere in the game.

Read object fields at their actual widths: the byte at reader+0x20 is 1 and
byte +0x28 is 0; surrounding qword bytes include unclassified storage. Do not
interpret the entire displayed qwords as pointer/ownership flags. The underlying
pointer plus 0x40 agrees with the read base, but no allocation lifetime was proven.

## Confirmed continuation, statically reconstructed dispatch

The breakpoint was before the initializer returned; the following instructions
were disassembled, **not stepped or proven executed** in this log:

1. 0x80077bd9b loads `[caller_RBX+0x168]` and stores it at `[caller_RBP-0x6c]`,
   which is **reader+0x14**. The field's meaning remains unknown.
2. 0x80077bda4 zero-extends byte `[caller_R15+0x2a]` into R12D. This is a selector
   in 0..255, not the dword from the discarded 0x80077c05e hypothesis.
3. Before dispatch, 0x80077bdb2 calls slot +0x18 on `[caller_R15+0x30]` with ESI=0.
   Its effect is unknown. Do not assume the stream cannot change during that call.
4. 0x80077bdc9 calls **0x80077bec0** with RDI=caller_RBX (manager candidate),
   RSI=&local(-0x40) (output storage), EDX=selector, RCX=reader (caller R13).
5. The already supplied body at 0x80077bec0 saves manager in R12, selector in
   R14D, reader in R13. It performs two unresolved calls before selection, sets
   byte manager+0x11a, then selects:

   ```text
   table    = [manager + 0x48]
   selected = [table + zero_extend(selector) * 8]
   vtable   = [selected]
   target   = [vtable + 8]
   0x80077bf2b: call target
       RDI = &local(-0x38), RSI = selected, RDX = reader
   0x80077bf2e: continuation reads local(-0x38)
   ```

This is the strongest current consumer candidate: a specific indirect method
receives the initialized reader and output storage. The dispatcher's own body
then manages output/reference-count-like state; it does not itself read a vertex
array. The method target, actual selector, selected object and table pointer
were not captured. No static relocation can identify which heap object this
array selected. Scanning other tables or initializer callers would not close it.

The earliest missing edge is now **this selected method -> a read through this
reader**. A successful request after the magic would identify native resource
parsing, initially for 0003d12ccbd11945. It would not automatically identify
Road742 or a persistent vertex allocation. The long-term chain still requires
target-ID/span selection, original position transfer, allocation lifetime and
recorded draw evidence. No geometry, collision or rendering change is justified.

## Implemented offline cross-check for the actual target

The inspector accepts `--dispatch-vtable ADDRESS --dispatch-target ADDRESS`
**only as a pair**, mutually exclusive with the earlier profiles. Use addresses
captured together at 0x80077bf2b in the next observation; never historical guesses.
It examines only vtable+8 via existing kind-0 relocation validation, requires the
linked target to equal the supplied live target, then previews its first basic
block (at most 256 bytes/64 instructions). It stops at the first control transfer,
including calls; it does not treat an adjacent routine as a continuation.

No executable/table search, call following, runtime access, memory write, patch
application or guard relaxation is added. Missing/import/addend/ambiguous/patched
slots, unequal targets and patched/relocated/unmapped code fail closed. Runtime
addresses supplied by the user are evidence inputs, not independently verified
by this offline check. A match establishes linked slot agreement, not object
ownership or resource consumption. Dynamic tables/import thunks may remain
unsupported; return that refusal instead of broadening the scan.

## One necessary Windows observation: selected dispatch and its first read

No game rebuild, RPK preparation or driving test. Use **only the PowerShell
preparation/launch section** of [the selection-edge guide](external-track-selection-edge.md#powershell-reuse-preparation-and-the-working-executable),
which reuses the working executable/inputs and clones the known-good save. Change
its fresh-directory prefix from `road742-selection-edge-` to `road742-reader-dispatch-`.
Use the debugger procedure below instead of that guide's old breakpoints. It
already sets all required variables:

```text
BB_DMEM_MB=8056
BB_RPK_TRACE_FILE=<reused inputs/rpk-trace.txt>
BB_RPK_REQUEST=2
BB_RPK_DEBUG_REQUEST=1
BB_GPU_USER_DIR, BB_CONFIG, BB_TOGGLE_FILE -> new cloned user directory
BB_CRASH_DIR -> new logs directory
VK_LOADER_LAYERS_DISABLE=~implicit~
```

Only the clone is writable by the game. Existing BB variables are restored after
exit. Load Fraser Valley -> Road Circuit -> Forward. The known request stop is
an anchor for the new dispatch observation; **do not repeat the initializer
breakpoint, the +0x30 path, or the refill watch**. No data watchpoints are used.

At ordinary loader breaks use `sxd av`, then `g`, as before. At the existing
`RpkRequest: STOP AFTER verified 8-byte copy`, require destination and prefix
verification and caller-return=0x800591f1e. Record the current thread and source/
destination from that marker, not the addresses in this report:

```text
.lastevent
~.
bl
r @$t0 = <current verified eight-byte destination>
r @$t1 = <current read-start source>
r @$t6 = @$tid
u 0x80077bec0 L10
u 0x80077bf15 L8
ba0 e 1 0x80077bf2b
g
```

Slot 0 must be free. Verify the dispatcher prologue pushes RBP and then R15, and
the expected array-selection/call instructions match the static report. On the
**first** execution hit remove slot 0 immediately and collect:

```text
bc 0
.lastevent
~.
r
dq @rbp L2
dq @rbp-8 L1
r @$t19 = poi(@rbp)
r @$t2 = @rdx
r @$t3 = @rax
r @$t4 = poi(@rax+8)
r @$t5 = poi(@rdx+0x18)
r @$t12 = poi(@rbp-8)
r @$t13 = poi(@r12+0x48)
r @$t14 = @r14d
dq @$t12 L7
db @$t12+0x2a L1
dq @r12+0x48 L1
dq @$t13+@$t14*8 L1
dq @rsi L2
dq @$t3+8 L1
dq @$t2 L6
dq @$t5 L8
u @$t4 L20
```

Stop on unreadable or inconsistent data. Required checks:

* Same thread (`@$tid == $t6`), RIP=0x80077bf2b, `[RBP+8]=0x80077bdce`.
* RDX=R13=caller frame minus 0x80; original request destination equals caller
  frame minus 0xd0 (the confirmed invocation's stack layout). These checks tie
  the reader to this selected request, not just an instruction address.
* Saved R15 at `[RBP-8]` is the caller's descriptor; its byte +0x2a agrees with
  R14D, and `[table+selector*8] == RSI`, `[RSI] == RAX`.
* Reader vptr is 0x8015f3c40. Its +0x18 object has the known buffered-reader
  vptr 0x8015e0820, buffer field +0x30 equals `$t1`, capacity +0x38 is 65536.

The descriptor snapshot is 56 bytes, not an asset dump. Its field meanings are
unknown; even a matching resource ID in it would need interpretation. RAX is the
**address point** and `$t4` the **target** for the offline command below. `u L20`
is at most 32 instructions and does not identify a whole function.

Next observe either the first read through this exact reader or return from the
selected method. With slots 0 and 1 free:

```text
u 0x8007ab060 L3
ba0 e 1 0x8007ab060
ba1 e 1 0x80077bf2e
g
```

The thunk must show `add rdi,18h; mov edx,edx; jmp 0x80058bd70`. At the **first**
hit, immediately `bc 0` and `bc 1`. If slot 1 hits first, or thread/reader checks
fail, record `.lastevent`, `r`, `kv 8` and finish. This only means no matching
thunk request was observed in that interval; it does not rule out other data
access paths. Never repeatedly filter unrelated calls or keep rearming.

If slot 0 hits first on the same thread with RDI=$t2, record the request before
the thunk advances the object pointer:

```text
.lastevent
~.
r
r @$t7 = @rsi
r @$t9 = @edx
r @$t10 = poi(@rsp)
r @$t11 = @rsp
dq @$t5 L8
r @$t15 = poi(@$t5+0x10)
r @$t16 = poi(@$t5+0x18)
r @$t17 = poi(@$t5+0x28)
r @$t18 = poi(@$t5+0x20)
r @$t8 = poi(@$t5+0x30)+@$t17
? @$t9
? @$t15
db @$t1 L8
ub @$t10 L8
u @$t10 L8
```

For a byte comparison, require 1 <= N=$t9 <=64, nonzero/nonoverflowing destination,
source/destination nonoverlap, unchanged buffered object/vptr/buffer/capacity,
window start `$t16=113877454` (decimal), cursor <=65536-N, N<=available `$t18`
<=65536, and logical position `$t15 == $t16+$t17`. The read-start bytes must still
be EVOSLITL. On larger requests, refill/alias/layout changes, or a nonnative/
unreadable return, **stop with metadata**; do not broaden sampling or infer a copy.

Substitute the printed N in hex for `<N-hex>` below. Do not type angle brackets
or reuse a previous length. These are debugger reads only:

```text
db @$t8 L<N-hex>
db @$t7 L<N-hex>
ba0 e 1 @$t10
g
bc 0
.lastevent
~.
r
dq @$t5 L8
db @$t8 L<N-hex>
db @$t7 L<N-hex>
bl
g
```

At the return require same thread, RIP=$t10 and RSP=$t11+8. Compare the captured
source **before** the request with the destination **after** it; do not merely
compare two after-images. Record AL/read status without guessing success if it
fails. Require consistent buffer mapping and cursor/logical advance by N for a
buffered-transfer interpretation. A changed/mismatching sample or interrupted
call gives no transfer claim. Debugger snapshots cannot exclude every concurrent
writer or identical-content reuse; they do not prove ownership/persistence.

If the logical position was 113877462 and N=8, the known read-start resource ID
predicts `45 19 D1 CB 2C D1 03 00`. This is a **conditional expectation**, not a
reason to force a length, advance the cursor or label arbitrary bytes. No step
modifies game memory, calls native methods or forces GPU synchronization.

Maximum **three execution hits** after the existing host stop: dispatch, first
read, read return (or only two if the selected method returns first). Stop at any
mismatch, unexpected exception or first wrong-thread hit; no retries within this
run. If a hit does not arrive within one Forward load/five running minutes, break,
remove only slots installed here, record the timeout and exit normally. No driving
is needed. Keep normal exception handling; clear hardware breakpoints before
resuming/closing the game.

## Same experiment: offline confirmation of the captured slot

Only after a consistent dispatch stop, use the captured RAX and `[RAX+8]`. Reuse
the existing Capstone venv `$Py` from successful offline inspections. The launch
script already retains `$LaunchArgs`, `$PatchAt`, `$Run`, `$Repo`; do not rebuild.

```powershell
$Image=$LaunchArgs[1]
$Patches=$LaunchArgs[$PatchAt+1]
if(-not (Get-Variable Py -ErrorAction SilentlyContinue) -or -not $Py) {
  $Venv=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-static-caller-*' |
    Where-Object { Test-Path "$($_.FullName)\venv\Scripts\python.exe" } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if(-not $Venv) { throw 'Existing inspector venv missing; do not replace it' }
  $Py=Join-Path $Venv.FullName 'venv\Scripts\python.exe'
}
$Vtable=Read-Host 'RAX address point from the dispatch stop, as 0x... without WinDbg backticks'
$Target=Read-Host 'Target [RAX+8] from the same stop, as 0x... without backticks'
if($Vtable -notmatch '^0x[0-9a-fA-F]+$' -or $Target -notmatch '^0x[0-9a-fA-F]+$') { throw 'Invalid captured address syntax' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--image',"`"$Image`"",
  '--patches',"`"$Patches`"",'--dispatch-vtable',$Vtable,'--dispatch-target',$Target,
  '--output',"`"$Run\dispatch-target.json`"")
$Inspect=Start-Process $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { Write-Warning 'Offline slot refused; return its error without bypassing safeguards' }
```

Return new stdout/stderr/WinDbg logs, `dispatch-target.json` if produced, both
inspection logs, commit/hash/reused-experiment metadata and save-source hash CSVs.
If an exit-code file is missing, state that; retain normal-termination output
without inventing its numeric code. Do not return images, RPKs, executable or saves.

## Validation and limits

All **91 Python tests passed**, no skips/failures, using the existing dcmod binary
and synthetic RPK fixture for integration coverage. All 25 focused offline tests
also passed. Local final log: `work/native-static-validation/dispatch-final-tests.log`
(not committed). The new profile's synthetic tests check matching/mismatched
targets, malformed address points, missing/ambiguous/addend/patched relocation
records, code overlaps, first-control-transfer/64-instruction limits, profile
exclusivity, no prior search and unchanged inputs. Windows debugger execution and
real captured-slot integration are still untested here. Runtime code is unchanged.
