# Road742: trace file reads and search actual vertex bindings

Follow-up: the Windows run of `04fe187` verified headers but exhausted the broad
search budget. See [targeted search findings](external-track-targeted-search.md).
The launch commands below now prepare a v2 position-prefix trace and enable
targeted search. Always prepare a **new** experiment folder with the rebuilt dcmod.

## Recovery and the Windows evidence

This continues `584d04ea828283d620df3018a5dd0d785d6f86c3`. The interrupted
working tree contained the file-span scanner, read trace, vertex-binding search,
and new native tests. The first test run stopped at a signed/unsigned compiler
warning in `Fresh`; that was corrected without dropping those changes.

The supplied `stdout.log` confirms:

* Capture serial **4038747**, replay serial **4040128**, in recording lane 0.
  The control had **60 indices, Vulkan format 97 (R16G16B16A16Sfloat), stride 8**.
  This validates CPU-backed sampling/replay for that real draw, not every layout.
* The Road742 signature has **195 indices, float XYZ, stride 16, first vertex
  1456**. Final counters: 20,874,207 direct attempts; 704,169 indirect unchecked;
  13,480 equal-count draws, all with index-prefix mismatch; zero index reads
  failed; zero exact/rebased/full matches.
* The bounded candidate examples repeatedly show one readable index address,
  `0x509a9723ce`, count 195, prefix `0,1,2`, base vertex 0 and element offset 0.
  A float XYZ binding has stride 16, 183,088 bytes, and position status 6
  (different bytes). Other attributes have incompatible formats/strides.
* Final position-unpaired and base-zero-only counters are also zero. Some log
  text is interleaved with other runtime messages; intact lines support the
  values above. The raw user log is not committed.
* The log ends with `Runtime: the process ended normally`; no startup fault is
  indicated by this run.

The first three index differences are all -1456, but **the full sampled prefix
failed the existing constant-delta test**. The three numbers alone do not prove
rebasing. Equal index counts alone do not identify the donor.

## Source findings and what remains unknown

`tools/modkit/src/rpk.cpp::RpkPack::Parse` reads resource payload offsets and
sizes directly from the RPK table. These payloads are uncompressed in the loose
RPK representation supported by dcmod. `RpkResource::source_offset` now retains
that input provenance; `Write` never uses it. `mesh scan` reports the mesh,
position-buffer and index-buffer `source_file_spans`. Their offsets include the
EVOSLITL/ID headers. They are not guest addresses or native GPU allocation offsets.

`src/runtime_file.c::do_read/do_pread` is the available native file-I/O boundary.
Its existing loose-file overlay precedes the original game archives. It does not
decode native mesh selection. The new trace observes successful reads only. A
fully included 20-byte resource header is checked against the manifest ID through
`runtime_memory_read_backing`. Split reads are logged as `header=partial`; they
cannot establish header identity. `header=MISMATCH` invalidates the assumed
manifest/file association. Sequential `read-observed-offset` is an observation of
SEEK_CUR before the read; concurrent reads sharing a descriptor could race with it.
`pread` has an explicit offset and is the stronger offset evidence. No additional
seek changes the file position, and no read destination is modified by the trace.

`vk_rasterizer.cpp::ResolveIndexBuffer` supplies the exact guest index address used
by the renderer: register base plus packet element offset times index width.
`GetDrawOffsets` supplies the signed base vertex. `ResolveVertexBuffers` uses the
live or prepared attribute-specific descriptors, whose starts can differ from
resource starts. Host Vulkan cache offsets are not RPK file offsets. This change
does not alter any of those calculations or the GPU buffer cache.

`DrawRecord` and `DrawIndirectRecord` both resolve these bindings on the recording
thread. The new search runs there, with thread-local state, independently of the
old direct/index-count/index-prefix gates. Indirect bindings are inspected, but
indirect arguments/counts may be GPU-written and are **not decoded from potentially
stale CPU bytes**. Their guest addresses, stride and maximum count are logged on
a hit. An indirect hit is not evidence that a particular indirect command executes.

The native game's RPK deserializers, scene/LOD selection, road generation and
resource-to-allocation ownership are not decoded in this repository. No observed
copy/remap/regeneration of Road742 has been established. The historical scenery
lift described in `models.md` left the road in place; it is a lead for separate
road resources, not proof of the present failure. Names, dependency IDs and native
range-record patterns do not prove selection. No LOD/culling cause is asserted.

## The next diagnostic and its bounds

`BB_RPK_TRACE_FILE` names a small ASCII manifest prepared for exactly one loose
RPK. It watches three payload ranges, from startup, with at most **64 log events
and 64 header reads** in total. Each event includes resource ID, file offset,
resource-relative offset, byte count and destination guest address. It does not
track subsequent copies, frees, decompression of archive volumes, or ownership.
It is disabled in diagnostics-OFF builds.

`BB_MESH_SEARCH` names a **new trigger file**, not a Boolean. Create it when driving
so menus do not consume the search budget. A valid `BB_MESH_PROBE` stock signature
is also required. The existing positive control can use the same trigger path.

