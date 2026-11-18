# One controlled gameplay run: both verified half-float donors

## Recovered work and real evidence

Recovered `31601cf0b52310c81a01b439e4b96716c39db5fb`, matching the fork, no
stashes, and the unfinished two-signature change to `bbport_mesh_probe.h`.
Legacy tests had completed. Finished pair-specific tests, stock-copy preparation,
terminal evidence classification and this guide. No parser experiment is repeated.

Read the supplied 6662-byte `donor-pair.json`. It verifies:

| Count / target | Vertex span | Index range (half-open) | Mesh payload / draw offset |
|---|---|---|---|
| 96 / `00087c1471eba534_0_polySurfaceShape1222` | 0–27 | 211128–211224 | 121960202 / +427 |
| 186 / `0008877f16d366b9_0_pPlaneShape147` | 28–91 | 218007–218193 | 113879350 / +427 |

Both share position resource `0003d12ccbd11945` at file offset 113877454,
1132 bytes including the header, and index resource `00042bcd5555ffa6` at
108564953. The descriptor is complete: half XYZ occupies six sampled bytes,
stride is 12, packed normals start at +8. Both record lists are +66/+132/+195/+253
within their respective mesh payloads; matching offsets in different payloads
are not the same record or draw. There are no recognized signature aliases in
the hash-identified 2641-slot report. This remains offline evidence.

The signatures use vertex-first **0 and 28**, not index-first 211128 and 218007.
Index payload byte ranges are [422284,422476) and [436042,436414), including the
28-byte header displacement. The first index triples are 0,1,2 and 28,29,30.
A runtime prefix 0,1,2 for the second candidate is accepted only if **all sampled
indices** share delta -28 and the same draw's positions match at
`signed base_vertex + signature.first + delta`. No guessed file offset, float
conversion or zero-base diagnostic is promoted to a match.

## Completed implementation and bounds

`BB_MESH_PROBE_SECOND` adds exactly one second signature to the existing probe.
Counts must differ and a nonempty `BB_MESH_PROBE_TRIGGER` is mandatory. Invalid
pair configuration disables offline matching instead of falling back to one
candidate. Existing single-signature and positive-control behavior is preserved.

On each recording lane the two signatures share **4096 equal-count candidates**.
Each draw selects its signature by exact index count. Matching stops on that
lane at its first paired match, budget exhaustion, or 90 seconds after arming.
The deadline is checked on recording callbacks; an inactive/destroyed lane can
lack a terminal report, which is classified **INCOMPLETE**, not absence.
Each lane owns its state; no cross-thread sampling snapshot or GPU synchronization
was added. Other active lanes finish their own bounded intervals. Indirect draws
remain unchecked. This experiment does not enable the independent positive control.

Each closed lane prints two `pair-result` records, including attempts, readable
index samples, index matches, paired matches and stop reason. Missing index
bindings count toward the budget. No rearm is possible. Terminal lanes make no
more offline sample reads. The existing bounded detail logs and matched binding
addresses are retained; `paired-binding` now also identifies the index count.

Preparation reuses `donor-pair.json` and its **actual recorded input paths and
hashes**, including the refreshed full report and dependencies under
`work/visual-recovery`. It checks source bytes/ranges again without calling dcmod
or rescanning. Both signatures are written unchanged to separate input files.
The only game data written is a byte-identical RPK copy in the new experiment.

## MSYS2 CLANG64: update, test and build diagnostics

Close any game instances. Preserve a dirty worktree rather than resetting it.
Only this diagnostics executable needs rebuilding; dcmod is unchanged this turn.

```bash
cd /h/DC-Track-Test
set -euo pipefail
if [[ -n $(git status --porcelain) ]]; then echo 'Preserve local changes before updating'; exit 1; fi
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
BuildLog=$(mktemp -d work/pair-probe-build-XXXXXXXX)
bash tools/test_mesh_probe.sh 2>&1 | tee "$BuildLog/native-tests.log"
python3 -m unittest discover -s tools/track_import/tests -p test_pair_run.py -v
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$BuildLog/build.log"
git rev-parse HEAD | tee "$BuildLog/commit.txt"
```

## PowerShell: prepare and run once

