# Road742: arm before the first RPK open, not after the second read

**Historical experiment, now completed. Do not repeat it.** See
[the consumed-donor decision and guarded next action](external-track-consumed-donor.md).

Recovered clean local/remote `feat/external-track-foundation` at
`7e0dee6eec8af14c93ed7cba844135093f08536a`; no unfinished work was present.
Read all four supplied logs, including the complete 156,247-line WinDbg log.
No original asset, save or prior experiment was changed.

## What this run establishes

| Evidence | Observed order / meaning |
|---|---|
| stderr lines 51–53 | index header, then two Road742-position resource intersections, lengths 60048 and 63181 |
| stderr lines 54–56 | request observer selects the second window, verifies an eight-byte EVOSLITL copy, then pauses |
| WinDbg lines 34574–34628 | same host pause, instruction checks, counter initialized and conditional breakpoint installed |
| WinDbg lines 121831–121842 | counter `0x36a` = 874; breakpoint still installed, then explicitly removed |
| stdout line 1489 | runtime reports normal termination |

The 60048/63181 values are **resource-overlap lengths**, not necessarily complete
host read sizes: `rpk_trace_note()` prints the intersection returned by
`rpk_trace_overlap()`. Both intersections start at resource offset zero and verify
the prefix. This proves the bytes were observed before arming, **not** that native
vertex parsing had occurred. The read-start request remains a different payload.
Stderr ordering is direct evidence; stdout buffering prevents imposing a total
cross-stream chronology from line numbers alone. The user's Forward/race
observation corroborates that the game progressed; the logs contain no Road742
native-destination capture.

874 is the number of breakpoint command evaluations in that installed interval.
The ID condition never produced its selected marker and did not exhaust 4096.
There was no recorded condition-evaluation error. This does not establish 874
unique objects, all threads/paths/resources, or any coverage before installation.
The budget was not the problem demonstrated by this run. No negative conclusion
about LOD, culling, indirect draws or Road742 usage follows.

`object+0x18` remains a justified **static** ID filter: the reconstructed header
parser stores the eight-byte ID there; the constructor preserves its object in
RBX. We have not captured that field for a Road742 invocation at this site. The
new procedure records eight representative IDs/dimensions and one occurrence of
the previously observed ID `0003d12ccbd11945` as a control, without pausing for
those samples. A control match supports the filter at this site; it does not
prove Road742 uses the same path. The reported +0x38 target is `0x800759400`;
the four logs do not contain its offline body/relocation report, so no effects of
that target are invented. Verify its live slot during the next stop, without
another offline inspection round.

## Debugger cost and exception handling

WinDbg contains **155736 first-chance AV message occurrences**, **zero second-chance
messages**. Of these, 34150 precede arming, 87198 occur in the observation interval,
and 34388 follow the breakpoint removal region. Most are on thread 8478.7b2c;
individual fault addresses/instructions are not recorded by those notifications.
The log is 9,696,092 bytes. `sxd av` appears once at process context 1; the game is
later context 2. Do not assume an inherited filter without checking it there.

`src/probe.c::vectored_fault()` first offers AVs to `bbgpu_handle_fault()` for GPU
page tracking, then supports a speculative-read recovery path. The page manager
also installs write-fault handlers. These are legitimate reasons for recoverable
AVs, not attribution of every logged exception. The log cannot measure their
individual cost or distinguish them from every other recoverable fault. The 874
hardware hits also require debugger transitions even when `gc` resumes silently.

Keep `sxd av` in the actual game process and inspect `sx`: it preserves a stop on
unhandled second-chance AVs while allowing the application's handlers to run.
Do **not** use `gh`, change page protections, disable page tracking, or use
`sxi av`/`sxn av` as a blanket performance fix. Microsoft's
[exception-filter documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/controlling-exceptions-and-events)
distinguishes second-chance break from ignore/notify. This change does not remove
first-chance debugger overhead. Limiting the run to one load and three selected
stops avoids prolonged driving under the debugger. No speedup is claimed.

