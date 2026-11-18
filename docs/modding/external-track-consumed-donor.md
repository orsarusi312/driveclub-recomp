# Retire the Road742 parser breakpoint; test one consumed-resource signature

**The precheck returned two half-float slots. Do not use the launch procedure below.**
Continue with [offline half-float pair validation](external-track-half-donor-pair.md).
The original float-only gate and single-row assumption are historical.

## Recovery and decision

Recovered HEAD and fork at `29e3352370dd4f0bc4a61c0091efb2959dba9f92`,
no stashes, and an unfinished change to `bbport_mesh_probe.h`. Completed its
regression coverage and this procedure. No original archive, save or previous
experiment was modified. The earlier early-open instructions are historical;
**do not repeat either parser breakpoint experiment**.

No available evidence identifies an RPK mesh as a visible object. Consequently,
there is no justified donor for a cube, ramp or inset yet. Existing dcmod import,
isolated buffers and `visual_marker.py` already provide the reversible geometry
mechanism; another importer or random replacement would not close this gap.

The single next candidate is **0003d12ccbd11945**, chosen because its resource-ID
read was observed and the early-open constructor control matched it. It is not
Road742 and is not claimed to be road geometry. First require its existing mesh
report to map it to one compatible position-buffer slot; otherwise stop offline.
Then compare that slot's immutable signature against recorded direct draws.

## What the complete early-open logs establish

- stderr line 50 gates the first matching host open; line 51 resumes; the index
  header and two Road742 prefix observations follow at lines 52–54. Thus the
  earlier *late installation* explanation does not apply to this observation
  interval. The 60048/63181 values are resource intersections, not full read sizes.
- WinDbg records 934 command evaluations, a known-ID control match, and eight
  initial examples whose byte-size = stride × count. This supports the field
  filter at this constructor, not 934 unique resources or universal coverage.
- There was also a nonmatching first-chance single-step stop at evaluation 387:
  object ID `00037432d97c4be8`, stride 20, count 2034, bytes 40680. It was inspected
  and resumed. It was **not** a Road742 selection. This detail qualifies the
  abbreviated description of an entirely automatic breakpoint run.
- The breakpoint was finally cleared in game process 2 (the initial clear attempt
  in process 1 did not address the game breakpoint). stdout ends normally. There
  is no numeric exit-code file or captured Road742 destination transfer.
- 91499 first-chance AV notifications, no second-chance messages, are present.
  Their notifications do not identify fault sites or costs. Existing GPU tracking
  and speculative-read handlers can handle AVs; attributing every AV to either
  is unjustified. The next test uses no debugger, exception-filter changes,
  hardware breakpoints, or per-candidate debugger transitions.

The live `+0x38` slot resolves to `0x800759400`. Its bounded instruction preview
selects a pointer from `object + 0x58 + index*8` after calling `0x8007597c0`, then
stores it through the output pointer at `0x80075945b`. This supports destination
preparation in that static chain. The preview does not establish all selector
values, allocation lifetime, or execution for Road742. Alternate resource
providers/classes and unchecked render paths remain hypotheses.

## Why the existing rendering evidence is insufficient

MeshProbe's positive control validates sampling/replay of a direct recorded draw,
without an RPK identity. BufferPath validates actual buffer acquisition, arena
returns and queued uploads for a shader pair, without proving resource ownership
or visible pixels. The stride-44 observations are three pairs of attribute views,
not six identified road meshes. Resource dependencies and recognized mesh draw
records are useful offline references, not decoded scene/LOD associations.

The remaining edge is **a validated resource/mesh signature paired with an actual
recorded draw**. Even a positive match supplies byte-content association, not
exclusive ownership, GPU completion or pixel visibility. A source signature
could be shared by unlisted/external resources. Only after reviewing a positive
result should a bounds-preserving isolated marker test be selected.

## Implemented diagnostic change

