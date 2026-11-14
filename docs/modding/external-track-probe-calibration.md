# Calibrate the draw probe before interpreting zero donor matches

Follow-up: the user has now validated capture/replay during gameplay on this
implementation. Road742 still has zero matches. Continue with
[resource read tracing and bound-buffer search](external-track-resource-trace.md)
instead of repeating the calibration experiment below.

## Evidence from the three Windows runs

At `59130353c870a51e6bd43440048495aa49be84ea`, Stock/Control/Marker all launched
and used Fraser Valley Road Circuit Forward. The reported counters were:

| Counter | Stock | Control | Marker |
|---|---:|---:|---:|
| Direct draw attempts | 12,284,288 | 10,858,911 | 11,647,798 |
| Indirect unchecked | 434,626 | 363,585 | 387,247 |
| Index-prefix matches | 0 | 0 | 0 |
| Full recorded matches | 0 | 0 | 0 |
| Index unread OR mismatch | 8,942 | 8,007 | 8,634 |

The override loaded and the marker looked unchanged. **Stock never provided a
positive control**, so those zeros do not establish that Road742 is unused, nor
that LOD, culling or placement caused the absence of visible change.

The last counter in that version counted only equal-index-count candidates and
combined read failures with different bytes. Vertex checks ran only after exact
index equality. Consequently the old logs cannot tell which of those gates failed.

## Source investigation and changes

`liverpool.cpp` dispatches `DrawIndex2` with offset zero and `DrawIndexOffset2`
with the packet's **element offset**. `Rasterizer::ResolveIndexBuffer` computes
`index_base_address + index_offset * index_size`, obtains the cached GPU buffer,
and records the binding. The probe now receives that exact resolved guest address,
width, base and offset at this call site. It no longer duplicates the expression
or reads indices at the beginning of `DrawRecord`, before bindings are resolved.
The CPU backing sample is taken after the draw reaches command recording.

`GraphicsPipeline::GetVertexInputs` derives format, stride and input rate from
fetch-shader attributes. An attribute has its own descriptor address; it is not
necessarily the start of the RPK position resource. The prepared-input path uses
corresponding cached attributes/bindings. The probe now stores those actual
formats, offsets, locations and rates, with a hard cap of 32 bindings. It samples
float/half XYZ only for Vulkan R32G32B32/R32G32B32A32 or R16G16B16/R16G16B16A16
**Sfloat**, per-vertex attributes. A float-shaped attribute is not automatically
identified as a position semantic. Unsupported layouts are reported, not guessed.

Both the ordinary and async draw-recording paths reach `DrawRecord`, vertex/index
resolution, and the recorded callback on the owning thread. State is thread-local
and reset at every direct or indirect attempt. Logs include a `lane` identifier;
counts are cumulative **per lane**, so do not blindly sum successive progress lines.

Offline matching remains compatible with existing `v1:` signatures:

* Count mismatch, missing index binding, invalid width/range, failed reads and
  unequal prefixes have separate counters.
* Constant index rebasing is reported separately. All sampled indices must differ
  by the same signed delta. The position lookup then uses `offline_first + delta +
  signed_base_vertex`. Reordering, changed topology or count changes are not treated
  as a match. `offline-rebased` is not an exact byte identity of the index stream.
* Position checking also runs for equal-count candidates whose index prefix fails.
  `position-unpaired` is evidence of matching position bytes only; it cannot promote
  a draw to a full match.
* Stride, format/rate, negative computed base vertex, descriptor/overflow range,
  failed vertex reads and position-prefix differences are separate counters.
* A bounded base-zero comparison reports `base-zero-only` if ignoring a nonzero
  base vertex would match. This is a diagnostic hypothesis, **never** accepted as
  a draw match, and never changes the draw's actual base vertex.

Host Vulkan buffer offsets are cache allocations, not RPK offsets. The probe reads
`runtime_memory_read_backing`, using guest descriptors, not host buffer handles.
This avoids changing GPU tracking protections. It does **not** establish equality
with GPU-written/stale data in a Vulkan allocation. GPU-generated/indirect data,
shader vertex pulling, geometry repacking, primitive conversion, split/batched
counts and later resource providers can still evade the offline signature.
World/view transforms normally act downstream of raw index fetch; transforms alone
would not alter this comparison if the same indexed geometry was selected. They
can affect selection/culling upstream, which remains undecoded here.

No renderer behavior, synchronization, buffer content, archive or physics changes
are made. No cause of the unchanged road has yet been established.

## Positive control: an actual recorded draw, then a later fresh replay

