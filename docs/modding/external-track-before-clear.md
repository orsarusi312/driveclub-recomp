# Road742: inspect the native caller before the read buffer is cleared

## Recovered state and evidence

Resumed `feat/external-track-foundation` at `5fa4cda59308515c686bb28ad56d0198a316aed0`.
Recovered the unfinished `runtime_rpk_clear.h` / `runtime_rpk_clear_impl.h` observer
and its two integration edits in `runtime.c` / `runtime_rpk_trace.h`. Those changes
had no tests or documentation yet. No resets, game-file edits, or prior-experiment
changes were needed. This milestone finishes that implementation.

Inspected all three supplied logs: 106,761 debugger lines, 1,434 stdout lines,
72 stderr lines. The debugger log contains one `Breakpoint 0 hit`, no breakpoint-1
hit and no second-chance exception report. Most lines are first-chance exceptions.
Stdout confirms 40 Forward RPK override messages and `the process ended normally`.
The read trace verifies the vertex prefix twice, but only the first read was stopped.

The first read was `0xf0026df180 + 65536`; the watched prefix was at offset `0x708c`
in that read. At the REP stop, `RDI - read_base = 0x7100`, and
`0x7100 + RCX(0x8f00) = 0x10000`. `R8=0x10000`, `R9=read_base`, `RAX=0` corroborate
a 64 KiB zero fill crossing the watched byte. These are historical addresses,
**never addresses to reuse in another process**.

The executable load base was `0x7ff6bca10000`. The immediate host return address
`0x7ff6bca51ecd` therefore has RVA **`0x41ecd`** in that particular binary. The
aligned disassembly shows a 4096-byte chunk loop, an intermediate write helper
with a success/fallback branch, and a whole-buffer CRT memset fallback. This closely
matches `src/runtime.c::guest_memset` (MEMOPS_CHUNK=4096, bbgpu_guest_write, CRT fallback).
The subsequent guest return address is **`0x800591dc5`**. The read-stop stack's
nearest guest return is `0x8005a6e2c`; `0x800591ede` also appears farther down the raw
stack. Without guest unwind information the deeper stack must not be treated as a
reliable call chain. The log's first `u ...1e90` starts mid-instruction; the later
aligned `ub ...1ec8` is the usable disassembly, not its spurious `std` decode.

This is strong structural evidence for a **guest-requested clear through the runtime
libc wrapper**, rather than autonomous allocator housekeeping. It is not exact
PDB/PE symbol resolution: the original executable and symbol map are not available
here. The new observer records the wrapper and its immediate guest return directly
to confirm or refute that attribution in a new Windows run.

## What the watchpoint does not cover

On x86, `ba r` triggers on writes as well as reads. The first observed write after
arming is not necessarily the first consumer of all resource bytes. The watch
covered only one byte in two virtual aliases, after host read/verification finished.
Another offset, another physical alias, GPU access, concurrent activity before
arming, or an earlier read of the resource could escape it. A one-shot host stop
also changes timing. The matching bytes in a later read do not establish which
read supplies rendering.

Native x86 guest code runs directly (`runtime_cpu.c`); inline accesses bypass HLE
memcpy/memmove instrumentation. The existing post-read observation does not interpose
on arbitrary native instructions. There is no evidence yet that indices/positions
were repacked, discarded, or submitted. The smaller next experiment is to identify
the exact native clear caller and inspect its surrounding instructions while the
verified bytes are still present, rather than scanning more geometry.

## Implemented observer

* `BB_RPK_CLEAR_TRACE=1` selects the first fully header-and-prefix verified RpkTrace
  read; `2` selects the second. Requires the existing v2 manifest in
  `BB_RPK_TRACE_FILE` and a diagnostics build. Invalid values disable observation.
* Keeps one immutable read snapshot. Up to 262,144 HLE memset calls after arming
  receive range checks; only the first call overlapping an actual sampled XYZ byte
  is checked against all eight (or manifest-specified) samples. Padding-only writes,
  empty/overflowing ranges and unrelated addresses do not trigger verification.
* Logs resource, original read range/file offset, destination, length, fill byte,
  immediate caller return, freshness and whether the read base/size match. Stale or
  unreadable samples consume the one-shot and cannot cause a debugger stop. There
  is no automatic reseed or delayed retry, and at most one overlap verification.