## Implemented: first matching host-open gate

`BB_RPK_DEBUG_OPEN=1` in a diagnostics build uses the existing validated
`BB_RPK_TRACE_FILE` manifest's **exact guest path**. At `do_open()`, after path
translation succeeds but **before host open and descriptor publication**, it
pauses once if a Windows debugger is attached. No runtime file-table/GPU lock is
held. A small diagnostic-only mutex serializes concurrent matching opens until
the selected pause returns; other paths do not enter that gate. This closes the
race where another opening thread could otherwise pass an atomic one-shot flag.

Default/OFF/invalid settings are inert. Missing/invalid manifests never arm. A
missing debugger consumes the stop rather than surprising a later attachment.
`errno` and Windows last-error are preserved. No native code patches, buffer reads,
protection changes or GPU synchronization are introduced. The pause intentionally
changes opening-thread timing. Only the first matching **attempt** is selected;
a later host-open failure does not rearm it. Normal failed-open behavior remains.

The guarantee covers this exact path through the runtime file API, including
future read/pread on its published handles. It is not a claim about a prior
resource loaded from another pack, an alias path, or an unrelated provider.
The marker must precede any matching RpkTrace resource observations. If it does
not, stop: the intended ordering has not been established.

## Single next experiment and exit criterion

One fresh run, stock RPK, cloned known-good save, no new mesh replacement.
Install the existing instance-specific breakpoint at **RpkOpenDebug STOP**, not
at RpkRequest. The old request/debug-read/copy/search observers remain unset.
Keep the 4096-candidate budget. Stop after one Forward load/five running minutes,
or the first failure/budget stop. Do not keep driving or raise the budget.

The required positive edge remains ID 0003a8908986f62b, stride 16, count 35189,
byte count 563024, a readable prepared destination, the correct same-invocation
reader return/status, and all destination bytes matching the immutable payload
with a changed before/after snapshot. That establishes native consumption only.

If the early gate is verified and this still does not match, the exact blocker is
that this constructor site has not been associated with Road742. If the known-ID
control matches, a generally broken +0x18 filter becomes less likely; alternate
provider/class/path or earlier loading through another source remain open. Stop
this Road742 breakpoint sequence. The alternative integration route is to use a
captured native object's resource ID and destination as the donor, confirm its
binding in existing BufferPath/MeshProbe, then make a bounds-preserving marker in
an isolated RPK. Native consumption alone is not sufficient to pick a visible
donor. Do not randomly replace further road-name meshes or implement collision.
If the control/first-eight samples are invalid, return those bounded samples;
fix the filter/site assumption before another game run.

## MSYS2 CLANG64: rebuild only for the new host arming boundary

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/early-transfer-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/test_resource_trace.sh 2>&1 | tee "$RunLog/native-tests.log"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer-tests.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit-tests.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## PowerShell: isolated executable, stock-copy inputs, cloned save