For each supported per-vertex float/half XYZ binding, the search looks for all
stock position-prefix samples at every vertex in a bounded portion of that binding.
It permits a different stride and vertex start, while requiring the same byte width
and exact XYZ bytes; there is no numeric tolerance, unknown-format guess, coordinate
conversion or vertex-order reconstruction. Shared buffers and changed draw counts
no longer exclude a candidate before it is searched.

Limits are **per recording lane**:

* 64 MiB total scanned bytes, including overlapping chunk reads; 2 MiB per binding.
* 1,024 distinct descriptor layouts in broad mode (4,096 in the new targeted mode),
  a fixed-size hash lookup and 64 KiB scratch. Targeted checks have an additional
  separate 2 MiB budget; see the follow-up guide above.
* 32 candidate hits per draw, 4,096 later hit validations, 16 detailed hit lines.
* Counters every five seconds; no forced GPU downloads, waits or synchronization.

Negative results are cached by address/size/layout for this run. A later rewrite
at a previously negative address will not be rescanned. Positive results are freshly
validated at command recording to reject changed bytes. Truncated scans, failed
reads, full cache, unsupported layouts and exhausted budgets constrain coverage;
zeros are **not proof of absence**. Vertex pulling via storage buffers, transformed,
reordered, quantized or GPU-generated vertices can remain undetected. A first copy
of a repeated prefix can hide later copies within the same binding. This is a
bounded diagnostic, not a full memory search or benchmark configuration.

On a fresh hit, `MeshSearch: ... position-prefix-copy` records the guest binding,
matched vertex offset, shader hashes and draw path. For direct indexed draws it
also reads up to 24 actual indices and reports how many reference the matched
prefix's vertices after applying the signed base vertex. It does not assume the
rest of the shared buffer belongs to Road742. Non-indexed and indirect hits report
buffer bytes only. The old full donor match counters retain their original meaning.

Correlate addresses without assuming allocation lifetime:

```
file-position-prefix = position resource file offset + 28 + vertex_first * position_stride
read-prefix-address  = RpkTrace.guest + file-position-prefix - RpkTrace.file-offset
bound-prefix-address = MeshSearch.vb + attr-offset + matched_vertex * runtime_stride
```

The read interval must actually include the prefix. Equal addresses are a useful
link, not proof that an allocation survived unchanged. Equal position bytes at
different addresses are consistent with copies/repacking or identical content;
they do not by themselves establish the copy operation.

## Build: MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
mkdir -p work
bash tools/track_import/test.sh 2>&1 | tee work/resource-trace-tests.log
bash tools/modkit/test.sh 2>&1 | tee work/resource-trace-modkit.log
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
git rev-parse HEAD
```

The importer script rebuilds dcmod and runs both probe suites. Blender checks run
only if Blender is on PATH; synthetic checks need no game files. Stop on build/test
failure. Do not launch the old executable thinking it contains the new diagnostics.

## Prepare and run: PowerShell

This creates a fresh byte-identical stock loose-file override, not another geometry
modification. It leaves the original source, prepared binaries, previous experiments
and working executable untouched. `prepare_resource_trace.py` rejects an existing
output directory, checks source headers/spans, and verifies source/copy SHA256.
Only use the original extracted stock RPK here. Generated RPK copies are private.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-targeted-search-$([guid]::NewGuid().ToString('N'))"
$Input="$Run\inputs"
python "$Repo\tools\track_import\prepare_resource_trace.py" `
  --dcmod "$Repo\out\dcmod.exe" `
  --source "$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk" `
  --dependency "$Repo\work\dependencies\global.rpk" `
  --dependency "$Repo\work\dependencies\worlds\canada_common.rpk" `
  --target '000862187dac760f_0_polyRoadShape742' --output $Input
if($LASTEXITCODE -ne 0) { throw 'Preparation failed; do not launch this folder' }
$RuntimeDir=Read-Host 'Working runtime folder containing the runtime DLLs'
$Prep=Read-Host 'Existing prepared folder containing boot-linked.bin, content.bin and patches.bin'
$Update=Read-Host 'Original Update folder (--app0)'
$Base=Read-Host 'Original Base folder (--app0-base)'
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
$Runtime=Join-Path $RuntimeDir "DriveClubGame.resource-trace-$([guid]::NewGuid().ToString('N')).exe"
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.diagnostics.exe" -Destination $Runtime
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
Get-FileHash -LiteralPath $Runtime,"$Input\mods\worlds\canada_road_circuit_02_n.rpk","$Prep\patches.bin" |
    Format-List | Out-File "$Run\hashes.txt"
