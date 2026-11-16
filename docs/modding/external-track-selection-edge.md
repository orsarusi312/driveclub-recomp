# Road742: separate scalar parsing from dynamic stream selection

> Completed: the Windows run identified 0x80077bd96 as the actual caller. Continue
> with [its selected dispatch](external-track-confirmed-dispatch.md), not the old
> +0x30 hypothesis or another initializer-caller experiment. The reusable
> PowerShell launch section below remains applicable.

Recovered a clean local and remote branch at
`264823d52eee47ed76b925cfba6d3bbb44cd49c8`. There was no unfinished work to
discard. Read the complete supplied payload-details.json (119,568 bytes) and
tests-stderr.log (2,698 bytes; all 21 Windows tests passed). Both input hashes
match the prior prepared image/patch reports. No game, save or experiment changed.

## What the new bytes establish

These are static instruction semantics, not evidence these candidate callers ran
in the Road742 experiment. No official engine names are assigned.

* **0x8007ab640:** requests four bytes via reader slot +0x10, reverses their byte
  order when byte +0x0c is nonzero, then tests the result as a signed dword. If
  positive, repeatedly requests min(remaining,128) bytes into a local stack buffer.
  Incoming RSI is never used as an output; it is overwritten before the calls.
  This supports a length-prefixed discard operation, not an ID output or proven
  string decoder. Negative/zero counts cause no additional requests. Read-success
  results are not checked in this body, so these are requested advances, not proof
  that all requested bytes were returned. On successful reads the advance is
  `4 + max(signed_length,0)` bytes.
* **0x8007601d0:** requests four bytes, either directly into incoming RSI or into
  a local whose bytes it copies to that destination in reverse order. Field +0x0c
  therefore controls scalar byte swapping. The initializer sets that field for
  `BIG `; the semantics of its other field +0x08 remain unestablished. The helper
  does not establish signedness or semantic meaning of the output dword.
* **0x800760240:** adjacent eight-byte helper, not part of the four-byte routine.
* **0x80077bec0:** preserves incoming RCX as the reader argument, uses incoming
  EDX as an index into the pointer array at `[incoming_RDI+0x48]`, and calls the
  selected object's slot +8 at **0x80077bf2b**. RDI is a local output address,
  RSI the selected object and RDX the reader. It then manages the output with
  atomic pointer updates/reference-count-like operations. There is no direct
  vertex-array read in this body; the indirect callee can do work not visible here.
  At the specific call 0x80077c10c, EDX=1 selects the second array entry. This is
  **not** the earlier dword from the stream, and does not identify a vertex type.

## Exact selection chain in the candidate at 0x80077bfe0

`M` denotes incoming RDI (saved in R14), `P` incoming RSI (saved in R15), and
`F = [M+0x128]`. These are descriptive placeholders, not recovered class names.
The prologue and continuations are available in the returned report, even though
they are adjacent to, rather than part of, 0x80077bec0.

| Site | Arguments/data flow | What is known |
|---|---|---|
| 0x80077c04e | slot +0x30 of F; RDI=&local(-0x110), RSI=F, RDX=[P+8], ECX=0 | receives storage subsequently used as an adapter; the callee's writes are not observed |
| 0x80077c05e | RDI=&reader(-0x100), RSI=&local(-0x110) | initializes first reader and requests its eight-byte magic |
| 0x80077c075 | reader -> 0x8007ab640 | discard-shaped operation; not a write to supplied local(-0x118) |
| 0x80077c084 | reader -> 0x8007601d0, destination -0xd0 | first four-byte value; not used for the following selector in this slice |
| 0x80077c093 | same helper, destination -0xcc | second four-byte value, called X below |
| 0x80077c09b | first reader -> 0x8007ab020 | cleanup-shaped call before second selection |
| 0x80077c0c9 | same F slot +0x30; RDI=&local(-0x130), RSI=F, RDX=[P+8], ECX=X | X zero-extended to RCX; same identifier-like input, different fourth argument |
| 0x80077c0d9 | RDI=&reader(-0x100), RSI=&local(-0x130) | second reader initialized from the output **stack object**, not RAX |
| 0x80077c10c | RDI=M, RSI=&output(-0x120), EDX=1, RCX=&reader(-0x100) | leads to array-selected slot +8 at 0x80077bf2b |