Paste this whole block into one PowerShell session. It uses a fresh directory,
clones the known-good save, and reuses the previous successful literal launch
arguments and ASCII junctions. It never alters those previous experiment files.
All executables passed to `Start-Process -FilePath` are scalar strings. Argument
arrays are named explicitly, and path rejection uses case-sensitive matching.
Any failed gate is a stop; return the failure rather than substituting a donor.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Pair="$Repo\work\half-pair-refresh-95087acdb9b34266b0376225da539533\result\donor-pair.json"
$Run="$Repo\work\pair-draw-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
$PythonCommand=Get-Command python -CommandType Application | Select-Object -First 1
[string]$Python=$PythonCommand.Source
if([string]::IsNullOrWhiteSpace($Python)) { throw 'Python executable not found' }
$PrepareArguments=@("$Repo\tools\track_import\prepare_resource_trace.py",
  '--prepare-consumed-probe',$Pair,
  '--source',"$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk",
  '--output',"$Run\inputs")
$PrepareQuoted=($PrepareArguments | ForEach-Object { '"'+$_+'"' }) -join ' '
$Preparation=Start-Process -FilePath $Python -ArgumentList $PrepareQuoted -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput "$Run\prepare-stdout.log" -RedirectStandardError "$Run\prepare-stderr.log"
if($Preparation.ExitCode -ne 0) { Get-Content "$Run\prepare-stderr.log"; throw "Preparation failed: $Run. Do not launch." }
Copy-Item -LiteralPath $Pair -Destination "$Run\donor-pair.json"
$Prior=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-request-caller-*' |
  Where-Object {
    (Test-Path "$($_.FullName)\launch.cmd") -and
    (Test-Path "$($_.FullName)\inputs\rpk-trace.txt") -and
    (Test-Path "$($_.FullName)\logs\stderr.log") -and
    (Select-String -LiteralPath "$($_.FullName)\logs\stderr.log" -SimpleMatch 'RpkRequest: STOP AFTER verified 8-byte copy' -Quiet)
  } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(-not $Prior) { throw 'Successful request-caller preparation not found; do not regenerate or guess inputs' }
