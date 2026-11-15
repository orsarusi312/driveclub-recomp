# Road742: second-read buffered consumer boundary

## Findings from the completed Windows experiment

Continued clean local and remote `feat/external-track-foundation` at
`4d499bedd7ad982ada9999e58816eb2f8b2391d0`. Analyzed the complete supplied logs:
73,108 debugger lines, 1,308 stdout lines and 65 stderr lines. There is one execution
breakpoint hit, no second-chance exception report, 40 Forward RPK override messages,
and stdout ends with `the process ended normally`. No previous work was discarded.

The Windows result validates the previous implementation: the first overlapping
HLE memset had freshly matching Road742 samples, the exact same 65,536-byte read
range, and value zero. Its actual guest return was `0x800591dc5`. The stop before
memset and the execution breakpoint after it both occurred on thread
`32dc.2f58` (`async_1`). The prefix bytes were intact before memset. This confirms
an observed **read-to-overwrite edge**, not a resource reader or rendering command.

The new disassembly permits a stronger explanation than the previous hypothesis:

| Native site | Observed operations | Supported interpretation |
|---|---|---|
| `0x800591d30` | Calls a factory-held object's virtual `+0x10`, checks returned object's `+0x58` predicate; can return it directly | Factory/wrapper selection, predicate semantics unknown |
| `0x800591d5d`–`0x800591dc5` | Calls a global service's virtual `+0x28` with size `0x10040`, receives RBX, initializes header, zeroes RBX+0x40 for 0x10000 bytes, returns RBX | Allocation-like request and initialization of a new buffered wrapper on this path; not an ordinary refill reset |
| `0x800591de0` | Handles origin-like argument 1, adjusts logical position; uses underlying `+0x30` and `+0x10`, can reposition within buffered range | Seek-like method; names inferred from data flow |
| `0x800591e60` | Accepts destination in RSI and length in RDX, advances `+0x10`, checks available bytes and capacity | Buffered read-like method |
| `0x800591ea7`–`0x800591ede` | Queries underlying `+0x30` / `+0x38`, computes min(capacity, difference), calls underlying `+0x18` with destination `[r13+0x30]` and count RBX | Concrete refill call and known return boundary |
| `0x80123e600` | Called with buffer, zero, 65536; runtime observer confirms guest_memset | Memset import/bridge target for this call; bridge body itself was not disassembled |

The allocation-like call returned object `0xf0026cfa30`; its inline buffer is
`0xf0026cfa70`. This equals the earlier read destination. It is strong evidence of
storage reuse during construction. We did **not** observe a free, allocator ownership,
or why the first read's prefix was not accessed by the old watchpoint. Do not
conclude that the resource was unused or discarded.

Virtual slot offsets are relative to each object's class: the factory's `+0x10`
call is not interchangeable with the underlying stream's seek-like `+0x10` call.
The factory writes wrapper vtable `0x8015e0820`. That address and code sites are
specific to the supplied guest image, not universal layouts for other versions.

The header established by the constructor and neighboring code is:

| Offset | Observed value/use | Confidence |
|---:|---|---|
| 0x00 | vtable pointer | Constructor store |
| 0x08 | underlying object | Constructor store and virtual dispatch |
| 0x10 | logical position | Incremented by read request; seek-like updates |
| 0x18 | buffered window start | Underlying `+0x30` return saved here |
| 0x20 | bytes available | Zero initialized and tested by read-like loop |
| 0x28 | cursor within buffer | Zero initialized; seek-like updates |
| 0x30 | buffer pointer = object+0x40 on this constructor path | Explicit constructor store |
| 0x38 | capacity = 65536 | Explicit constructor store and refill bound |

Semantic names are interpretations of the shown instructions, not recovered symbols.
The read-method tail after `0x800591ede` is **missing** from the supplied disassembly.
The branch to `0x800591f2e` when a request is at least the capacity suggests a bypass
path, but its body is not supplied. We do not guess a memcpy target or destination.
Windows stack unwinding after the native frame remains unreliable. The captured
return-site registers are stronger evidence than deeper `kv` entries.

## Smallest next diagnostic

Target **the second verified read**, after the now-understood construction event.
At its existing RpkDebugRead stop, optional `BB_RPK_READER=1` reads only:

* the 64-byte candidate header immediately before the actual read buffer;
* at most 96 bytes of its vtable;
* the underlying object's first 8 bytes and at most 96 bytes of its vtable.

Maximum additional backing-read requests: **264 bytes, four reads, once** per
selected debugger stop. Invalid read ranges, wrong buffer/capacity, impossible
cursor/available totals, null/unaligned/overflowing pointers and unreadable data
are reported. Invalid headers are not followed. Unreadable tables are individually
marked; their values are not used as call targets. No function is invoked through
these pointers. No code patch, allocation, protection change, GPU operation or
synchronization is introduced.