The initializer's 0x80058bc90 helper copies a pointer and byte from that adapter
into reader+0x18/+0x20. Its read thunk eventually invokes the copied pointer's
slot +0x18. The actual pointer, class, stream span and cursor are not in the
static reports. The two slot +0x30 calls might return different streams, the same
stream repositioned, or another adapter; no allocation or ownership is proved.

In analogous continuations:

* 0x80077d5ec produces the dword loaded into ESI at 0x80077d5f9; EDX=0,
  RDI=R13, call 0x80058bd50 at 0x80077d604. The next initializer uses RSI=R13.
* 0x8007848e1 produces the dword loaded into ESI at 0x8007848ee; EDX=0,
  RDI=R12, call 0x80058bd50 at 0x8007848f6. The next initializer uses RSI=R12.

That forwarding wrapper calls the referenced object's slot +0x10. Prior runtime
evidence associates that slot on the buffered-reader class with a seek-like
method. It makes **position/offset** a stronger hypothesis for X than an arbitrary
array index, but it does not prove the class, origin convention or units here.
The manager's F+0x30 is a different interface; its slot number cannot be matched
to the previously observed stream's +0x30 by offset alone.

X is not the literal complete 64-bit Road742 resource ID. An indirect index or
offset could eventually select that resource; none is observed. `[P+8]` is later
passed to another helper and copied into output object state, but its name/type
cannot be established from those call sites alone.

## Why relocation metadata cannot yet resolve this slot

The call loads F from a dynamic field, then loads its vptr. There is no observed
store of a constant vptr to F in this slice. Relocation metadata can resolve a
**known table slot**, as it did for 0x8015f3c40; it cannot determine which table a
heap object uses. A scan of every table's +0x30 would produce ungrounded candidates.
No further executable or table scan is introduced.

More importantly, the actual saved return **out of** 0x8007aaf40 has never been
captured. 0x8007aafa2 is a return *inside* that initializer. The eight candidate
callers came from an incomplete static E8 search. None has been tied to the
verified read-start request. Continuing their caller graphs without that edge
risks investigating an unrelated parser. The verified EVOSLITL read belongs to
0003d12ccbd11945, not Road742; adjacent prefetched XYZ bytes do not close this gap.

A useful **conditional** check illustrates the risk: if this exact helper sequence
were applied immediately after that payload's EVOSLITL (with no intervening stream
change), the signed length would be the low ID word, 0xcbd11945 = -875488955.
The discard helper would therefore skip no extra bytes. The two subsequent
dwords would be the high ID word 0x0003d12c and the documented version 65536.
X would then be 65536, not a demonstrated resource selector. This arithmetic uses
the existing read-start.json header layout; it is **not** an observed execution
or evidence that the stream was actually positioned there in any candidate path.

## Inspector correction; no repeat inspection requested

The original block walk allowed syntactic fallthrough after calls, including
the shared failure-path call to 0x80123e420. Its nonreturning behavior is not
established. This let 0x800760240 and 0x80077bfe0 appear as possible continuations
and spent the latter window's instruction budget on adjacent code.

The payload profile now ends each reviewed window immediately after a guarded
failure-path CALL: 0x8007ab72d (exclusive), 0x80076023e, 0x80077bfdd. Sizes are
237, 110 and 285 bytes. It does not assign a noreturn symbol or pretend these
bounds prove complete function recovery. `window-end-fallthrough` explicitly
reports the unresolved return case; schema is now dc-payload-details-v2. Ordinary
calls remain listed, not followed. Mapping/patch/relocation checks still apply to
every inspected byte; adjacent bytes outside these smaller windows are not read.

Local replay from the supplied report's instruction bytes decoded 64, 37 and 81
instructions respectively, with no missing/conflicting bytes in those reviewed
windows. This is a report-byte replay, not a new inspection of the user's binary.
It suffices to validate the correction without another Windows offline run.

## Single next experiment: establish the actual initializer caller

