# Forward: shader-filtered buffer acquisition trace

## Recovered state and actual Windows evidence

Started from a clean `feat/external-track-foundation` at
`efe2ba75260e41d41fe2acdc2a6fe41bce0ed506`, matching the fork. No unfinished changes
were discarded. Inspected both supplied logs (2029 stdout / 278 stderr lines) and
the focused scan. Raw private logs and game data are not committed.

The observed-layout experiment did its intended work: eight completed scans,
10,497,892 bytes including overlap, zero failures/truncations and no donor-prefix
matches. Positive control captured **2997460**, replay **2997461**. The stdout log
ends normally and confirms the stock `_n.rpk` override. Forward remains the only
route covered by this manifest.

Six selected float views share `vs=2a937475 / ps=d1e35cbc`. They are **three pairs**
of attribute views, not six independent geometry buffers: each pair has the same
size/stride and addresses separated by 12 bytes. Records are 18057, 37334 and 37449,
stride 44. They are leads, not confirmed road resources. Both float-shaped views
were searched; neither matched the original position prefix. Shader hashes alone
cannot tell us whether their inputs are positions, normals, transformed coordinates
or some other data. No matching shader binary/source is present in this repository.

RpkTrace verified prefix addresses `0xf0026dc2fc` and `0xf0026db6bf`. Both failed at
sample zero and retired on the same later HLE copy, source `0xf0026d529e`, destination
`0x509ef84c00`, length 36864. That copy is **not** verified Road742 provenance: the
source was already different. Its destination must not be treated as the donor.
The index header remains verified; it does not verify the distant mesh index window.

## Source findings

| Boundary | Source | What it actually does |
|---|---|---|
| Loose RPK read | `src/runtime_file.c` / `runtime_rpk_trace.h` | Reads bytes into a guest address and verifies the manifest prefix; no native mesh ownership |
| Guest libc copy | `src/runtime.c`, `runtime_rpk_copy_impl.h` | Observes guest HLE memcpy/memmove only; inline/native instructions are outside these hooks |
| Upload byte reader | `gpu/shim/bbgpu.cpp::CopySparseMemory/CopySparseSerial` | Copies backing regions through `runtime_memory_read_sparse` (or backing reads); no position conversion |
| Small read-only binding | `BufferCache::ObtainBuffer` | If below stream threshold and not GPU-modified, queues `RunGuestCopy` or copies into mapped stream memory immediately |
| Arena acquisition | `ObtainBuffer -> SynchronizeMemory -> UploadCopies` | Collects CPU-dirty ranges, stages bytes, changes copy offsets from guest addresses to arena-relative offsets, records Vulkan buffer copies |
| Deferred host work | `SmallGuestCopy`, `RunGuestCopy`, `BbCopy::Async` | Host copies bypass the guest HLE hooks; existing scheduler waits for them before submission |
| Image acquisition | `ObtainBufferForImage`, `SynchronizeMemoryFromImage` | Can use staging or image/meta-derived content; not native mesh decoding |
| Native GPU copy packet | `Rasterizer::CopyBuffer` | Can use host memcpy for clean ranges or record a GPU transfer; neither is a guest libc wrapper |
| Raw shader memory | `CollectShaderInfoPass`, `BindResources`, `SynchronizeDmaBuffers` | Dynamic ReadConst can set `uses_dma`, add a BDA page table/fault buffer and access arena memory beyond vertex attributes |
| Vertex input | `ResolveVertexBuffers` | Uses prepared/live descriptors, merges overlapping address ranges, obtains one buffer per merged range and adjusts binding offsets |
| Direct/indirect recording | `DrawRecord`, `DrawIndirectRecord` | Bindings and commands are resolved on their recording lane; indirect arguments may be GPU-written |

The observed upload functions copy bytes; they do not rebase triangle indices,
apply coordinate transforms, reorder vertices or quantize positions. Those operations
could occur in native guest processing or shaders, but there is no evidence yet that
they occur for Road742. Arena offset changes are host buffer addressing, not evidence
of a native mesh transformation. Host-visible arena mappings used for readback do
not establish a CPU-to-GPU zero-copy path for this resource.

**Why upload logging alone is insufficient:** cache APIs receive guest addresses,
sizes and dirty state, not RPK IDs. Uploads may predate an observed draw. A GPU-modified
range may contain compute output, a copy result or other content. Constant rings can
hold a snapshot made earlier than recording. Equal addresses across time do not
establish allocation lifetime. Shader-side raw reads can bypass the ordinary vertex
input descriptors. The RPK-to-native-allocation gap therefore remains open.

## Implemented diagnostic

