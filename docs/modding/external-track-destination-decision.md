# Final destination slot and one Road742 transfer test

Recovered clean local and remote `feat/external-track-foundation` at
`88c6604b12332c02d4d02d3538e6f24cbfa0ecf2`. No unfinished changes were present.
Read the complete supplied vertex-transfer.json, including every bounded body,
slot record and stop condition. Its image/patch hashes match the established
prepared inputs. The user reports 37 focused Windows tests passed.

## What is now established statically

The missing initialization edge is closed:

```text
0x800739c10 -> 0x800759140 -> 0x800742b70 -> 0x8007595d0 -> 0x8007730c0
                            object,reader    object,1,reader
```

The bridge calls the parser at 0x800742b85, then changes the vptr and updates a
reference-like field at +0x50. This is a syntactic call chain, not proof that the
Road742 instance executed it or that the allocations succeeded.

The two-byte helper 0x800760160 really requests two bytes and conditionally
reverses them according to reader+0x0c. Together with the already inspected
four/eight-byte helpers, this closes the width assumption in the serialized
layout comparison: ID at payload+8 -> object+0x18, u32 at +16 -> object+0x0c,
u16 at +20 -> flag nibble, u16 at +22 -> object+0x2c, u32 at +24 -> object+0x30.
Successful sequential parsing implies that the following read starts at
payload+28. No read-success/instance claim follows just from these instructions.

0x8007abd00 loads RSI from local[0], EDX from local.u32[8], and invokes the
reader's slot +0x10 at **0x8007abd15**. It then returns the reader pointer in RAX;
**its caller cannot use RAX as the read-success result**. Observe AL at
**0x8007abd18**, before `mov rax,rbx`, if inspecting the known boolean reader.
The caller supplies low32(object+0x2c * object+0x30) in local+8. The known native
reader path can therefore receive that product as an unsigned byte count.

Slot +0x30 resolves to 0x8007593e0. Its entire 19-byte body forwards:

```text
RDI = object; RSI = &local; EDX = 0; ECX = prior EDX = 0
R8D = prior ECX = object.u32_at_28; R9D = prior R8D = 1
tail jump through [object.vptr + 0x38]
```

The final exact slot is **0x8015ede88**, conditional on the stored address point
0x8015ede50 remaining unchanged. Its relocation record and target bytes are NOT
in the uploaded report. There is no prepared game executable in this workspace.
Its target and effects therefore cannot honestly be supplied here yet.

Slot +0x40 resolves to 0x800747f70: `object.u32_at_20 &= 0xffff0000; return`.
It is **not evidence of upload, unmapping or synchronization**.

0x800753bf0 selects a global-backed object, calls its +0x10 slot with the requested
size and second argument, retries after two helper calls if the result is zero,
then passes the result through 0x800744a90 and returns local storage. This supports
an allocation-related interpretation, but does not establish a pool, lifetime,
GPU ownership or that the returned value is identical to the initial slot result.
No further allocation call graph is needed for the proposed transfer observation.

## Packed descriptor: exact comparison, not a draw claim

For pointer P, stride S, count N, and input F, 0x800b4b2d0 writes four dwords:

```text
D0 = low32(P)
D1 = ((P >> 32) & 0xfff) | ((S & 0x3fff) << 16)
D2 = N
D3 = ((F >> 12) & 0xfff) | ((F << 4) & 0x7000)
     | (eligible ? ((F & 15) << 15) : 0) | 0x20000000
eligible = 0 < (F & 255) < 15 and (F & 0x800) == 0
```

At this call F=0x4401, so D3=0x2000c004. Comparing with
`gpu/shadps4/video_core/amdgpu/resource.h::Buffer`: stride occupies bits 48..61,
record count bits 64..95, swizzles bits 96..107, number/data format bits 108..114,
and memory type bits 123..125. These fields align. Type bits 126..127 are zero.
There is a material distinction: native packing preserves **44 address bits**,
where this runtime struct models **40** and treats bits 40..47 as padding. Do not
silently equate the full address encodings or change the runtime. For captured
addresses below 2^40 they agree. This identifies a buffer-descriptor-compatible
packing operation, not a submitted draw, resource lifetime or GPU residency.

## Decision: B, one instance-specific transfer observation

There is enough static evidence to stop expanding helper profiles. It is not
enough to change geometry integration: the known executed ID remains
0003d12ccbd11945, not Road742. An importer change now would be speculative.

The existing `--vertex-transfer` supports **`--destination-only`**: it verifies
the constructor store, original +0x30 call, exact parent relocation/target and
19-byte forwarding thunk, then resolves only +0x38 and walks at most 512 bytes,
256 instructions/32 block starts. No earlier helper windows or new call graph
are inspected. Missing/ambiguous/import/addend/patched metadata, code overlaps
and mapping failures remain blocked. Targets are never guessed.

This is the last offline lookup for this chain, not a new profile series. Run it
once locally. If blocked or the body has unresolved outgoing edges, retain those
limits; do not enlarge it. A blocked provenance guard prevents the runtime steps
below and is an explicit input/evidence blocker (decision C), not permission to
scan. Otherwise the single runtime experiment below directly checks destination
preparation regardless of whether every internal operation has a static name.

The test is **stock geometry only**. It must establish, in the same invocation:

1. Parsed object ID = 0003a8908986f62b, stride=16, count=35189, size=563024.
2. Live exact +0x38 target agrees with the guarded offline result.
3. The prepared local has a readable destination and count=563024.
4. The actual reader call returns on the same thread/stack; AL/status and stream
   position are recorded before the helper overwrites RAX.