Set `BB_MESH_PROBE_CONTROL` to a **new trigger-file path**, not to `1`. The runtime
polls for this file at most once per second per recording lane. Create it only
when driving. The probe does not write, delete or consume the file.

After arming, each lane captures at most one readable direct indexed draw with
at least 12 indices, a supported per-vertex XYZ-shaped attribute, and three distinct
referenced indices in the first 24. It saves at most 24 decoded indices and three
finite XYZ byte samples **in memory**, along with shader hashes and binding layout.
Capture happens only for a draw reaching the recorded callback.

A **later** recorded draw must reproduce the count, shader hashes, layout, index
prefix and three referenced XYZ samples using fresh reads of its current bindings.
It may use different buffer addresses. The capture draw cannot validate itself.
Expect these two distinct messages:

```
MeshProbe: lane=... positive-control captured serial=...
MeshProbe: lane=... positive-control REPLAY-MATCH capture=... replay=...
```

The replay validates the diagnostic's CPU backing address/format path for that
actual draw. It is not proof of Road742 identity, GPU completion or visible pixels.
The selected attribute/draw may be a car, scenery or another pass, not a road.
A maximum of 4096 candidate draws are sampled per lane. Matching stops on success
or exhaustion; exhaustion is explicit. A dynamic first sample may never repeat;
restart with a fresh trigger rather than assuming the reader is wrong.

This control works **without `BB_MESH_PROBE`**. It is independent of donor count and
prefix gates. Use both variables in the first run to calibrate and investigate the
stock signature simultaneously.

## Bounded logging and interpretation

Per lane: six index-candidate examples, six vertex examples, ten exact and ten
rebased full-match messages, one control capture/replay, and progress once per
five seconds. Reads are capped at 24 indices and eight XYZ records per binding
for offline checks (plus the bounded base-zero diagnostic), and three referenced
XYZ records per binding for the control. No forced GPU downloads, waits or uploads.
This is opt-in diagnostic overhead, not a benchmark configuration.

Numeric `read-status` / vertex `status` values in bounded examples:

| Value | Meaning |
|---:|---|
| 0 | Read/position check succeeded; consult `index-match` separately for index equality |
| 1 | Stride unsupported or mismatched |
| 2 | Format or input rate unsupported/mismatched |
| 3 | Negative effective vertex index |
| 4 | Bounds/overflow rejection |
| 5 | Backing read failed |
| 6 | Position bytes differ |
| 7 | Index width unsupported |

Progress counters for index stages are per **recorded indexed draw**. Vertex
failure counters are per inspected **binding**; several can increment for one draw.
`direct` and `indirect-unchecked` count attempts, including ones filtered later.
`no-bindings` is a coverage gap; it can indicate vertex pulling but does not prove it.
A positive control followed by donor zeros validates only the control's layout.

