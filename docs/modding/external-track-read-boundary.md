# Road742: stop at the verified file-read boundary

## Recovery and the BufferPath evidence

Continued a clean `feat/external-track-foundation` at
`a22cf6e6932e7e84e2f1d01858f5a06719544f1b`. Both complete logs (320 stderr and
2409 stdout lines) and the focused mesh scan were inspected. No game assets or
private logs are committed. The runtime ended normally; 40 stock-override messages
appear. Positive control captured 7831225 and replay-matched 7832579.

Nine BufferPath captures have no event overflow: five startup, four gameplay.
Every capture has seven vertex attribute views, including a separate stride-12
float XYZ input at location 0 and six views into a stride-44 buffer. This does not
establish their shader semantics or RPK identity.

| Draw | Phase | Indices | Location-0 guest VA / bytes | Stride-44 base / bytes | Upload events |
|---:|---|---:|---|---|---:|
| 1 | startup | 2784 | `50fbb36b00` / 262656 | `50fbb76d00` / 963072 | 0 |
| 2 | startup | 3162 | `50fb8815e0` / 446544 | `50fb8ee630` / 1637328 | 0 |
| 3 | startup | 252 | `50fb542150` / 447072 | `50fb5af3b0` / 1639264 | 1 |
| 4 | startup | 216 | `50fb2dd850` / 448812 | `50fb34b180` / 1645644 | 3 |
| 5 | startup | 216 | `50fb542150` / 447072 | `50fb5af3b0` / 1639264 | 0 |
| 6 | gameplay | 4500 | `50ffa3a000` / 216684 | `50ffa6ee70` / 794508 | 0 |
| 7 | gameplay | 543 | `50ff608a40` / 448008 | `50ff676050` / 1642696 | 0 |
| 8 | gameplay | 1329 | `50ff608a40` / 448008 | `50ff676050` / 1642696 | 0 |
| 9 | gameplay | 543 | `50ff3220e0` / 449388 | `50ff38fc50` / 1647756 | 0 |

Draw 4's location-0 request lies within the queued CPU-dirty upload
`0x50fb2dd000 + 454656`. Both identify the same arena token and consistent offsets:
`guest - arena_offset = 0x5000000000`. Its stride-44 request has two queued dirty
subranges, not one upload of the whole descriptor. Draw 3 merges adjacent inputs
into one acquisition; draw 5 reuses that guest range without another upload in its
capture. This is cache-operation evidence, not mesh ownership or GPU completion.

All nine index requests take the queued stream path. Gameplay vertex requests have
`obtain flags=0`, return arenas, and schedule no upload in these captures. The
GPU-modified `0x4004c3c000` / 1008-byte resource is **pixel-stage slot 1**, not a
vertex-buffer producer. Vertex-stage flags=1 mean fetch present and `uses_dma=false`;
the special resource is clip planes, not a BDA page table. No dynamic raw-memory
path is reported for this compiled vertex-stage variant. That does not rule out
processing by other shaders or native code.

The startup attempt limit appears **before** the two verified Road742 prefix reads
(`0xf0026e0a1c`, `0xf0026dfddf`). Therefore those captured startup uploads cannot prove
provenance from these reads. Equal shader hashes and an arena shared by many ranges
do not supply the missing link. None of the captured ranges is established as Road742.
The previous allowlist did not cover these location-0 stride-12 views; we do not
respond by repeating another geometry search.

## Why a debugger boundary

`src/runtime_cpu.c` executes x86-64 guest code directly; `guest_cpu_hook` is a stub
on this host. There is no general instruction observer to enable. The existing
`BB_GUEST_TRACE` in `runtime_services.c` requires already-known instruction addresses,
patches guest code with INT3 and changes protection. It cannot find an unknown first
consumer without choosing addresses. We leave it disabled.

`runtime_file.c::do_read/do_pread` calls RpkTrace immediately after a successful
host read. RpkTrace already verifies the header and actual prefix via backing reads.
This is the narrow reliable boundary before returning to the caller. The new option
stops there rather than polling stale addresses or changing memory protections.

`BB_RPK_DEBUG_READ=1` selects the first **fully header-and-prefix verified** observation;
`2` selects the second. Other values disable it. Selection is atomic and consumed
once per process, even when no debugger is attached. Partial headers, mismatched,
unreadable or uncovered prefixes cannot stop. It requires the existing valid v2
manifest and a diagnostics build. On non-Windows platforms it reports unsupported.