`BB_MESH_PROBE_TRIGGER` optionally gates the existing offline signature matcher
until a file exists. Each recording lane stops after its first paired index and
position match or 4096 equal-count candidates, including missing-index candidates.
It cannot rearm. Before arming and after stopping it makes no offline guest-byte
reads or vertex-binding collection. Unequal counts consume no sample budget;
five-second counters remain so zero candidate coverage is visible. Trigger polling
is at most once a second per lane. Existing no-trigger behavior and independent
positive control are preserved. Diagnostics-OFF builds ignore the environment.

`offline paired-binding` prints the matched vertex/index binding, stride, format,
attribute offset, base vertex and recorded serial from already sampled state.
There are no guest writes, native patches, GPU waits, upload changes or forced
synchronization. Async recording uses the existing thread-local state; budgets
are per lane, not a process-wide 4096 bound. Indirect draws remain unchecked.

## One Windows action: guarded donor association

Run the following offline preparation first. **If it stops, return `selection.json`
and the error; do not select a different row, rescan broadly, or launch the game.**
The full existing report must be present; the uploaded Road742-only reports cannot
answer this mapping question. The 1132-byte span below includes the 28-byte vertex
header and comes from the already successful `read-start.json` inspection.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\consumed-donor-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
$Scan=Get-Content "$Repo\work\visual-recovery\mesh-scan.json" -Raw | ConvertFrom-Json
if($Scan.schema -ne 'dc-mesh-compatibility-1' -or
   $Scan.scanned_slots -ne $Scan.reported_slots -or
   $Scan.scanned_slots -ne @($Scan.slots).Count -or $Scan.scanned_slots -lt 1) {
  throw 'Need the existing complete report, not a filtered Road742 report; no launch'
}
$Rows=@($Scan.slots | Where-Object { $_.position_buffer -eq '0003d12ccbd11945' })
ConvertTo-Json -InputObject $Rows -Depth 30 | Set-Content "$Run\selection.json" -Encoding UTF8
$Candidates=@($Rows | Where-Object {
  $_.compatible -eq $true -and $_.draw_table_found -eq $true -and
  $_.skinned -eq $false -and $_.position_half_float -eq $false -and
  $_.draw_probe_signature -match '^v1:'
})
if($Candidates.Count -ne 1) { throw "Expected one compatible donor; found $($Candidates.Count). Return selection.json; no launch" }
$Donor=$Candidates[0]
if(@($Scan.slots | Where-Object { $_.draw_probe_signature -eq $Donor.draw_probe_signature }).Count -ne 1) {
  throw 'Signature is shared within the report; no unique donor association; no launch'
}
$Py='python'
$Dcmod="$Repo\out\dcmod.exe"
$Pack="$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk"
& $Py "$Repo\tools\track_import\prepare_resource_trace.py" --dcmod $Dcmod `
  --source $Pack --target $Donor.target `
  --dependency "$Repo\work\dependencies\global.rpk" `
  --dependency "$Repo\work\dependencies\worlds\canada_common.rpk" `
  --output "$Run\inputs"