Set `BB_BUFFER_PATH=p1:2a937475:d1e35cbc` and `BB_BUFFER_PATH_TRIGGER` to a fresh
trigger file path. No MeshSearch or offline position matching is needed. The normal
positive control may run separately with only `BB_MESH_PROBE_CONTROL` set.

A thread-local scope in the actual direct/indirect recording functions captures
metadata only for the selected shader pair in two phases when `BB_BUFFER_PATH_EARLY=1`: first use during startup/loading,
then a fresh budget after the gameplay trigger. This catches initial static uploads
that would already be resident by gameplay. Without EARLY it waits for the trigger.
It logs only after the
command is handed to the existing scheduler. Early-return/aborted draws emit nothing.
The scope includes prepared vertex inputs and the normal BindBuffers path, including
when texture binding is delegated to a helper. It deliberately does not move TLS
state onto asynchronous copy workers: queued upload metadata is captured where the
copy is scheduled. This is not a completion callback.

Bounds per lane **per phase**: **256 selected attempts, 16 distinct binding-set captures, 64 events
per attempted draw**. The trigger advances phases only once; limits cannot be repeatedly reset. Deduplication uses a bounded 64-bit key of vertex/index descriptor
metadata, not transient constant-ring addresses. It can omit later writes at reused
addresses, different constants for the same binding set, and extremely rare hash
collisions. Event overflow and capture/attempt exhaustion are reported. No unlimited
per-draw logging, memory scanning, guest writes, extra backing reads, GPU downloads,
forced waits or changes to normal uploads are introduced. Only existing dirty-state
metadata is queried. Diagnostics-OFF and absent/invalid configuration disable it.

All lines for one capture share `draw=N`:

* `vertex`: guest descriptor address/size, attribute offset, location, runtime stride
  and Vulkan format; flags = Vulkan input rate (0 vertex, 1 instance).
* `index`: actual guest index address after packet offset; stride = index element
  size; offset = packet index offset in elements. On indirect draws the descriptor
  size is the existing binding request, not an executed indirect count.
* `resource`: stage hash, native buffer slot, guest address/size/stride; format =
  shader BufferType (0 Guest), flags bit 0 written / bit 1 formatted.
* `constant-ring`: pre-captured ring size/offset and stage/slot. Guest origin is
  **unknown** here, represented by zero, not a null guest constant declaration.
* `special`: shader resource type: 1 Flatbuf, 2 BDA page table, 3 fault buffer,
  4 GDS, 5 shared memory, 6 clip planes. These are not decoded mesh resources.
* `shader`: stage hash and kind in slot; bytes field holds the buffer-resource count
  for this metadata event. Flags bit 0 has fetch shader; bit 1 uses dynamic raw-memory
  access (`uses_dma`). Neither flag by itself proves geometry vertex pulling.
* `obtain`: the actual cache request; flags bit 0 written, bit 1 texel/formatted,
  bit 2 GPU-modified-before, bit 3 CPU-modified-before.
* `stream-queued` / `stream-now`: guest-to-stream copy path, returned buffer token
  and host offset. Queued is scheduling evidence only.
* `upload-queued` / `upload-now`: CPU-dirty source range handed to UploadCopies and
  destination arena token/offset. These can extend beyond one descriptor due to
  tracking granularity. They describe an upload request, not completion/byte identity.
* `arena`: returned arena token/offset; no matching upload event can mean it was
  already resident, GPU-produced or omitted due to overflow, not "never uploaded".
* `indirect-args` / `indirect-count`: guest argument ranges, never sampled as if
  they were executed commands. Draw summary count/instances zero means unchecked.

`buffer-token` is a process-local C++ buffer object identity, **not** a Vulkan device
address or a persistent allocation ID. Correlate guest range containment and equal
token/offset **within the same captured draw**. A captured buffer request is not
necessarily vertex data; auxiliary resources used during binding may also appear.

This establishes a narrower, observable path from a selected command through its
binding and acquisition operations. It does **not** establish that those resources
are Road742, nor does it claim custom rendering.

## Windows build (MSYS2 CLANG64)

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/buffer-path-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Windows gameplay (PowerShell)