Close all game instances first. This reuses the working argument list/ASCII
junctions and existing DLLs, but copies the **new build** into the fresh experiment
instead of overwriting any previous executable. No offline inspector is rerun.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$SlotTarget='0x800759400'
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
$Pack="$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk"
$ModsAt=[array]::IndexOf($LaunchArgs,'--mods')
if($ModsAt -lt 0 -or $ModsAt+1 -ge $LaunchArgs.Count) { throw 'Missing mods argument' }
$StockCopy=Join-Path $LaunchArgs[$ModsAt+1] 'worlds\canada_road_circuit_02_n.rpk'
if((Get-FileHash -LiteralPath $Pack).Hash -ne (Get-FileHash -LiteralPath $StockCopy).Hash) { throw 'Not the stock-copy experiment' }
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
$Run="$Repo\work\road742-early-transfer-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
Get-ChildItem $KnownSave -Force | Copy-Item -Destination "$Run\user" -Recurse
if(Compare-Object $Before @(Get-SaveHashes "$Run\user") -Property Relative,Hash) { throw 'Save clone mismatch' }
$Before | Export-Csv "$Run\save-source-before.csv" -NoTypeInformation
$LaunchArgs[$UserAt+1]="$Run\user"
$LaunchArgs[$TimeoutAt+1]='900'
$RuntimeDir="$Run\runtime"
New-Item -ItemType Directory $RuntimeDir | Out-Null
$DllSource="$Repo\work\multistream-ea121055b3cd415e84ce2f65990c6063\runtime"
$Dlls=@(Get-ChildItem -LiteralPath $DllSource -File -Filter '*.dll')
if(-not $Dlls.Count) { throw 'Known runtime DLL directory is empty' }
$Dlls | Copy-Item -Destination $RuntimeDir
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.diagnostics.exe" -Destination $RuntimeDir
$LaunchArgs[0]=Join-Path $RuntimeDir 'DriveClubGame.diagnostics.exe'
if((Get-FileHash -LiteralPath $LaunchArgs[0]).Hash -ne (Get-FileHash "$Repo\out\DriveClubGame.diagnostics.exe").Hash) { throw 'New executable copy mismatch' }
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
  $env:BB_RPK_DEBUG_OPEN='1'
  $env:BB_GPU_USER_DIR="$Run\user"
  $env:BB_CONFIG="$Run\user\driveclub.ini"
  $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
  $env:BB_CRASH_DIR="$Run\logs"
  $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
  $DebugArgs="-o -logo `"$Run\logs\windbg.log`" `"$env:ComSpec`" /d /v:off /c $Launcher"
  $Process=Start-Process $Debugger -ArgumentList $DebugArgs -PassThru
  Write-Host "Wait for RpkOpenDebug STOP, then follow the early arming procedure. Evidence: $Run"
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

## WinDbg: arm at the new first-open stop

At loader stops continue as before. When the **game process** stops at:

```text
RpkOpenDebug: STOP before-first-host-open path=/app0/worlds/canada_road_circuit_02_n.rpk
```

collect the following. The Stop marker means path selection, not verified bytes:

```text
.lastevent
sxd av
sx
bl
u 0x800759204 L10
u 0x8007abd00 L12
dq 0x8015ede88 L1
u 0x800759400 L20
```

Require free hardware slot 0, the same previously verified call/return boundaries,
and live slot value 0x800759400. Keep the 32-instruction target preview bounded;
it does not prove the whole function. On mismatch stop, retain metadata, remove
only breakpoints installed here and finish. No static guards are bypassed.

Now install the bounded filter. The first eight candidates are logged for filter
calibration; at most one known-ID control is logged. These are observations, not
additional stops or rendering claims. Paste each command as one line:

```text
r @$t19 = 0
r @$t18 = 0
ba0 e 1 0x80075920a "r @$t19 = @$t19+1; .if (@$t19 > 0n4096) { .echo STOP candidate budget; } .else { .if (@$t19 <= 8) { .echo PARSER candidate ID then bytes stride count; dq @rbx+0x18 L1; dd @rbx+0x28 L3; }; .if (poi(@rbx+0x18) == 0x0003a8908986f62b) { .echo ROAD742 object selected; } .else { .if (poi(@rbx+0x18) == 0x0003d12ccbd11945) { .if (@$t18 == 0) { r @$t18 = 1; .echo PARSER known-ID control matched; }; }; gc; } }"
g
```

Load Fraser Valley -> Road Circuit -> Forward once. Stop on one load/five running
minutes, 4096 candidates, unreadable evaluation, or an unexpected exception. On
no match, break manually, record `.lastevent`, `r @$t19`, `r @$t18`, `bl`, then
`bc 0`, `g` and exit normally. Do not wait for a late RpkRequest stop (it is OFF).

If **ROAD742 object selected** fires, use the three selected-stop procedure below.
Do not rearm on a wrong-thread or wrong-stack stop. Runtime validation of this new
host boundary and WinDbg command remains pending on Windows.