$Lines=@(Get-Content "$($Prior.FullName)\launch.cmd" | Where-Object { $_ -cmatch '--content-profile' })
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
  if($Arg -cmatch '["%\r\n\x80-\uFFFF]') { throw 'Existing ASCII launch paths required' }
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
  $env:BB_MESH_PROBE=(Get-Content "$Run\inputs\probe-96.txt" -Raw).Trim()
  $env:BB_MESH_PROBE_SECOND=(Get-Content "$Run\inputs\probe-186.txt" -Raw).Trim()
  $env:BB_MESH_PROBE_TRIGGER="$Run\arm-pair.flag"
  $env:BB_GPU_USER_DIR="$Run\user"
  $env:BB_CONFIG="$Run\user\driveclub.ini"
  $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
  $env:BB_CRASH_DIR="$Run\logs"
  $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
  $Process=Start-Process -FilePath ([string]$env:ComSpec) -ArgumentList "/d /v:off /c $Launcher" -PassThru
  Read-Host 'Load Fraser Valley > Road Circuit > Forward. When driving is available, press Enter HERE to arm'
  if($Process.HasExited) { throw 'Runtime exited before arming; return logs' }
  New-Item -ItemType File "$Run\arm-pair.flag" | Out-Null
  (Get-Date).ToUniversalTime().ToString('o') | Set-Content "$Run\armed-at.txt"
  Write-Host 'Drive with the game active for 100 seconds, then exit normally. No visible geometry change is expected.'
  $Until=(Get-Date).AddSeconds(100)
  while(-not $Process.HasExited -and (Get-Date) -lt $Until) { Start-Sleep -Seconds 1 }
  Write-Host 'Observation interval ended. Exit DriveClub normally now; preserve the logs even if no match appeared.'
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
& $Python "$Repo\tools\track_import\summarize_mesh_probe_pair.py" "$Run\logs\stderr.log" --output "$Run\pair-result.json"
$SummaryCode=$LASTEXITCODE
if($SummaryCode -ne 0) { Write-Host "INCOMPLETE or failed summary (exit $SummaryCode). Return all evidence; do not retry with another donor." }
Write-Host "Evidence saved (not a rendering-success claim): $Run"
```

The 900-second existing runtime timeout is a loading/exit safeguard, not a longer
sampling allowance. Keep the game active during the 100-second observation so
recording callbacks can close their lane deadlines. A new/paused lane lacking
terminal records is explicitly incomplete. Do not extend a failed run or repeat
retired Road742 breakpoints.

## Expected evidence and interpretation

Stderr must contain `pair configured counts=96,186`, then `offline armed` only
after the flag is created. Results are per lane:

- `reason=matched outcome=matched`: requires agreeing `offline-exact`/`offline-rebased
  draw-recorded` **and** `offline paired-binding ... count=96|186` lines.
- `reason=budget`: combined attempts on that lane total 4096. A candidate with
  zero attempts has no coverage even if the other consumed the budget.
- `reason=deadline outcome=no-coverage`: zero equal-count direct candidates.
- `outcome=sampled-unmatched`: index bytes were readable but no paired match.
- `outcome=unreadable-or-unbound`: counts were observed without readable indices.

The summarizer reports MATCHED, BUDGET_EXHAUSTED, NO_COVERAGE,
SAMPLED_UNMATCHED, or INCOMPLETE. A deadline with some sampling is not called
budget exhaustion. Missing lane endings, inconsistent counters or missing match
corroboration fail closed. INCOMPLETE can retain a positive lane while another
lane lacks an ending; return the raw evidence rather than interpreting it as
absence. The result concerns direct draws within this interval,
not an exhaustive statement about resources. A positive result is sampled byte
association, not exclusive ownership, GPU completion, visibility or collision.

Return `pair-result.json`, `donor-pair.json`, `inputs/preparation.json`, both
`inputs/probe-*.txt`, `commit.txt`, `hashes.txt`, `armed-at.txt`,
`prepare-stdout.log`, `prepare-stderr.log`, `logs/stdout.log`, `logs/stderr.log`,
`logs/exit-code.txt`, `diagnostic-lines.txt`, both save-hash CSVs and the build log.
A screenshot showing the route/camera at arming is useful context, not mesh proof.
Do not upload the copied RPK, executable, DLLs, saves or original game data.

## Decision after this one run

If one signature matches, use **that exact matched slot** for the existing
`visual_marker.py` inset experiment: unchanged topology/materials, positive
uniform factor 0.25 about its donor-local bounds center, isolated replacement
buffers, re-export verification, a stock control and a separate marker mod folder.
Do not change the neighbor sharing its buffers. This is a proposal pending the
actual matched count and binding evidence, not a prepared or visible marker.

If neither matches, the gap is now specifically between verified source
signatures and observed direct draw representations/coverage. Index matches
without position matches focus on format, stride, base vertex and repacking;
no equal-count coverage leaves indirect/batched/unselected paths unresolved.
None proves absence, LOD or culling. Stop at the reported gate; do not randomly
replace geometry or return to parser breakpoints. Collision remains separate.

## Validation in this change

- `tools/test_mesh_probe.sh`: legacy single/control/half tests plus eight pair
  scenarios in diagnostics ON/OFF builds passed. Pair scenarios cover either
  donor matching, rebasing, shared budget exhaustion, no coverage, sampled but
  unmatched deadline, invalid signature, duplicate counts, missing trigger,
  per-lane independence, no post-terminal reads and unchanged sample memory.
- The same native suite passed with ASan/UBSan.
- **122 Python tests passed, no skips**, including stock preparation without a
  rescan, changed-hash/claim/path refusal, copy equality, overwrite refusal and
  fail-closed terminal log classification.
- Resource-tracing regression suite and modkit/ATRAC9 tests passed.
- The summarizer also accepted the actual output of the synthetic native pair
  harness with two recording threads. This is synthetic evidence, not gameplay.
- Standalone pair harness cross-compiled for Windows x64, diagnostics ON/OFF.
  It was not executed on Windows; the full diagnostics game build and PowerShell
  workflow require the user's Windows installation.
- `git diff --check` passed. No original assets or saves were changed.