Use the original Forward RPK. Dependency paths match the supplied scan; adjust only
those two paths if your existing copies are elsewhere. This creates a new stock-copy
experiment, unique executable, fresh user/config and logs. No previous experiments,
original archives or saves are overwritten. It disables the previous geometry searches.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-buffer-path-$([guid]::NewGuid().ToString('N'))"
$Input="$Run\inputs"
python "$Repo\tools\track_import\prepare_resource_trace.py" `
  --dcmod "$Repo\out\dcmod.exe" `
  --source "$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk" `
  --dependency "$Repo\work\visual-recovery\global.rpk" `
  --dependency "$Repo\work\visual-recovery\worlds\canada_common.rpk" `
  --target '000862187dac760f_0_polyRoadShape742' --output $Input
if($LASTEXITCODE -ne 0) { throw 'Preparation failed; do not launch this folder' }
$RuntimeDir=Read-Host 'Working runtime folder containing the runtime DLLs'
$Prep=Read-Host 'Existing prepared folder containing boot-linked.bin, content.bin and patches.bin'
$Update=Read-Host 'Original Update folder (--app0)'
$Base=Read-Host 'Original Base folder (--app0-base)'
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
$Runtime=Join-Path $RuntimeDir "DriveClubGame.buffer-path-$([guid]::NewGuid().ToString('N')).exe"
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
    $env:BB_BUFFER_PATH='p1:2a937475:d1e35cbc'
    $env:BB_BUFFER_PATH_TRIGGER=$Trigger
    $env:BB_BUFFER_PATH_EARLY='1'
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
    Read-Host 'Begin driving Fraser Valley Road Circuit Forward, then press Enter HERE to arm the gameplay capture phase'
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
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','BufferPath:','MeshProbe:','replaced by mods folder' |
    Out-File "$Run\diagnostic-lines.txt"
Write-Host "Evidence directory: $Run"
```

Return both logs, diagnostic-lines, commit/hash/arming metadata, preparation report
and focused scan. Do not upload RPKs, shader binaries, game binaries or memory dumps.
No visible geometry change is expected from this stock copy.

## Interpret the next run

1. `early=1` and `phase=startup` captures should precede the gameplay trigger
   if the pair is used during loading. `armed for selected shader pair` and
   `draw=N phase=gameplay path=direct/indirect-unchecked` confirms the new observer executed.
   An armed banner without captures can indicate aborted attempts; limit messages
   constrain coverage. Absence of the shader pair after arming remains inconclusive.
2. Compare startup and gameplay captures, without assuming lifetime from equal
   addresses alone. Correlate vertex/index guest spans with `obtain`, `upload-*`/`stream-*` and `arena`
   records for the same draw. An upload gives an actual CPU backing source to trace;
   a GPU-dirty/no-upload request instead points toward the previous GPU producer.
3. Inspect shader/resource records for constant-ring use and DMA/special resources.
   Metadata can reveal an uncovered path; it cannot decode transformation math.
4. The next observable native boundary, if the buffer is CPU-produced, is the first
   consumer of the verified RPK prefix **immediately after that read returns**, before
   the scratch address is overwritten. A debugger read watchpoint on that run's
   prefix, installed at the file-read boundary, can capture the guest instruction
   and call path. Installing it after the stale-copy log is too late. This requires
   an interactive Windows debugger/native trace; no protection changes or watchpoints
   are installed by this patch. Do not reuse historical addresses from these reports.
5. For GPU-produced spans, the next targeted observer should watch writes/copies to
   the **captured guest range** and name the dispatch/copy producer. For dynamic raw
   shader accesses, inspect the selected shader's local IR/access path before treating
   vertex-input metadata as complete. Neither path should begin with another broad
   geometry search or random mesh replacement.

The milestone remains a verified resource-to-command connection, followed by a
controlled visible marker. LOD/culling, transformations, generated road geometry and
Road742 selection remain unresolved; none is asserted as the cause.

## Validation and changed files

* Importer: **63 Python tests passed**, native probe tests passed, Blender 4.3.2
  smoke/hierarchy/instance/reflected-winding checks passed.
* Modkit self-tests and ATRAC9 encoder checks passed.
* Resource diagnostic suite passed in diagnostics ON/OFF, including ASan/UBSan.
  Final BufferPath tests also passed sanitizers with a separate translation-unit
  peer, verifying shared TLS state, disabled/absent/invalid configuration, shader
  filtering, trigger gating, startup-to-gameplay budget renewal, nested/aborted
  scopes, deduplication, event/attempt/capture limits and indirect metadata.
* Final standalone BufferPath tests cross-compiled for Windows x64 using LLVM-MinGW
  with `-Wall -Wextra -Werror`, diagnostics ON/OFF. Not executed on Windows here.
* `git diff --check` passed. No remaining automated test failures.

**Not validated here:** full Vulkan compilation/integration or Windows gameplay.
CMake and the GPU build environment are unavailable in this workspace. Standalone
unit tests validate the observer, not the rasterizer integration or native resource
identity. The Windows build and experiment above are required. No rendering fix,
Road742 identity, GPU completion or visible custom geometry is claimed.

Changed: `gpu/shim/bbport_buffer_path.h` (new),
`gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`,
`gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp`,
`tools/buffer_path_test.cpp` and `tools/buffer_path_test_peer.cpp` (new), `tools/test_resource_trace.sh`, this report and
the resource-trace guide's latest-experiment link.