At **ROAD742 object selected**, immediately remove the breakpoint and capture:

```text
bc 0
.lastevent
~.
r
r @$t0 = @rbx
r @$t1 = @$tid
r @$t2 = @rsp
r @$t3 = @rbp-0x40
r @$t4 = @r14
dq @$t0 Lf
dd @$t0+0x28 L3
dq poi(@$t0)+0x30 L3
u poi(poi(@$t0)+0x38) L20
dq @$t4 L6
```

Require RDI=$t0, RSI=$t3, EDX=0, ECX=563024 (0x89750), R8D=1,
RAX=[object]=0x8015ede50, ID at object+0x18 as above, and object+0x28/+0x2c/+0x30
= 563024/16/35189. Compare the live +0x38 target to `$SlotTarget` from offline
inspection. Capture +0x58/+0x60 pointers and +0x68 descriptor from `dq Lf`, but
do not require the destination to equal either pointer without observing it.
Abort on any mismatch. The code preview has a 32-instruction limit; do not follow
other calls or step through allocation internals.

Arm just the pre-transfer boundary:

```text
ba0 e 1 0x80075921d
g
bc 0
.lastevent
~.
r
dq @$t3 L2
dq @$t0 Lf
r @$t5 = poi(@$t3)
r @$t6 = dwo(@$t3+8)
r @$t8 = poi(@$t4+0x18)
dq @$t8 L8
db @$t5 L20
```

Require same thread=$t1, RSP=$t2, RDI=$t4, RSI=$t3, unchanged object ID/vptr,
count=$t6=0x89750, nonzero destination, and no overflow in destination+count.
Require the reader vptr 0x8015f3c40 and its slot +0x10 target 0x8007ab060:
`dq poi(@$t4)+0x10 L1`. If the reader differs, retain metadata and stop.
Record the live buffered object state, including logical position at +0x10.
Expected position before this read is **113879837** (payload start 113879809+28).
If the layout/position differs, do not assert a file-offset association.

Replace `RUN` below with the **new** experiment directory printed by PowerShell.
Write exactly 563024 bytes to fresh files; keep these private, never upload them:

```text
.writemem "RUN\destination-before.bin" @$t5 (@$t5+0x89750-1)
ba0 e 1 0x8007abd18
g
bc 0
.lastevent
~.
r
dq @rsp+0x18 L1
dq @$t8 L8
dq @$t0 Lf
.writemem "RUN\destination-after.bin" @$t5 (@$t5+0x89750-1)
bl
```

Require same thread and **RSP=$t2-0x20**, saved return `[RSP+0x18]=0x800759222`.
These follow the helper's call/push-rbp/push-rbx/push-rax prologue. AL is the
reader result before RAX is overwritten; require AL=1 for the known boolean
reader. Record the buffered logical position; expected after is **114442861**.
Refill/bypass can change buffer fields; do not require the old read buffer or
aliases to persist. If any check fails, record it and make no transfer claim.

On completion or any failure, `bl`, clear only slot 0 if still installed, `g`,
and exit the game normally; no driving required. Never `bc *` other breakpoints,
freeze threads, change native registers, write game memory, force GPU sync, or
keep watching for further resources. The debugger pseudo-registers are bookkeeping.
Snapshots cannot exclude all concurrent writers. An already identical before
snapshot or a mismatching after snapshot does not prove a new vertex transfer.

## Compare the private snapshots to the immutable original

After game/debugger exit, retain `$Run` from preparation and `$Pack` above.
Restore `$Py` to the existing inspector venv from the prior offline session
if needed (do not reinstall or rerun inspection). Use the existing dcmod to verify the exact TOC span (not a new mesh scan):