Use the existing diagnostic executable and request observer; **no rebuild**, new
RPK scan, refill watch, geometry change or broad trace. The unresolved question is
whether this verified request reaches the proposed selection path at all, and,
only if it does, which live object supplies its slot +0x30 and what X contains.

Preparation and launch commands are in the next section. They reuse the previous
successful request-caller experiment's inputs/arguments, clone the documented
known-good save into a fresh directory, and capture new logs. Existing artifacts
remain read-only. Load Fraser Valley -> Road Circuit -> Forward.

At loader breaks use `sxd av` and `g` as in prior runs. At
`RpkRequest: STOP AFTER verified 8-byte copy`, check `destination=verified`,
`vertex-prefix-now=verified`, and `caller-return=0x800591f1e`. If any differs, stop
this procedure and return the logs. This marker is the prerequisite, not a new
vertex-transfer claim. Do not install data watchpoints.

1. Record `.lastevent`, `~.`, `r`, `bl`. Confirm slot 0 free and the initializer
   code matches the successful offline report:

   ```text
   u 0x8007aaf40 L6
   u 0x8007aafa2 L6
   r @$t6 = <destination address from this run's RpkRequest STOP>
   ba0 e 1 0x8007aafa2
   g
   ```

2. At the **first** execution hit, `bc 0` immediately. Check the same OS thread
   as the selected request. Capture:

   ```text
   .lastevent
   ~.
   r
   dq @rbp L2
   db @rbp-0x20 L8
   dq @rbx L6
   r @$t0 = poi(@rbp+8)
   r @$t1 = poi(@rbp)
   r @$t2 = @rbx
   ub @$t0 L8
   u @$t0 L20
   ```

   The prologue uses RBP and preserves the initializer's destination in RBX.
   Require `RBP-0x20 == $t6` and the eight local bytes to equal the verified copy's
   bytes; this ties the initializer to the selected request rather than merely
   finding the same instruction on the same thread.
   `[rbp+8]` now supplies the missing caller return. Do not treat deeper `kv`
   frames as proof. If unreadable, wrong thread, nonnative return, or unexpected
   code, stop. If return is **anything other than 0x80077c063**, the decisive
   result is the actual caller; **finish this experiment without more breakpoints**.
   In particular, 0x80077c0de would already be after the second initialization;
   do not try to infer its earlier selection. No negative rendering claim follows.

3. Only for return 0x80077c063, verify the current return follows the exact
   CALL at 0x80077c05e and that `$t2 == $t1-0x100`. Set:

   ```text
   u 0x80077c05e L2
   u 0x80077c0b9 L6
   ba0 e 1 0x80077c0c9
   g
   ```

   On the first hit, `bc 0`; check same thread and RBP=$t1. Stop on mismatch.
   The instruction must be `call qword ptr [rax+30h]`:

   ```text
   .lastevent
   ~.
   r
   dd @rbp-0xd0 L2
   dq @r14+0x128 L1
   dq @rsi L2
   dq @rax+0x30 L1
   r @$t3 = poi(@rax+0x30)
   r @$t4 = @rdi
   r @$t5 = @rcx
   u @$t3 L40
   ```

   Check RSI=[R14+0x128], RAX=[RSI], ECX=[RBP-0xcc], RDI=RBP-0x130;
   RDX is the other input and must be logged without assuming it is an ID or
   dereferencing it recursively. This records the real dispatch target and X.
   `u L40` is at most 64 instructions; do not expand it into a call graph.

4. One final breakpoint at `0x80077c0cc`, the instruction immediately after the
   virtual call. Clear it on its first hit, check the same thread/frame and collect:

   ```text
   ba0 e 1 0x80077c0cc
   g
   bc 0
   .lastevent
   ~.
   r
   dq @$t4 L2
   u @rip L10
   bl
   g
   ```

   The two qwords capture the output adapter's pointer/flag storage before the
   next initializer. They do not establish ownership, lifetime, stream cursor or
   byte transfer. No stepping into arbitrary native methods, invoking methods,
   patching code, changing protections or writing memory is permitted.