* `BB_RPK_DEBUG_CLEAR=1` additionally pauses **before** the existing memset, only
  when bytes still verify and a Windows debugger is already attached. It emits
  `RpkClear: STOP BEFORE memset` to stderr and OutputDebugString, then host DebugBreak.
  Without a debugger it logs a skipped stop and continues. The non-Windows observer
  can log but cannot pause through a real debugger callback.
* Metadata publication and claiming use atomics. No metadata lock is held while
  reading or stopping. errno and Windows LastError are preserved. With diagnostics
  OFF the wrapper call is compiled out; default ON builds perform no guest sampling
  until explicitly configured and armed.
* The original CPU/GPU/fallback memset paths and return values are unchanged. No
  guest instruction patch, watchpoint, protection change, GPU upload or fence is
  added. The opt-in pause affects timing. It observes only the selected guest VA,
  not all aliases, native inline memset, or arbitrary readers. Fresh samples are
  evidence at sampling time, not an allocation-lifetime lease.

This confirms a **read-to-overwrite edge** if Windows reports fresh bytes. It is
not yet a read-to-consumer or resource-to-draw edge. The caller's bounded native
context is the input needed to select the next actual consumer boundary.

## Build in MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/before-clear-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
bash tools/test_runtime_memory.sh 2>&1 | tee "$RunLog/memory.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Prepare and launch under WinDbg: PowerShell