`status=layout-consistent` means only that this candidate fits the observed schema.
It is not a type check or lifetime lease. Snapshots can race other threads. A
non-inline buffer or different reader class may reject; no fallback scan occurs.
The option is disabled by default and compiled behind diagnostics gating, attached
to the **same selected stop**, avoiding separate ordinal races. Existing read/clear/
copy/BufferPath functionality remains available. The enclosing stop wrapper restores
errno and Windows LastError. New metadata reads occur before WATCH/DebugBreak, so
watchpoints armed afterward do not trap this snapshot itself.

At the native refill return, R13/R15/R12/RBX are candidates for reader object,
current destination, remaining request and refill size, based on the displayed
prologue and instructions. Their **live values** and the missing function tail
must be captured. R15 is not automatically the final Road742 destination: this
request may cover only a header or small range, and later calls may copy the prefix.
A one-access watch after refill targets the native consumer of the selected prefix.
Do not treat the shader pair, a pointer match or this snapshot as mesh ownership.

## Build in MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/reader-consumer-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
bash tools/test_runtime_memory.sh 2>&1 | tee "$RunLog/memory.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Prepare and launch under WinDbg: PowerShell

Use a new directory and the original Forward RPK. This run targets the second verified file read and the native refill return. It does not repeat the successful pre-clear experiment.
The small CMD launcher redirects the actual game's streams, while WinDbg's `-o`
option also debugs its child process. WinDbg must be installed locally; supply its
executable path (`WinDbgX.exe` or the x64 Windows SDK `windbg.exe`). This wrapper uses
ASCII paths and rejects CMD expansion characters rather than guessing escaping.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-reader-consumer-$([guid]::NewGuid().ToString('N'))"
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
$Runtime=Join-Path $RuntimeDir "DriveClubGame.reader-consumer-$([guid]::NewGuid().ToString('N')).exe"
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
  $env:BB_RPK_DEBUG_READ='2'
  $env:BB_RPK_READER='1'
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
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','RpkDebugRead:','RpkReader:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
Write-Host "Return the logs and metadata from: $Run"
```

Do not set BB_RPK_CLEAR_TRACE, BB_RPK_DEBUG_CLEAR, BB_GUEST_TRACE, BB_GUEST_SAMPLE, BB_MESH_SEARCH, BB_BUFFER_PATH or copy
tracing for this experiment. The wrapper clears inherited BB settings only for
launch and restores them afterward. Existing files, saves and experiments remain
untouched. No graphics or geometry change is expected.

## WinDbg steps: second read, refill return, one data access

Use a fresh process under WinDbg. The script clears inherited BB settings and
selects read ordinal 2. No gameplay trigger file is needed. Choose **Fraser Valley
Road Circuit Forward**. At ordinary loader breaks use `g`; `sxd av` allows the
runtime's handled page-tracking faults to run while retaining unhandled-exception
stops. Do not suppress breakpoint/single-step exceptions. Do not arm old addresses.

1. At `RpkDebugRead: WATCH` / host DebugBreak, stderr should contain
   `STOP selected=2`, `RpkReader: ... status=...`, and table lines when consistent.
   Record the current process/thread and use current-run values:

   ```text
   .lastevent
   ~.
   kv 8
   bl
   r @$t0 = <guest-prefix-from-current-WATCH>
   r @$t1 = <backing-alias-from-current-WATCH>
   r @$t2 = <read-base-from-current-STOP>
   db @$t0 L80
   u 0x800591edb L2
   u 0x800591e60 L80
   ```

   These code addresses are from the supplied native image; verify the first `u`
   shows `call qword ptr [rax+18h]` at `0x800591edb` and the cursor-zero store at
   `0x800591ede`. If they differ or are unreadable, **stop this procedure**, return
   the logs and do not set a guessed breakpoint. The second `u` captures the missing
   tail starting at an established function entry; do not infer functions from
   bytes printed beyond its return. Do not use historical heap addresses.

2. If breakpoint slot 0 is free, arm exactly one hardware execution breakpoint
   at the verified refill-return instruction. Use another free slot consistently
   if occupied; preserve existing breakpoints:

   ```text
   ba0 e 1 0x800591ede
   g
   ```

3. On that execution hit, immediately remove it and inspect:

   ```text
   bc 0
   .lastevent
   ~.
   r
   dq @r13 L8
   db @$t0 L80
   u @rip L50
   kv 8
   bl
   ```

   Check that this is the same thread as the selected read, R13 equals the candidate
   object when a consistent snapshot was reported, and `[r13+0x30]` equals `$t2`.
   Compare the prefix bytes with the earlier view. If these checks fail, report the
   mismatch and resume normally without attributing this return to the target read.
   The breakpoint is process-wide and could catch an unrelated call on another thread.
   Do not silently rearm it. R15/R12 describe only this invocation; do not assume
   they describe a whole-resource transfer.

4. If the context and prefix still match, arm one-byte read/write watches on the
   current prefix aliases, **after** the refill return:

   ```text
   ba0 r 1 @$t0
   ba1 r 1 @$t1
   g
   ```

   Skip slot 1 if backing is zero or equal to guest; use free slots if occupied.
   On x86 `r` includes writes. The next stop can be a consumer read, a diagnostic/
   runtime read or another overwrite. On the **first hit**, remove only these
   watchpoints and collect bounded evidence:

   ```text
   bc 0
   bc 1
   .lastevent
   ~.
   r
   ub @rip L20
   u @rip L10
   kv 8
   dq @rsp L10
   bl
   g
   ```

   Omit `bc 1` if not installed. Data traps report after-access execution; REP
   operations require instruction/register context. Do not label a memcpy destination
   from a register without the actual instruction and range calculation. Do not
   leave watches running or single-step unboundedly. This experiment has at most
   one host stop, one refill-return hit, and one data-watch hit (apart from ordinary
   loader/exception stops). Clear its breakpoint(s) if ending early. Close the game
   normally, then close WinDbg to let the PowerShell collector finish.

## Expected markers, decision and returned evidence

```text
RpkDebugRead: STOP selected=2 ...
RpkReader: ... candidate=... status=layout-consistent ...
RpkReader: vtable=... underlying=... logical=... window-start=... available=... cursor=... buffer=... capacity=65536
RpkReader: wrapper ... table=read +10=... +18=... +30=... +38=... +58=...
RpkReader: underlying ... table=read ...
RpkDebugRead: WATCH ...
```

A rejected layout or unreadable table is valid diagnostic output, not permission
to bypass the checks. If no second verified observation occurs, there is no stop;
return the logs. No geometry or visible change is expected.

Return `logs/windbg.log`, `logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`,
`diagnostic-lines.txt`, `commit.txt`, `hashes.txt`, `inputs/preparation.json`, the
focused `inputs/mesh-scan.json` and build/test logs. Note both thread IDs, which
watchpoint hit, and any failed context check. Do not upload archives, executables,
full memory dumps or shader binaries. Keep native disassembly/data snippets private
to this investigation.

A demonstrated read of the still-verified prefix at a native copy/decode instruction
identifies a consumer. A source range, destination range and matching destination
bytes after completion are needed to prove transfer. Persistence then requires
following ownership/reuse rather than assuming an address remains live; only then
can BufferPath binding evidence link it to a recorded draw. **None of those later
edges has been established by the current logs.** The missing tail and live context
are the next evidence needed to instrument a copy precisely. No custom rendering,
Road742 draw identity, collision or drivability is claimed.

## Validation

* `bash tools/track_import/test.sh`: **63 Python tests passed**, native mesh,
  resource and track probes passed, Blender **4.3.2** smoke/hierarchy/instance/
  reflected-winding checks passed.
* `bash tools/modkit/test.sh`: synthetic self-tests and ATRAC9 encoder checks passed.
* `SANITIZE=1 bash tools/test_resource_trace.sh`: resource diagnostics, existing
  clear/copy/search/BufferPath tests and new reader tests passed ON/OFF under
  AddressSanitizer and UndefinedBehaviorSanitizer.
* `SANITIZE=1 bash tools/test_runtime_memory.sh`: real synthetic mappings passed
  under atomic and pthread locks; six memory-profile tests passed under UBSan.
* New tests cover the 264-byte/four-read cap, malformed layout, cursor and pointer
  overflow, invalid buffer/capacity, unreadable headers/tables/underlying object,
  pre-refill cursor state, little-endian slots, unchanged bytes, default/OFF gates,
  and coupling to only the second verified debugger observation. Stops are mocked.
* Reader, debugger, trace and copy test executables cross-compiled for Windows x64
  ON/OFF with warnings as errors. Linux/LLVM-MinGW runtime-file syntax checks and
  Windows runtime.c syntax checks passed ON/OFF. `git diff --check` passed.

No automated failures remain. Local logs: `work/rpk-reader-validation/`.
No proprietary assets or private logs are committed. Full Vulkan/game building and
Windows execution are unavailable here; the new snapshot and debugger procedure
still require the Windows test above. The previous before-clear implementation
**has** now been validated by the supplied real Windows logs. The existing separate
real-memset wrapper harness remains Linux-only; Windows observer tests still run.

Changed files: `src/runtime_rpk_reader.h` (new), `src/runtime_rpk_debug.h`,
`tools/rpk_reader_test.c` (new), `tools/rpk_debug_test.c`,
`tools/test_resource_trace.sh`, this report, and the latest-experiment link in
`external-track-resource-trace.md`. No importer, allocator, resource upload, vehicle
physics or rendering behavior was changed.