$Trigger="$Run\arm-search.flag"
$Saved=@{}
Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name]=$_.Value }
$OldLayers=$env:VK_LOADER_LAYERS_DISABLE
try {
    Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
    $env:BB_DMEM_MB='8056'
    $env:BB_RPK_TRACE_FILE="$Input\rpk-trace.txt"
    $env:BB_MESH_PROBE=(Get-Content -Raw "$Input\stock-probe.txt").Trim()
    $env:BB_MESH_SEARCH=$Trigger
    $env:BB_MESH_SEARCH_TARGET=(Get-Content -Raw "$Input\mesh-search-target.txt").Trim()
    $env:BB_MESH_PROBE_CONTROL=$Trigger
    $env:BB_GPU_USER_DIR="$Run\user"
    $env:BB_CONFIG="$Run\user\driveclub.ini"
    $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
    $env:BB_CRASH_DIR="$Run\logs"
    $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
    $ArgsList=@("$Prep\boot-linked.bin",'--content-profile',"$Prep\content.bin",
        '--patches',"$Prep\patches.bin",'--app0',$Update,'--app0-base',$Base,
        '--user',"$Run\user",'--mods',"$Input\mods",'--stub-unresolved','--timeout','900')
    foreach($Arg in $ArgsList) { if($Arg.Contains('"')) { throw 'Argument contains a quote' } }
    $Quoted=($ArgsList | ForEach-Object { '"'+$_+'"' }) -join ' '
    $Process=Start-Process -FilePath $Runtime -ArgumentList $Quoted -WorkingDirectory $RuntimeDir `
        -RedirectStandardOutput "$Run\logs\stdout.log" -RedirectStandardError "$Run\logs\stderr.log" -PassThru
    Read-Host 'Begin driving Fraser Valley Road Circuit Forward, then press Enter HERE to arm the search'
    if($Process.HasExited) { throw 'Runtime exited before arming; inspect logs' }
    Get-Date -Format o | Set-Content "$Run\arming-time.txt"
    New-Item -ItemType File -Path $Trigger | Out-Null
    Write-Host 'Drive the route, then close the game. No visible change is expected from this stock copy.'
    $Process.WaitForExit()
    $Process.ExitCode | Set-Content "$Run\logs\exit-code.txt"
} finally {
    Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
    foreach($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
    $env:VK_LOADER_LAYERS_DISABLE=$OldLayers
}
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','MeshSearch:','MeshProbe:','replaced by mods folder' |
    Out-File "$Run\diagnostic-lines.txt"
Write-Host "Evidence directory: $Run"
```

Keep stdout and stderr separately to reduce the interleaving seen in the previous
log. Return those logs, `diagnostic-lines.txt`, `commit.txt`, `hashes.txt`,
`arming-time.txt`, `inputs/preparation.json` and the focused `inputs/mesh-scan.json`.
Do not upload the generated RPK, game binaries or memory dumps.

## How to decide the following step

1. **RpkTrace header verified:** the expected resource header reached a read
   destination. This is stronger than an RPK-open message, but not native selection.
2. **Fresh position-prefix-copy:** matching Road742 prefix bytes occur in a supported
   binding of a recorded draw. Correlate its address with read spans and its shaders.
3. **referenced-prefix-indices > 0:** sampled indices of that direct draw reference
   the matching prefix. This identifies a specific draw to inspect further, though
   shared/repeated geometry can still produce ambiguity and pixels remain unverified.
4. **Indirect hit only:** inspect the logged indirect argument producer next;
   do not treat stale CPU argument bytes as executed draw commands.
5. **No hit:** inspect coverage limits and read provenance first. This does not
   establish LOD/culling, nor justify randomly replacing more meshes.

A fresh direct hit with referenced prefix vertices is the immediate desired result.
After identifying that draw/resource relationship, compare a controlled marker run
using its own regenerated signature. Visible custom geometry remains a separate
Windows observation; this experiment does not establish collision or drivability.

## Validation and current limits

Verified in this workspace:

* `tools/track_import/test.sh`: 63 Python tests passed, native mesh/track probe
  tests passed, and Blender 4.3.2 smoke/hierarchy/instance checks passed.
* `tools/modkit/test.sh`: synthetic modkit and ATRAC9 checks passed. Existing RPK
  round trips remain covered; source offsets are diagnostic metadata only.
* `tools/test_resource_trace.sh`: diagnostics ON/OFF passed, including float/half
  search, chunk boundaries, relocated/repacked rows, failed reads, arithmetic
  bounds, index references, stale data, trigger gating, read budgets and TLS.
* The new native suites also passed with AddressSanitizer and UndefinedBehaviorSanitizer.
* Both new standalone tests cross-compiled to Windows x64 with LLVM-MinGW and
  `-Wall -Wextra -Werror`. These Windows binaries were not run here.
* `src/runtime_file.c` passed Linux syntax checks in diagnostics ON/OFF modes.
* Python preparation tests verify source/copy identity, reject malformed source
  spans/paths/headers, and reject reuse of an output directory.

The full Vulkan runtime cannot be compiled in this workspace: CMake and the GPU
build dependencies are unavailable. The rasterizer integration therefore requires
the Windows rebuild above. The new file/buffer trace has **not** been validated
against the user's game, and a synthetic matching test is not gameplay evidence.

Modified implementation files: `gpu/shim/bbport_mesh_search.h`,
`gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`,
`src/runtime_rpk_trace.h`, `src/runtime_file.c`,
`tools/modkit/src/{rpk.hpp,rpk.cpp,cmd_mesh.cpp}`,
`tools/track_import/prepare_resource_trace.py`, the native tests/runner and importer
test integration, plus this guide and the calibration follow-up link.