if($LASTEXITCODE -ne 0) { throw 'Fresh exact-target validation failed; no launch' }
$Fresh=(Get-Content "$Run\inputs\mesh-scan.json" -Raw | ConvertFrom-Json).slots[0]
$Span=@($Fresh.source_file_spans | Where-Object { $_.id -eq '0003d12ccbd11945' })
if($Fresh.target -ne $Donor.target -or $Fresh.position_buffer -ne '0003d12ccbd11945' -or
   $Fresh.compatible -ne $true -or $Fresh.skinned -ne $false -or
   $Fresh.position_half_float -ne $false -or $Fresh.draw_table_found -ne $true -or
   $Fresh.draw_probe_signature -ne $Donor.draw_probe_signature -or
   $Span.Count -ne 1 -or $Span[0].offset -ne 113877454 -or $Span[0].size -ne 1132 -or
   28+[long]$Fresh.position_stride*[long]$Fresh.position_resource_vertices -ne 1132) {
  throw 'Fresh donor identity/layout differs from established evidence; no launch'
}
Write-Host "Validated one stock donor: $($Fresh.target). Keep this PowerShell session open: $Run"
```

Only if that succeeds, update and build in **MSYS2 CLANG64**. There is no new
parser breakpoint or offline inspector profile. Stop if the worktree is dirty;
merge only fast-forward, without resetting local work.

```bash
cd /h/DC-Track-Test
set -euo pipefail
if [[ -n $(git status --porcelain) ]]; then echo 'Preserve local changes before updating'; exit 1; fi
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
bash tools/test_mesh_probe.sh
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
```

Back in the **same PowerShell session**, reuse the previously successful literal
launch arguments, ASCII junctions and cloned known-good save. Keep all previous
experiments closed. This copies the new executable and existing DLLs into the
fresh directory. The RPK is still a byte-identical stock copy. It does not use
WinDbg, RpkTrace, MeshSearch, BufferPath, or the already-passed positive control.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
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

New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
Get-ChildItem $KnownSave -Force | Copy-Item -Destination "$Run\user" -Recurse
if(Compare-Object $Before @(Get-SaveHashes "$Run\user") -Property Relative,Hash) { throw 'Save clone mismatch' }
$Before | Export-Csv "$Run\save-source-before.csv" -NoTypeInformation
$LaunchArgs[$UserAt+1]="$Run\user"
$LaunchArgs[$TimeoutAt+1]='900'
$LaunchArgs[$ModsAt+1]="$Run\inputs\mods"
$RuntimeDir="$Run\runtime"
New-Item -ItemType Directory $RuntimeDir | Out-Null
$DllSource="$Repo\work\multistream-ea121055b3cd415e84ce2f65990c6063\runtime"
$Dlls=@(Get-ChildItem -LiteralPath $DllSource -File -Filter '*.dll')
if(-not $Dlls.Count) { throw 'Known runtime DLL directory is empty' }
$Dlls | Copy-Item -Destination $RuntimeDir
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.diagnostics.exe" -Destination $RuntimeDir
$LaunchArgs[0]=Join-Path $RuntimeDir 'DriveClubGame.diagnostics.exe'
if((Get-FileHash -LiteralPath $LaunchArgs[0]).Hash -ne (Get-FileHash "$Repo\out\DriveClubGame.diagnostics.exe").Hash) { throw 'New executable copy mismatch' }

foreach($Arg in ($LaunchArgs+@($RuntimeDir,$Run))) {
  if($Arg -match '["%\r\n\x80-\uFFFF]') { throw 'Existing ASCII launch paths required' }
}
$Quoted=($LaunchArgs | ForEach-Object { '"'+$_+'"' }) -join ' '
$Launcher="$Run\launch.cmd"
[IO.File]::WriteAllLines($Launcher,@('@echo off',"cd /d `"$RuntimeDir`"",
  "$Quoted 1>`"$Run\logs\stdout.log`" 2>`"$Run\logs\stderr.log`"",
  "echo %errorlevel% > `"$Run\logs\exit-code.txt`"",'exit /b'),[Text.Encoding]::ASCII)
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
Get-FileHash -LiteralPath $LaunchArgs[0],$LaunchArgs[1],$LaunchArgs[$PatchAt+1],$Pack |
  Format-List | Out-File "$Run\hashes.txt"
$PackBefore=(Get-FileHash -LiteralPath $Pack).Hash
$Saved=@{}; Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name]=$_.Value }
$OldLayers=$env:VK_LOADER_LAYERS_DISABLE
try {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  $env:BB_DMEM_MB='8056'
  $env:BB_MESH_PROBE=(Get-Content "$Run\inputs\stock-probe.txt" -Raw).Trim()
  $env:BB_MESH_PROBE_TRIGGER="$Run\arm-offline.flag"
  $env:BB_GPU_USER_DIR="$Run\user"
  $env:BB_CONFIG="$Run\user\driveclub.ini"
  $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
  $env:BB_CRASH_DIR="$Run\logs"
  $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
  $Process=Start-Process $env:ComSpec -ArgumentList "/d /v:off /c $Launcher" -PassThru
  Read-Host 'Load Fraser Valley > Road Circuit > Forward. Once driving is available, press Enter HERE to arm'
  if($Process.HasExited) { throw 'Game exited before arming; return logs' }
  New-Item -ItemType File "$Run\arm-offline.flag" | Out-Null
  Write-Host 'Observe at most 90 seconds. On paired-binding or budget completion, exit normally. No geometry change is expected.'
  $Process.WaitForExit()
} finally {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  foreach($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
  $env:VK_LOADER_LAYERS_DISABLE=$OldLayers
}
$After=@(Get-SaveHashes $KnownSave)
$After | Export-Csv "$Run\save-source-after.csv" -NoTypeInformation
if(Compare-Object $Before $After -Property Relative,Hash) { throw 'Original save hash changed' }
if((Get-FileHash -LiteralPath $Pack).Hash -ne $PackBefore) { throw 'Original RPK hash changed' }
Select-String -Path "$Run\logs\*.log" -Pattern 'MeshProbe:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
Write-Host "Evidence: $Run"
```