When a Windows debugger is already attached, the selected observation logs `STOP`,
prints `WATCH` to stderr and OutputDebugString, then calls **DebugBreak in host code**.
No guest-code breakpoint, hardware watchpoint, page guard, GPU fence, new exception
handler or buffer write is installed. The pause deliberately affects execution
timing. With no debugger it logs `skipped` and continues; it never deliberately
raises an unattended breakpoint. Keep the debugger attached throughout this test.

The stop occurs after prefix verification/logging, with no further diagnostic
prefix reads in that callback. Read results, errno and Windows LastError are
preserved by the stop helper. Existing BufferPath, RpkTrace and staging remain intact.

The WATCH line supplies two addresses:

* `guest`: this run's verified prefix virtual address, used by native guest accesses.
* `backing`: a read-only snapshot of the corresponding unprotected backing-view
  alias. Host upload readers use this alias in `runtime_memory_read_sparse` /
  `runtime_memory_read_backing`, so watching guest VA alone can miss them.

The alias query holds only the existing mapping-table read lock and releases it
before stopping. It reads no data, changes no mappings and returns zero for reserved,
unmapped, empty, overflowing or cross-region requests. Neither address is a lifetime
lease. Other mappings of the same physical bytes can still escape these two watches.

## Build in MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/read-boundary-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
bash tools/test_runtime_memory.sh 2>&1 | tee "$RunLog/memory.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Prepare and launch under WinDbg: PowerShell