5. A bounded post-call destination snapshot equals **all 563024 original vertex
   bytes**, and differs from its pre-call snapshot.

That would establish an instance-specific read into the captured destination,
subject to concurrent-writer limitations. It would not prove persistent ownership
or a draw. The next integration task would then trace that captured destination
and the object's +0x68 descriptor through existing BufferPath facilities, before
attempting a reversible visual marker. No new geometry replacement in this test.

## PowerShell: final exact-slot inspection, no rebuild

Reuse `$Py`, `$Image`, `$Patches` from the successful offline run. If opening a
new shell, restore them using the existing path/venv initialization in
[vertex-transfer instructions](external-track-vertex-transfer.md#powershell-offline-only),
but do not rerun its old inspection. Then:

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed' }
$Offline="$Repo\work\road742-destination-slot-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Offline | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Offline\commit.txt"
$Test=Start-Process $Py -ArgumentList @('-m','unittest','test_native_caller','test_native_control_flow','test_resource_consumer','test_vertex_transfer','-v') `
  -WorkingDirectory "$Repo\tools\track_import\tests" -Wait -PassThru `
  -RedirectStandardOutput "$Offline\tests-stdout.log" -RedirectStandardError "$Offline\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Tests failed' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--vertex-transfer','--destination-only',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Offline\destination-slot.json`"")
$Inspect=Start-Process $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Offline\inspect-stdout.log" -RedirectStandardError "$Offline\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw 'Guarded inspection failed; return logs' }
$Report=Get-Content "$Offline\destination-slot.json" -Raw | ConvertFrom-Json
if($Report.image_sha256 -ne '839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971' -or
   $Report.patch_sha256 -ne '6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761') { throw 'Different prepared inputs' }
if($Report.vertex_transfer.blocked) { throw $Report.vertex_transfer.blocked }
$SlotTarget=$Report.vertex_transfer.destination_slot.target
Write-Host "Expected live +0x38 target: $SlotTarget"
```

## Prepare the one runtime session

This is the existing preparation procedure, with a fresh experiment prefix and
an explicit stock-copy hash check. All game processes must be closed first.
It clones the documented known-good save and restores environment settings after
the debugger exits. Run in the same PowerShell session as the offline commands;
retain `$Offline` and `$SlotTarget`. No executable rebuild is needed.

Keep `BB_DMEM_MB=8056`, `BB_RPK_REQUEST=2`, `BB_RPK_DEBUG_REQUEST=1` and the existing
`BB_RPK_TRACE_FILE`. The existing host request stop is only an early arming point;
do not repeat its old eight-byte/caller/dispatch procedure. No copy/search/watch
or BufferPath observers are enabled. The original RPK and prior experiments stay
read-only. Stop on a stock-copy mismatch rather than replacing any archive.

```powershell
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
$Run="$Repo\work\road742-native-transfer-$([guid]::NewGuid().ToString('N'))"
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
  Write-Host "Follow the three-stop Road742 transfer steps. Evidence: $Run"
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

## WinDbg: three selected stops, one Forward load

Use the established exception setup (`sxd av`, `g`) at loader stops; do not change
memory protections or suppress second-chance failures. At the existing
`RpkRequest: STOP AFTER verified 8-byte copy`, inspect the following code only:

```text
.lastevent
bl
u 0x800759204 L10
u 0x8007abd00 L12
```

Require `0x80075920a: call [rax+30h]`, `0x80075921d: call 0x8007abd00`,
return site 0x800759222, and `0x8007abd15: call [rcx+10h]`, followed at 0x8007abd18
by `mov rax,rbx`. Require hardware slot 0 free. No historical heap addresses are
used. Arm the only ID filter (MASM numbers below are explicitly hex/decimal):

```text
r @$t19 = 0
ba0 e 1 0x80075920a "r @$t19 = @$t19+1; .if (@$t19 > 0n4096) { .echo STOP candidate budget; } .elsif (poi(@rbx+0x18) == 0x0003a8908986f62b) { .echo ROAD742 object selected; } .else { gc; }"
g
```

Stop after at most 4096 nonmatching candidates or five running minutes/one Forward
load, whichever comes first. Break manually on timeout. This counts execution
hits without per-hit logs; it is bounded but debugger filtering has overhead.
An unreadable condition, budget stop, unexpected exception, or no hit is a result,
not proof that Road742 is unused. Clean up slot 0 and finish without rearming.

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
Use the existing dcmod to verify the exact TOC span (not a new mesh scan):

```powershell
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

Return `destination-slot.json`, both inspection logs, test logs, current commit,
new runtime stdout/stderr/WinDbg, executable/input hashes, source-span.json,
byte-comparison.json and the original-save hash CSVs. **Do not return** snapshots,
game binaries, archives or saves. Missing hits/files/status must be reported as
missing, not inferred from normal termination.

## Validation and limits

107 full Python tests passed without skips/failures using existing dcmod and the
synthetic RPK fixture; 41 focused tests passed. New tests cover the single-method
read budget, parent/thunk provenance, blocked relocation variants, code overlap,
non-executable/out-of-range windows, unchanged inputs and modifier misuse.
The first local run caught a synthetic test's decoded-length expectation (11
instead of 12 bytes); corrected the assertion, without changing decoder behavior.
The documented byte comparator was also exercised locally with synthetic data:
changed/matching, already-matching, mismatching and short-dump cases behaved as
intended. The 19-byte forwarding guard matches the supplied report exactly.
No native code changed; no game build or new sanitizer claim is warranted.
Windows commands and the final real slot remain unverified here. There is no
claim of Road742 consumption, rendering, collision or drivable custom geometry.