```powershell
if(-not (Get-Variable Py -ErrorAction SilentlyContinue) -or -not $Py) {
  $InspectorVenv=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-static-caller-*' |
    Where-Object { Test-Path "$($_.FullName)\venv\Scripts\python.exe" } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if(-not $InspectorVenv) { throw 'Existing inspector venv missing; do not replace it' }
  $Py=Join-Path $InspectorVenv.FullName 'venv\Scripts\python.exe'
}
$Dcmod="$Repo\out\dcmod.exe"
$Locate=Start-Process $Dcmod -ArgumentList @('rpk','locate',"`"$Pack`"",'113879809') -Wait -PassThru `
  -RedirectStandardOutput "$Run\source-span.json" -RedirectStandardError "$Run\source-span-stderr.log"
if($Locate.ExitCode -ne 0) { throw 'Source span lookup failed' }
$Span=(Get-Content "$Run\source-span.json" -Raw | ConvertFrom-Json).resource
if($Span.id -ne '0003a8908986f62b' -or $Span.size -ne 563052 -or
   $Span.offset_within_payload -ne 0 -or -not $Span.payload_id_matches_toc -or -not $Span.header_magic_valid) { throw 'Unexpected source span' }
@'
import hashlib, json, pathlib, struct, sys
pack, run = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
def digest(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
initial = digest(pack)
with pack.open('rb') as f:
    f.seek(113879809); header = f.read(28)
    if len(header)!=28 or header[:8]!=b'EVOSLITL' or struct.unpack_from('<Q',header,8)[0]!=0x0003a8908986f62b:
        raise SystemExit('source header mismatch')
    stride, count = struct.unpack_from('<HI',header,22)
    if (stride,count)!=(16,35189): raise SystemExit('source dimensions mismatch')
    source = f.read(stride*count)
if len(source)!=563024: raise SystemExit('short source')
snapshots=[]
for name in ('destination-before.bin','destination-after.bin'):
    path=run/name
    if path.stat().st_size!=len(source): raise SystemExit('wrong snapshot size: '+name)
    snapshots.append(path.read_bytes())
if initial!=digest(pack): raise SystemExit('source changed; discard comparison')
before,after=snapshots
result=dict(schema='dc-road742-byte-comparison-v1',resource='0003a8908986f62b',
    source_file_sha256=initial,source_data_offset=113879837,bytes=len(source),
    source_sha256=hashlib.sha256(source).hexdigest(),
    before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest(),
    before_equals_source=before==source,after_equals_source=after==source,
    destination_changed=before!=after,
    scope='byte equality only; corroborate same-call debugger identity/status; no ownership or draw claim')
with (run/'byte-comparison.json').open('x') as f: json.dump(result,f,indent=2)
print(json.dumps(result,indent=2))
'@ | Set-Content "$Run\compare-bytes.py" -Encoding UTF8
& $Py "$Run\compare-bytes.py" $Pack $Run
if($LASTEXITCODE -ne 0) { throw 'Byte comparison failed' }
```

Return build/native/importer/modkit logs, current commit, new runtime
stdout/stderr/WinDbg, diagnostic-lines.txt, executable/input hashes, source-span.json,
byte-comparison.json and the original-save hash CSVs. **Do not return** snapshots,
game binaries, archives or saves. Missing hits/files/status must be reported as
missing, not inferred from normal termination.

## Validation

* 107 Python importer/inspector tests: passed, no skips/failures.
* Existing modkit suite: passed, including synthetic RPK/model and ATRAC9 checks.
* Resource/native diagnostics suite: passed in OFF/ON modes, and again under
  AddressSanitizer + UndefinedBehaviorSanitizer.
* New first-open tests cover strict opt-in/path matching, missing debugger,
  single consumption, error preservation, OFF behavior and concurrent serialization.
* Changed runtime_file.c: Linux syntax check and Windows cross-compilation syntax
  checks passed with warnings as errors, diagnostics OFF and ON for Windows.
* Runtime memory regression suite: passed.
* New standalone Windows test executable cross-compiled successfully; not run here.
* Full Windows/Vulkan executable and gameplay cannot be run in this Linux workspace.
  No Windows integration, native Road742 transfer, rendering or collision success
  is claimed. Build and retest commands above are the required validation.