A second PowerShell window can follow the exact log path printed above with
`Get-Content -LiteralPath '<fresh Run>\logs\stderr.log' -Wait`. The 90-second
limit is the observation limit; the unchanged runtime timeout is a 900-second
loading/exit safeguard. Do not extend a negative run or substitute another mesh.
Exit normally; do not kill the known-good installation or copy this save back.

Expected markers on **stderr**:

- `offline waiting-for-trigger-file`, then `offline armed` after the file appears.
- `offline-status armed=1 attempts=N/4096 matched=... complete=...` every five
  seconds. Zero attempts means no equal-count direct candidate in that interval.
- Success: `offline-exact draw-recorded` or `offline-rebased draw-recorded` **and**
  `offline paired-binding`. These pair index-prefix and position-prefix content
  in one recorded draw. They do not prove the object contributed visible pixels.
- Stop: `offline final candidate`, then `complete=1`; no further offline samples
  on that lane. A matching final candidate may also emit the success markers.

Return `selection.json`, `inputs/preparation.json`, the small fresh
`inputs/mesh-scan.json`, `inputs/stock-probe.txt`, `commit.txt`, `hashes.txt`,
`logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`, `diagnostic-lines.txt`,
and save-hash CSVs. Include a screenshot and note the driving/camera context at
arming. Do not upload the RPK, executable, save or game textures.

If the offline gate fails, the exact blocker is no unique supported mesh mapping
for the **known consumed ID**; return the bounded selection evidence, not another
game run. If the signature does not match, the remaining gap is representation
or direct-draw coverage. Neither result justifies guessing a replacement. A
positive paired result permits planning the existing isolated inset-marker test;
visible confirmation is still required before expanding to Tsukuba or collision.

## Validation of this change

- `bash tools/test_mesh_probe.sh`: passed all four invocation modes in both
  diagnostics ON/OFF builds. Tests cover pre-trigger no-read behavior, unequal
  counts, the 4096 limit including missing bindings, failure on the final candidate,
  terminal paired match, no rearm, independent async recording lanes, unchanged
  source bytes, existing rebasing and positive-control replay.
- `SANITIZE=1 bash tools/test_mesh_probe.sh`: passed with ASan/UBSan.
- `bash tools/test_resource_trace.sh` and its `SANITIZE=1` run: passed, including
  existing BufferPath cross-translation-unit state and RPK observer regressions.
- Full importer suite with `DCMOD=out/dcmod` and the synthetic
  `TRACK_FIXTURE=work/track-fixture.rpk`: **107 tests passed, no skips**.
- `bash tools/modkit/test.sh`: all checks and ATRAC9 round trip passed.
- Standalone probe harness cross-compiled with LLVM-MinGW for Windows x64 in
  diagnostics ON/OFF modes, with warnings treated as errors. These binaries were
  **not executed on Windows**; this is not a full game build or runtime result.
- `git diff --check`: passed. PowerShell and gameplay require the user's Windows
  machine; neither was executed in this Linux workspace.

No new donor match, visible custom geometry, native collision or drivability is
claimed. The implementation is a bounded extension of the existing probe, not a
new scanner, parser profile, geometry replacement or rendering fix.