Maximum: one existing host stop and **three hardware execution hits**, often
only one. Do not rearm on wrong-thread hits. If a breakpoint is not reached within
one Forward load/five minutes after resume, break manually, `bc 0`, record the
timeout and resume/exit normally. On an unexpected exception capture `.lastevent`,
`r`, `kv 8`, clear only the breakpoint installed here and stop this procedure.
No historical heap/stack addresses are reused. Close the game normally and then
WinDbg so log collection completes.

This experiment either ties a real request to the candidate path and exposes its
dynamic target, or rules that path out **for this request** and supplies the actual
caller. It does not itself prove Road742 vertex consumption. A later positive
edge still requires the target ID/span and verified position bytes at the actual
consumer, followed by persistent allocation and recorded draw evidence.

## PowerShell: reuse preparation and the working executable

The existing request-caller launcher stores the working image, base/update
junctions and executable arguments. Reuse those literal arguments, replace only
the cloned user directory and timeout, and keep its inputs/mods read-only. All
game instances must be closed before copying the save. No build command is needed.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed' }
$Prior=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-request-caller-*' |
  Where-Object {
    (Test-Path "$($_.FullName)\launch.cmd") -and
    (Test-Path "$($_.FullName)\inputs\rpk-trace.txt") -and
    (Test-Path "$($_.FullName)\logs\stderr.log") -and
    (Select-String -LiteralPath "$($_.FullName)\logs\stderr.log" -SimpleMatch 'RpkRequest: STOP AFTER verified 8-byte copy' -Quiet)
  } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(-not $Prior) { throw 'Successful request-caller preparation not found; do not regenerate or guess inputs' }
$Lines=@(Get-Content "$($Prior.FullName)\launch.cmd" | Where-Object { $_ -match '--content-profile' })
if($Lines.Count -ne 1) { throw 'Unexpected existing launcher format' }
$Command=($Lines[0] -split ' 1>',2)[0]
$MatchesFound=[regex]::Matches($Command,'"([^"\r\n]*)"')
$LaunchArgs=@($MatchesFound | ForEach-Object { $_.Groups[1].Value })
if((($LaunchArgs | ForEach-Object { '"'+$_+'"' }) -join ' ') -cne $Command) { throw 'Launcher is not the expected quoted argument list' }
$UserAt=[array]::IndexOf($LaunchArgs,'--user')
$TimeoutAt=[array]::IndexOf($LaunchArgs,'--timeout')
$PatchAt=[array]::IndexOf($LaunchArgs,'--patches')
if($LaunchArgs.Count -lt 4 -or $UserAt -lt 2 -or $TimeoutAt -lt 2 -or $PatchAt -lt 2 -or
   $UserAt+1 -ge $LaunchArgs.Count -or $TimeoutAt+1 -ge $LaunchArgs.Count -or $PatchAt+1 -ge $LaunchArgs.Count) { throw 'Missing launch arguments' }