Use a new directory and the original Forward RPK. This **does not repeat BufferPath**.
The small CMD launcher redirects the actual game's streams, while WinDbg's `-o`
option also debugs its child process. WinDbg must be installed locally; supply its
executable path (`WinDbgX.exe` or the x64 Windows SDK `windbg.exe`). This wrapper uses
ASCII paths and rejects CMD expansion characters rather than guessing escaping.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-read-boundary-$([guid]::NewGuid().ToString('N'))"
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
$Runtime=Join-Path $RuntimeDir "DriveClubGame.read-boundary-$([guid]::NewGuid().ToString('N')).exe"
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
  $env:BB_RPK_DEBUG_READ='1'
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
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','RpkDebugRead:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
Write-Host "Return the logs and metadata from: $Run"
```

Do not set BB_GUEST_TRACE, BB_GUEST_SAMPLE, BB_MESH_SEARCH, BB_BUFFER_PATH or copy
tracing for this experiment. The wrapper clears inherited BB settings only for
launch and restores them afterward. Existing files, saves and experiments remain
untouched. No graphics or geometry change is expected.

## At the debugger: capture ONE access, not an unlimited trace

1. At the normal initial loader break(s) for CMD and the game child, enter `g`.
   If normal handled access violations from the runtime's page tracking interrupt
   execution, `sxd av` lets its first-chance handlers run while still breaking on
   unhandled exceptions. This is a debugger filter, not a protection change. Do not
   suppress breakpoint or single-step events used by the data watchpoints.
   Select **Fraser Valley Road Circuit Forward** in the game. No gameplay trigger
   file is required. Do not mistake an initial loader break for the verified stop.
2. Stop when the debugger displays **`RpkDebugRead: WATCH ...`**, followed by the
   host DebugBreak exception. The same addresses appear in stderr after `STOP`.
   This occurs before the selected read returns. Symbols/PDBs are not required.
3. Record current process/thread and stack, and inspect existing breakpoints:

   ```text
   |
   ~.
   kv
   bl
   ```

4. Use **this run's** guest/backing addresses from WATCH, never an address from an
   earlier log. Replace the placeholders below. If IDs 0/1 are already occupied,
   choose free IDs and use those same IDs when clearing; preserve existing breakpoints.

   ```text
   r @$t0 = <guest-address>
   r @$t1 = <backing-address>
   ba0 r 1 @$t0
   ba1 r 1 @$t1
   bl
   g
   ```

   Skip the second `ba` when backing is zero or equal to guest. One-byte watches
   avoid alignment assumptions (the second verified prefix can be unaligned).
   On x86, `r` watches **reads and writes**, not reads alone. WinDbg manages hardware
   debug registers; these are data breakpoints, not software patches in game code.
5. On the **first hardware-watchpoint hit**, clear only the watchpoints just added
   before continuing, then collect the following bounded evidence:

   ```text
   bc 0
   bc 1
   ~.
   r
   ub @rip L12
   u @rip L4
   kv
   bl
   ```

   Omit `bc 1` if it was not installed. A data trap reports execution after the
   access (REP instructions need care); inspect preceding disassembly as well as
   RIP. `kv` may be incomplete because guest code lacks Windows unwind symbols;
   keep the register/disassembly evidence even if the call stack is incomplete.
   Record which watchpoint fired. A read can identify a consumer; a write may
   instead be scratch reuse. Host upload/validation reads are not automatically a
   native mesh-processing function. Registers can expose a copy's destination, but
   interpreting that requires the actual instruction and surrounding call path.
6. Enter `g`, then close the game normally. After the game/CMD child exits, close
   WinDbg so the PowerShell script can finish collecting artifacts. No second runtime stop occurs. If the
   target exits without a watchpoint hit, clear your watches and return that result;
   it is not proof of no consumer. To examine the second verified read, make a fresh
   experiment and change only `BB_RPK_DEBUG_READ` to `2` before launching. Do not
   automatically expand tracing beyond this one access.

If `selected=1 skipped: no attached Windows debugger` appears, the stop was consumed
and attaching afterward is too late. Restart under WinDbg; it will not unexpectedly
stop at a later address. `resumed ... stop budget exhausted` means the host debug
stop returned, not that a consumer was found. Debugger pauses change timing; a thread
could consume data concurrently between verification and the stop. The claim is
**first observed access after arming to the watched byte/aliases**, not necessarily
the first absolute consumer of the entire mesh or read buffer.

## Evidence and next decision

Return `logs/windbg.log`, `logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`,
`diagnostic-lines.txt`, `commit.txt`, `hashes.txt`, `inputs/preparation.json` and the
focused `inputs/mesh-scan.json`. Note the selected read ordinal, watched addresses,
watchpoint ID that fired and whether the instruction reads or writes. No bulk
memory dumps, game archives, binaries or shader dumps are needed.

A verified prefix plus an observed read/copy instruction gives a real resource-to-
consumer edge. A demonstrated destination and its subsequent lifetime/processing
must then be linked to a recorded draw's binding and index references. A shader hash,
matching pointer at another time, debug stop alone, or scratch overwrite does not
prove that complete chain. No rendering fix, Road742 draw identity, visible custom
geometry, collision or drivability is claimed.

## Validation

* Importer: 63 Python tests, native probes and Blender 4.3.2 checks passed.
* Modkit self-tests and ATRAC9 encoder checks passed.
* Resource diagnostics passed ON/OFF and under ASan/UBSan. New policy tests cover
  strict selection, verified-only eligibility, missing debugger, consumed one-shot
  behavior, second-read selection, concurrent observations and unchanged input.
  Debugger callbacks are mocked; these tests do not claim a Windows debugger stop.
* Real synthetic memory mappings passed with atomic and pthread locks, including
  UBSan. Alias tests cover reserved/unmapped/overflow/empty/cross-region requests,
  correspondence to actual bytes, remapping, unmapping and unchanged protection.
  The six memory-profile Python tests also passed.
* Linux runtime C syntax checks and LLVM-MinGW Windows x64 syntax checks passed
  ON/OFF. Observer tests and the memory test cross-compiled with warnings as errors.
  The initial Windows syntax command omitted the existing allocator's API-version
  defines; rerunning with `_WIN32_WINNT=0x0a00` and `NTDDI_VERSION=0x0a000006` passed.
* `git diff --check` passed. No remaining automated test failures.

Windows binaries were not executed here. Full Vulkan/runtime integration, the
WinDbg launch workflow, real DebugBreak delivery and the first-access watchpoint
remain **untested on Windows**. This environment lacks the full CMake/GPU build
setup. The workflow above is the required next runtime experiment.

Changed source: `src/runtime_rpk_debug.h` (new), `src/runtime_rpk_trace.h`,
`src/runtime_memory.c`, `src/runtime.h`. Tests: `tools/rpk_debug_test.c` (new),
`tools/rpk_trace_test.c`, `tools/rpk_copy_test.c`, `tools/runtime_memory_test.c`,
`tools/test_resource_trace.sh`. Documentation: this report and the resource-trace
latest-experiment link. No import pipeline or rendering behavior was changed.