## Windows build — MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
mkdir -p work
bash tools/test_mesh_probe.sh 2>&1 | tee work/probe-calibration-tests.log
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
git rev-parse HEAD
```

The standalone tests exercise both diagnostics ON/OFF, with and without an offline
signature. They require no game assets. The full runtime build must succeed before
copying its new executable.

## Next run — PowerShell, fresh stock baseline

Keep the existing three experiments untouched. Use their **stock-probe.txt**, the
same prepared files and Base/Update directories. No new marker generation is needed.
This run uses no mod directory or benchmark.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Previous=Read-Host 'Previous visual-marker experiment folder containing stock-probe.txt'
$RuntimeDir=Read-Host 'Working runtime folder containing its DLLs'
$Prep=Read-Host 'Existing prepared folder containing boot-linked.bin, content.bin and patches.bin'
$Update=Read-Host 'Original Update folder (--app0)'
$Base=Read-Host 'Original Base folder (--app0-base)'
$Run="$Repo\work\probe-calibration-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
$Runtime=Join-Path $RuntimeDir "DriveClubGame.probe-$([guid]::NewGuid().ToString('N')).exe"
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.diagnostics.exe" -Destination $Runtime
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
Get-FileHash -LiteralPath $Runtime,"$Previous\stock-probe.txt","$Prep\patches.bin" |
    Format-List | Out-File "$Run\hashes.txt"
$Trigger="$Run\arm-control.flag"
$Saved=@{}
Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name]=$_.Value }
$OldLayers=$env:VK_LOADER_LAYERS_DISABLE
try {
    Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
    $env:BB_DMEM_MB='8056'
    $env:BB_MESH_PROBE=(Get-Content -Raw "$Previous\stock-probe.txt").Trim()
    $env:BB_MESH_PROBE_CONTROL=$Trigger
    $env:BB_GPU_USER_DIR="$Run\user"
    $env:BB_CONFIG="$Run\user\driveclub.ini"
    $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
    $env:BB_CRASH_DIR="$Run\logs"
    $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
    $ArgsList=@("$Prep\boot-linked.bin",'--content-profile',"$Prep\content.bin",
        '--patches',"$Prep\patches.bin",'--app0',$Update,'--app0-base',$Base,
        '--user',"$Run\user",'--stub-unresolved','--timeout','900')
    foreach($Arg in $ArgsList) { if($Arg.Contains('"')) { throw 'Argument contains a quote' } }
    $Quoted=($ArgsList | ForEach-Object { '"'+$_+'"' }) -join ' '
    $Process=Start-Process -FilePath $Runtime -ArgumentList $Quoted -WorkingDirectory $RuntimeDir `
        -RedirectStandardOutput "$Run\logs\stdout.log" -RedirectStandardError "$Run\logs\stderr.log" -PassThru
    Read-Host 'Enter Fraser Valley Road Circuit Forward and begin driving. Then press Enter HERE to arm the control'
    if($Process.HasExited) { throw 'Runtime exited before the control was armed; inspect its logs' }
    New-Item -ItemType File -Path $Trigger | Out-Null
    Write-Host "Control armed. Drive another lap, then close the game. Logs: $Run\logs"
    $Process.WaitForExit()
    $Process.ExitCode | Set-Content "$Run\logs\exit-code.txt"
} finally {
    Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
    foreach($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
    $env:VK_LOADER_LAYERS_DISABLE=$OldLayers
}
Select-String -Path "$Run\logs\*.log" -Pattern 'MeshProbe:','Fault:'
Write-Host "Evidence directory: $Run"
```

`Start-Process` inherits the configured environment; restoration affects the parent
PowerShell only. Fresh user/config/cache paths preserve existing saves and settings.
The timeout bounds the child; reaching it is not a success criterion. Original RPKs,
prepared files and the working executable are not overwritten.

If the stock signature file is unavailable, create a new one read-only:

```powershell
$Report = & "$Repo\out\dcmod.exe" mesh scan `
    "$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk" `
    --dependency "$Repo\work\dependencies\global.rpk" `
    --dependency "$Repo\work\dependencies\worlds\canada_common.rpk" `
    --target '000862187dac760f_0_polyRoadShape742' | ConvertFrom-Json
if($LASTEXITCODE -ne 0) { throw 'Stock scan failed' }
$Signature=$Report.slots[0].draw_probe_signature
```

Then set `$env:BB_MESH_PROBE=$Signature` in the launch block (and omit the missing
signature file from the hash command). To test control-only, omit the offline
signature assignment entirely; retain `BB_MESH_PROBE_CONTROL` and arm after driving.

Return the new stdout/stderr, exit code, commit/hash metadata and the approximate
arming time/location. No game binaries, RPKs, GLBs or memory dumps are needed.
First look for **capture followed by REPLAY-MATCH with a larger serial**. Then
inspect the donor's progressive gates. Only after interpreting these results should
we choose another slot/LOD or investigate a different geometry representation.

## Validation scope

Verified in this Linux workspace:

* `bash tools/test_mesh_probe.sh`: diagnostics ON/OFF, each with offline and
  control-only configuration, passed.
* `SANITIZE=1 bash tools/test_mesh_probe.sh`: the same checks passed under
  AddressSanitizer and UndefinedBehaviorSanitizer.
* `bash tools/track_import/test.sh`: all 61 Python tests passed, together with
  the native track-probe checks and Blender 4.3.2 export/hierarchy/instance tests.
* `bash tools/modkit/test.sh`: self-tests and ATRAC9 encode/decode checks passed.
* The diagnostics-enabled standalone probe test cross-compiled to Windows x64
  using LLVM-MinGW with `-Wall -Wextra -Werror`.

Synthetic tests cover exact/constant-rebased/reordered indices; 16/32-bit decoding;
failed reads and overflow; format, stride, attribute offset and signed base vertices;
finite half/float samples; capture versus later replay; changed data/shaders;
replay at different buffer addresses; trigger-file arming; budgets and read-only
behavior; independent recording-thread state; diagnostics OFF and control-only mode.

The full GPU runtime cannot be built in this workspace (CMake/GPU build dependencies
are unavailable). The Windows standalone test is cross-compiled, not executed here.
A synthetic replay is **not** real gameplay evidence. No native Road742 match,
GPU completion, visible custom geometry, LOD cause or culling cause is claimed.