if(-not (Test-Path -LiteralPath $LaunchArgs[0] -PathType Leaf)) { throw 'Working executable missing' }
if((Get-FileHash -LiteralPath $LaunchArgs[1]).Hash.ToLowerInvariant() -ne '839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971') { throw 'Prepared image differs' }
if((Get-FileHash -LiteralPath $LaunchArgs[$PatchAt+1]).Hash.ToLowerInvariant() -ne '6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761') { throw 'Patches differ' }
$KnownSave="$Repo\work\road742-reader-consumer-123c703077484a289814451bdea22d5e\user"
if(-not (Test-Path -LiteralPath $KnownSave -PathType Container)) { throw 'Documented known-good save missing; stop rather than substitute a save' }
if(((Get-Item $KnownSave).Attributes -band [IO.FileAttributes]::ReparsePoint) -or
   @(Get-ChildItem $KnownSave -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Save links are not supported' }
if(-not @(Get-ChildItem $KnownSave -Recurse -Force -File -Filter profile.sav).Count) { throw 'Incomplete save tree' }
function Get-SaveHashes([string]$Root) {
  Get-ChildItem $Root -Recurse -Force -File | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{Relative=$_.FullName.Substring($Root.Length+1); Hash=(Get-FileHash -LiteralPath $_.FullName).Hash}
  }
}
$Before=@(Get-SaveHashes $KnownSave)
$Run="$Repo\work\road742-selection-edge-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
Get-ChildItem $KnownSave -Force | Copy-Item -Destination "$Run\user" -Recurse
if(Compare-Object $Before @(Get-SaveHashes "$Run\user") -Property Relative,Hash) { throw 'Save clone mismatch' }
$Before | Export-Csv "$Run\save-source-before.csv" -NoTypeInformation
$LaunchArgs[$UserAt+1]="$Run\user"
$LaunchArgs[$TimeoutAt+1]='900'
$RuntimeDir=Split-Path -Parent $LaunchArgs[0]
$Debugger=(Get-Command WinDbgX.exe -ErrorAction Stop).Source
foreach($Arg in ($LaunchArgs+@($RuntimeDir,$Run))) {
  if($Arg -match '["%\r\n\x80-\uFFFF]') { throw 'CMD launcher requires the existing ASCII paths without quotes or percent signs' }
}
$Quoted=($LaunchArgs | ForEach-Object { '"'+$_+'"' }) -join ' '
$Launcher="$Run\launch.cmd"
[IO.File]::WriteAllLines($Launcher,@('@echo off',"cd /d `"$RuntimeDir`"",
  "$Quoted 1>`"$Run\logs\stdout.log`" 2>`"$Run\logs\stderr.log`"",
  "echo %errorlevel% > `"$Run\logs\exit-code.txt`"",'exit /b'),[Text.Encoding]::ASCII)
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$Prior.FullName | Set-Content "$Run\reused-experiment.txt"
Get-FileHash -LiteralPath $LaunchArgs[0],$LaunchArgs[1],$LaunchArgs[$PatchAt+1],"$($Prior.FullName)\inputs\rpk-trace.txt" |
  Format-List | Out-File "$Run\hashes.txt"
$Saved=@{}; Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name]=$_.Value }
$OldLayers=$env:VK_LOADER_LAYERS_DISABLE
try {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  $env:BB_DMEM_MB='8056'
  $env:BB_RPK_TRACE_FILE="$($Prior.FullName)\inputs\rpk-trace.txt"
  $env:BB_RPK_REQUEST='2'
  $env:BB_RPK_DEBUG_REQUEST='1'
  $env:BB_GPU_USER_DIR="$Run\user"
  $env:BB_CONFIG="$Run\user\driveclub.ini"
  $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
  $env:BB_CRASH_DIR="$Run\logs"
  $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
  $DebugArgs="-o -logo `"$Run\logs\windbg.log`" `"$env:ComSpec`" /d /v:off /c $Launcher"
  $Process=Start-Process $Debugger -ArgumentList $DebugArgs -PassThru
  Write-Host "Follow the bounded selection-edge debugger steps. Evidence: $Run"
  $Process.WaitForExit()
} finally {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  foreach($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
  $env:VK_LOADER_LAYERS_DISABLE=$OldLayers
}
$After=@(Get-SaveHashes $KnownSave)
$After | Export-Csv "$Run\save-source-after.csv" -NoTypeInformation
if(Compare-Object $Before $After -Property Relative,Hash) { throw 'Original save hash changed' }
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkRequest:','RpkTrace:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
Write-Host "Return logs, commit.txt, hashes.txt, reused-experiment.txt, diagnostic-lines.txt and save hash CSVs from $Run"
```

Only the new user clone is writable by the game. Do not return saves, prepared
images, executable binaries or RPKs. `commit.txt` identifies the current analysis
tools; the executable hash identifies the reused diagnostics build, not a rebuilt
binary. The PowerShell/debugger integration remains untested here (no Windows).

## Validation

All **88 Python tests passed**, no skips/failures, with the existing dcmod binary
and original synthetic RPK fixture enabled for integration coverage. Focused
inspector/control-flow tests passed 22/22. Final local log:
`work/native-static-validation/selection-final-tests.log` (not committed).
New regression coverage excludes an adjacent patched routine while still refusing
patch/relocation overlaps inside the inspected window and mismatched end guards.
There are no native runtime changes, game builds or GPU synchronization changes.