Use a new directory and the original Forward RPK. This run observes the existing HLE memset **before** it writes. It does not repeat BufferPath or install data watchpoints.
The small CMD launcher redirects the actual game's streams, while WinDbg's `-o`
option also debugs its child process. WinDbg must be installed locally; supply its
executable path (`WinDbgX.exe` or the x64 Windows SDK `windbg.exe`). This wrapper uses
ASCII paths and rejects CMD expansion characters rather than guessing escaping.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-before-clear-$([guid]::NewGuid().ToString('N'))"
$Inputs="$Run\inputs"
python "$Repo\tools\track_import\prepare_resource_trace.py" `
  --dcmod "$Repo\out\dcmod.exe" `
  --source "$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk" `
  --dependency "$Repo\work\visual-recovery\global.rpk" `
  --dependency "$Repo\work\visual-recovery\worlds\canada_common.rpk" `
  --target '000862187dac760f_0_polyRoadShape742' --output $Inputs
if($LASTEXITCODE -ne 0) { throw 'Preparation failed' }
$RuntimeDir=Read-Host 'Working runtime directory containing its DLLs'
$Prep=Read-Host 'Prepared directory containing boot-linked.bin, content.bin, patches.bin'
$Update=Read-Host 'Original Update directory (--app0)'
$Base=Read-Host 'Original Base directory (--app0-base)'
$Debugger=Read-Host 'Full path to WinDbgX.exe or x64 windbg.exe'
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
$Runtime=Join-Path $RuntimeDir "DriveClubGame.before-clear-$([guid]::NewGuid().ToString('N')).exe"
Copy-Item "$Repo\out\DriveClubGame.diagnostics.exe" $Runtime
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
Get-FileHash -LiteralPath $Runtime,"$Inputs\mods\worlds\canada_road_circuit_02_n.rpk","$Prep\patches.bin" |
  Format-List | Out-File "$Run\hashes.txt"
$ArgsList=@($Runtime,"$Prep\boot-linked.bin",'--content-profile',"$Prep\content.bin",
  '--patches',"$Prep\patches.bin",'--app0',$Update,'--app0-base',$Base,
  '--user',"$Run\user",'--mods',"$Inputs\mods",'--stub-unresolved','--timeout','900')
foreach($Arg in ($ArgsList + @($RuntimeDir,$Run,$Debugger))) {
  if($Arg -match '["%\r\n\x80-\uFFFF]') { throw 'Use ASCII paths without quotes, percent signs or newlines for this CMD launcher' }
}
$Quoted=($ArgsList | ForEach-Object { '"'+$_+'"' }) -join ' '
$Launcher="$Run\launch.cmd"
$Lines=@('@echo off',"cd /d `"$RuntimeDir`"",
  "$Quoted 1>`"$Run\logs\stdout.log`" 2>`"$Run\logs\stderr.log`"",
  "echo %errorlevel% > `"$Run\logs\exit-code.txt`"",'exit /b')
[IO.File]::WriteAllLines($Launcher,$Lines,[Text.Encoding]::ASCII)
$Saved=@{}
Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name]=$_.Value }
$OldLayers=$env:VK_LOADER_LAYERS_DISABLE
try {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  $env:BB_DMEM_MB='8056'
  $env:BB_RPK_TRACE_FILE="$Inputs\rpk-trace.txt"
  $env:BB_RPK_CLEAR_TRACE='1'
  $env:BB_RPK_DEBUG_CLEAR='1'
  $env:BB_GPU_USER_DIR="$Run\user"
  $env:BB_CONFIG="$Run\user\driveclub.ini"
  $env:BB_TOGGLE_FILE="$Run\user\toggles.txt"
  $env:BB_CRASH_DIR="$Run\logs"
  $env:VK_LOADER_LAYERS_DISABLE='~implicit~'
  # The fixed H:\DC-Track-Test root keeps this CMD /c filename free of spaces.
  if($Launcher.Contains(' ')) { throw 'Use the stated repository root without spaces' }
  $DebugArgs="-o -logo `"$Run\logs\windbg.log`" `"$env:ComSpec`" /d /v:off /c $Launcher"
  $Process=Start-Process -FilePath $Debugger -ArgumentList $DebugArgs -PassThru
  Write-Host "Follow the WinDbg steps below. Evidence directory: $Run"
  $Process.WaitForExit()
} finally {
  Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
  foreach($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
  $env:VK_LOADER_LAYERS_DISABLE=$OldLayers
}
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','RpkClear:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
Write-Host "Return the logs and metadata from: $Run"
```

Do not set BB_RPK_DEBUG_READ, BB_GUEST_TRACE, BB_GUEST_SAMPLE, BB_MESH_SEARCH, BB_BUFFER_PATH or copy
tracing for this experiment. The wrapper clears inherited BB settings only for
launch and restores them afterward. Existing files, saves and experiments remain
untouched. No graphics or geometry change is expected.

## At WinDbg: one pre-clear stop, then one native return

1. At initial CMD/game loader breaks use `g`. Use `sxd av` if the runtime's handled
   page-tracking exceptions interrupt execution; this leaves protections intact
   and still stops on unhandled exceptions. Select **Fraser Valley Road Circuit
   Forward**. There is no gameplay trigger file. No BufferPath or MeshSearch is armed.
2. Wait for `RpkClear: STOP BEFORE memset ... guest-return=...`. Do not confuse a
   loader break with this stop. `RpkClear: armed` alone does not mean a clear occurred.
   Use the **current STOP line** for the following two placeholders:

   ```text
   .lastevent
   |
   ~.
   r
   kv 8
   bl
   r @$t0 = <guest-return-from-current-STOP>
   r @$t1 = <prefix-from-current-STOP>
   db @$t1 L80
   ub @$t0 L30
   u @$t0 L60
   ```

   `ub` is a backward decode heuristic, not guaranteed instruction boundaries.
   The return address starts at a known instruction boundary; its forward `u`
   output is authoritative. Preserve both to inspect the call and surrounding
   native control flow. The small 128-byte prefix view is optional local evidence;
   do not send full buffers, resource dumps, archives or executables.
3. If `bl` shows hardware breakpoint slot 0 free, place **one execution hardware
   breakpoint** at the reported return, then resume:

   ```text
   ba0 e 1 @$t0
   g
   ```

   Use another free slot if needed and clear only that same slot. No software `bp`,
   code patch or single-step loop is needed. This breakpoint can also hit another
   thread reaching the same site: check thread identity and stack before attributing
   it to the stopped clear. If the reported return is zero/unmapped, skip this step
   and return the pre-clear evidence instead of guessing an address.
4. At that first execution hit, immediately clear the breakpoint and collect:

   ```text
   bc 0
   .lastevent
   ~.
   r
   u @rip L60
   kv 8
   bl
   g
   ```

   This captures the native state after returning from the existing clear, without
   assuming what the next native routine does. Do not trace unlimited instructions
   or leave the breakpoint active for repeated hits. If a different stop intervenes,
   identify it before collecting/continuing. Clear the one breakpoint if ending
   the experiment early. Close the game normally, then close WinDbg so PowerShell
   finishes recording the evidence. The runtime never repeats its pre-clear stop.

## Expected results and evidence to return

The useful sequence in **stderr** is:

```text
RpkTrace: ... position-prefix=verified ...
RpkClear: armed selected=1 ...
RpkClear: BEFORE ... freshness=verified ... caller=... same-read-base=1 same-read-size=1
RpkClear: STOP BEFORE memset ... guest-return=...
RpkClear: resumed; original memset will proceed unchanged; stop budget exhausted
```

The same-read flags are an expectation to test, not a requirement. A different
caller, length, or nonzero fill is also useful evidence. `prefix-mismatch` or
`backing-unreadable` means the old read snapshot could not be freshly attributed;
no pause follows. `call budget exhausted` means only that this bounded HLE observation
missed an overlap. An armed trace with no terminal line may mean the run ended
before an overlap or call limit. None of these negative outcomes proves absence
from rendering. For a separately justified second-read test, use a **new** run
folder and change only `BB_RPK_CLEAR_TRACE` to `2`; do not widen budgets blindly.

Return `logs/windbg.log`, `logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`,
`diagnostic-lines.txt`, `commit.txt`, `hashes.txt`, `inputs/preparation.json`, and the
small focused `inputs/mesh-scan.json`. Include the build/test logs, selected read
ordinal, whether the pre-clear stop occurred, and whether the native return hit on
the same thread. Keep any buffer bytes and native disassembly limited to the ranges
above and private to this investigation. No complete game archives or bulk dumps.

A fresh prefix at the real wrapper plus its captured native caller proves which
operation overwrites that read window. To advance to a **consumer**, the surrounding
native instructions must show a load/copy/decode of verified resource bytes and a
verifiable destination. That destination's lifetime and later draw binding must
then be traced. Matching a shader or pointer alone is insufficient. No visual
change is expected from this diagnostic; no Road742 draw identity, custom rendering,
collision or drivability is claimed.

## Validation

* `bash tools/track_import/test.sh`: **63 Python tests passed**, native mesh/resource
  probes passed ON/OFF, track probe passed, Blender **4.3.2** smoke/hierarchy/instance/
  winding checks passed. Existing importer and staging code were not modified.
* `bash tools/modkit/test.sh`: all synthetic checks and ATRAC9 encoder checks passed.
* `SANITIZE=1 bash tools/test_resource_trace.sh`: ASan/UBSan passed with diagnostics
  ON/OFF, including new observer and real memset-wrapper tests and prior BufferPath,
  RpkTrace, RpkCopy, MeshProbe search and separate-translation-unit tests. The final
  added trace-callback integration assertion was also rerun under ASan/UBSan ON/OFF.
* `SANITIZE=1 bash tools/test_runtime_memory.sh`: real synthetic mappings passed
  under both atomic and pthread locks; six memory-profile tests passed under UBSan.
* New tests exercise first/second verified-read selection, refusal to reseed, byte
  overlap versus stride padding, empty/overflow ranges, stale final sample, failed
  reads, missing debugger, call budget, concurrent claims, shared exported observer
  state across translation units, unchanged data and errno (Windows LastError check
  compiled, not executed here). Mock callbacks are used for debugger pauses.
* The real `guest_memset` harness verifies observer-before-write order, caller
  forwarding, CPU path, GPU write path, failed GPU-write fallback, identical final
  bytes and return pointer. This harness runs on Linux; its section-discard link
  technique failed with LLVM-MinGW COFF unresolved unrelated runtime functions, so
  it is explicitly skipped on Windows. Windows still runs the complete observer
  policy/integration tests. No runtime behavior was changed to accommodate a test.
* New observer+peer tests cross-compiled for Windows x64, diagnostics ON/OFF, with
  warnings as errors. Linux and LLVM-MinGW syntax checks of both changed runtime
  translation units passed ON/OFF. `git diff --check` passed.

Private/local validation logs remain in `work/rpk-clear-validation/`. No game logs,
proprietary assets or large artifacts are committed. Full Vulkan/game builds and
new Windows debugger stops cannot be run in this Linux environment (no full CMake/
GPU dependency setup). Cross-compilation and synthetic tests are **not** Windows
runtime validation. The commands above are the remaining integration test.

Source files changed: `src/runtime.c`, `src/runtime_rpk_trace.h`, new
`src/runtime_rpk_clear.h`, new `src/runtime_rpk_clear_impl.h`. Tests: new
`tools/rpk_clear_test.c`, `tools/rpk_clear_test_peer.c`,
`tools/runtime_memset_trace_test.c`, and `tools/test_resource_trace.sh`.
Documentation: this report and the latest-experiment link in
`external-track-resource-trace.md`.
