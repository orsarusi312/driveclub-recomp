# Road742: identify the caller of the initial eight-byte request

## Recovery and complete log analysis

Continued a clean `feat/external-track-foundation` at
`fab696863da854e2ed3bb40f766301246926555c`. The uploaded stdout has 1,696 lines,
stderr 76 lines, WinDbg 262,934 lines (mostly first-chance exceptions), and the
UTF-16 diagnostic summary 141 lines. Stdout reports 40 stock override messages and
normal termination. No second-chance exception is reported in the debugger log.
The summary duplicates wrapped lines from the full logs; it is not an independent
observation. Original game/experiment files are unchanged.

The actual second-read record is:

* resource context `0003a8908986f62b`, read offset **113877454**, size 65536;
* reader `0xf0026c3380`, buffer `0xf0026c33c0`, original XYZ prefix
  `0xf0026c980f` at buffer offset **0x644f**;
* underlying object **`0xf0026c31c0`** in this run. The prompt's `0xf0026a8710`
  is from an older run; heap addresses must not be mixed between experiments;
* logical position 113877462, eight bytes beyond the window start; available=0,
  cursor=0 when the underlying read is about to return;
* WinDbg thread **`5770.4438`**, `async_1`, at the selected stop.

Both aliases show the original bytes. Both vtables are now readable. The bounded
snapshot agrees with the debugger's `dq` output:

| Object's slot | Wrapper table 0x8015e0820 | Underlying table 0x8015e1c30 |
|---|---|---|
| +0x10 | 0x800591de0 | 0x8005a6dc0 |
| +0x18 | **0x800591e60** | **0x8005a6df0** |
| +0x30 | 0x800ea5d50 | 0x800ea5d50 |
| +0x38 | 0x80000eb90 | 0x8005a6eb0 |
| +0x58 | 0x800e70b10 | 0x80005de60 |

This validates the previous native-image metadata reader on Windows. These are
observed slot values, not recovered Evolution Studios symbols or source code.
Slot meanings depend on the object's interface. The displayed native code and
Recomp's available runtime source are different evidence sources.

After resume, a host read of the same RPK at **44925680** into the same 64 KiB
buffer returns 65536 bytes. The sample matches before and mismatches afterward;
backing mapping and returned-overlap checks pass. The first sample changes from
`ec91014522fd4242f4954744` to `001862802106186280f02939`. This is strong, bounded
refill-provenance evidence. Concurrent/uninstrumented writes are not excluded by
that observer, but repeating this same experiment is no longer the next step.

The subsequent 36,864-byte HLE copy is explicitly rejected because its source
prefix is stale. No destination-verification success is reported. It must not be
added to a Road742 vertex-transfer chain. No persistent geometry or GPU reference
has been established.

## Most promising call chain and unresolved distinction

Known native path (return sites, not guessed function symbols):

```text
unknown caller/parser
  -> buffered read 0x800591e60, initial requested length = 8
     -> underlying read 0x8005a6df0 via vtable +0x18
        -> Recomp HLE file read, native return 0x8005a6e2c
     <- refill return 0x800591ede
     -> byte-copy bridge at 0x800591f19, return 0x800591f1e
  <- buffered-read return to the unknown caller
```

The previous disassembly establishes both buffered copies and the large-read
bypass: when no buffered bytes remain and the request is at least capacity, the
underlying reader receives the caller's destination directly. Otherwise the
buffered path copies `min(remaining, available)` bytes from `buffer+cursor` and
advances destination/counters. The known first request is **8 bytes** starting at
the read window, far before the vertex prefix. It cannot consume those positions.

We have not recovered the caller above this generic reader. The `kv` output becomes
unreliable across native guest frames, so deeper entries cannot supply that caller
by themselves. The next experiment captures the first exact eight-byte copy while
the XYZ context is still fresh, returns to the known buffered-read frame, then uses
its established frame-pointer layout to inspect **one caller return**. This can
reveal whether the request checks a resource header, reads an index, or drives a
seek to another resource. None of those interpretations is asserted in advance.

Prefetching adjacent vertex bytes incidentally while parsing another record is a
plausible explanation. Inline readers, different aliases/threads, transformation
and other allocations remain possible. No negative HLE copy result proves absence
of consumption. There is no justification yet for changing more meshes.

## Implemented one-shot request observer

* `BB_RPK_REQUEST=2` snapshots the first eight bytes of the second fully verified
  read window. It uses the existing resource trace manifest and records the actual
  read-base/file offset, not a historical address.
* It inspects at most **4096 HLE copy calls** and selects only `source == read_base`,
  `length == 8`, nonempty/nonoverflowing destination and nonoverlapping ranges.
  It does not broaden existing vertex-copy validation or reinterpret partial XYZ.
* Before the actual existing memcpy/memmove, both the eight source bytes and every
  configured Road742 XYZ sample must still match. A stale/unreadable candidate
  consumes the one-shot without a transfer claim. Different aliases are not assumed
  equivalent and are not followed by this narrowly selected observer.
* After the copy, all eight destination bytes are compared against the captured
  source. Source/destination addresses, byte count, actual immediate guest return,
  before/after bytes and current XYZ freshness are logged. Data scope is explicitly
  **`read-start-request-not-vertex`**. Matching eight bytes proves only that small
  request's observed transfer, not mesh ownership or lifetime.
* `BB_RPK_DEBUG_REQUEST=1` causes one host DebugBreak **after** the eight-byte copy
  only when destination verifies, XYZ is still fresh, the caller is nonzero and a
  Windows debugger is attached. No debugger means one skipped stop, no delayed
  retry. Existing read, clear, refill and copy diagnostics remain intact.
* This uses existing copy begin/end plumbing. Caller capture occurs only for a
  valid diagnostic ticket. Atomics publish the one selected read and claim the
  single candidate/completion. Error states are restored. No guest write, code
  patch, extra copy, upload, forced synchronization or memory scan is added.

An eight-byte destination can be a native stack local below the native-image range.
The new `runtime_memory_debug_stack_read` accepts at most 128 bytes and **only the
calling thread's stack bounds**. Windows uses GetCurrentThreadStackLimits and the
existing VirtualQuery/ReadProcessMemory checks; guard/no-access pages are refused.
Linux queries the current pthread stack and uses OS-checked self reads. Other
thread stacks/host heaps are not a fallback. The general metadata reader keeps its
original address policy. Unsupported OS reads fail closed. Sampling is not an
atomic ownership lease; concurrent writers or identical-content reuse cannot be
excluded. The debugger pause changes timing.

## Build in MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/request-caller-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
bash tools/test_runtime_memory.sh 2>&1 | tee "$RunLog/memory.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Prepare and launch under WinDbg: PowerShell

Use a new directory and the original Forward RPK. This run stops after the first verified eight-byte request from the second read window, then follows two bounded native return sites. It does not repeat the refill observer.
The small CMD launcher redirects the actual game's streams, while WinDbg's `-o`
option also debugs its child process. WinDbg must be installed locally; supply its
executable path (`WinDbgX.exe` or the x64 Windows SDK `windbg.exe`). This wrapper uses
ASCII paths and rejects CMD expansion characters rather than guessing escaping.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-request-caller-$([guid]::NewGuid().ToString('N'))"
$Inputs="$Run\inputs"
python "$Repo\tools\track_import\prepare_resource_trace.py" `
  --dcmod "$Repo\out\dcmod.exe" `
  --source "$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk" `
  --dependency "$Repo\work\visual-recovery\global.rpk" `
  --dependency "$Repo\work\visual-recovery\worlds\canada_common.rpk" `
  --target '000862187dac760f_0_polyRoadShape742' --output $Inputs
if($LASTEXITCODE -ne 0) { throw 'Preparation failed' }
$RuntimeDir="$Repo\work\multistream-ea121055b3cd415e84ce2f65990c6063\runtime"
$Prep='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep'
$Update=Read-Host 'Existing ASCII Update junction under H:\DC-Track-Test\work\ascii-paths (--app0)'
$Base=Read-Host 'Existing ASCII Base junction under H:\DC-Track-Test\work\ascii-paths (--app0-base)'
$Debugger=Read-Host 'Full path to WinDbgX.exe or x64 windbg.exe'
# Close every game instance before copying the known-good user directory.
$KnownSave="$Repo\work\road742-reader-consumer-123c703077484a289814451bdea22d5e\user"
if(-not (Test-Path -LiteralPath $KnownSave -PathType Container)) {
  $KnownSave=Read-Host 'Full existing known-good --user directory (not only its PROFILE subfolder)'
}
$KnownSave=(Resolve-Path -LiteralPath $KnownSave).Path.TrimEnd('\')
if((Get-Item -LiteralPath $KnownSave).Attributes -band [IO.FileAttributes]::ReparsePoint) {
  throw 'Use the actual save directory rather than a reparse point'
}
if(@(Get-ChildItem -LiteralPath $KnownSave -Force -Recurse |
  Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
  throw 'Save tree contains links; select a plain copy of the known-good user directory'
}
if(-not @(Get-ChildItem -LiteralPath $KnownSave -Force -Recurse -File -Filter profile.sav).Count) {
  throw 'No profile.sav found: select the complete known-good user directory'
}
function Get-SaveHashes([string]$Root) {
  Get-ChildItem -LiteralPath $Root -Force -Recurse -File | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{ Relative=$_.FullName.Substring($Root.Length+1); Hash=(Get-FileHash -LiteralPath $_.FullName).Hash }
  }
}
$SaveBefore=@(Get-SaveHashes $KnownSave)
New-Item -ItemType Directory "$Run\user","$Run\logs" | Out-Null
Get-ChildItem -LiteralPath $KnownSave -Force | Copy-Item -Destination "$Run\user" -Recurse -Force
$CloneBefore=@(Get-SaveHashes "$Run\user")
if(Compare-Object $SaveBefore $CloneBefore -Property Relative,Hash) { throw 'Save clone verification failed' }
$SaveBefore | Export-Csv "$Run\save-source-before.csv" -NoTypeInformation

$Runtime=Join-Path $RuntimeDir "DriveClubGame.request-caller-$([guid]::NewGuid().ToString('N')).exe"
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
  $env:BB_RPK_REQUEST='2'
  $env:BB_RPK_DEBUG_REQUEST='1'
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
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','RpkRequest:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
$SaveAfter=@(Get-SaveHashes $KnownSave)
$SaveAfter | Export-Csv "$Run\save-source-after.csv" -NoTypeInformation
if(Compare-Object $SaveBefore $SaveAfter -Property Relative,Hash) { throw 'Original save changed; retain evidence and stop' }
Write-Host "Original save unchanged. Return logs and metadata from: $Run"
```

Do not set BB_RPK_DEBUG_READ, BB_RPK_READER, BB_RPK_READ_WATCH, BB_RPK_COPY_TRACE, BB_RPK_COPY_READ, BB_RPK_CLEAR_TRACE, BB_RPK_DEBUG_CLEAR, BB_GUEST_TRACE, BB_GUEST_SAMPLE, BB_MESH_SEARCH or BB_BUFFER_PATH for this experiment. The wrapper clears inherited BB settings only for
launch and restores them afterward. Existing files, saves and experiments remain
untouched. No graphics or geometry change is expected.

## WinDbg: capture the request's caller before buffer replacement

Use the script above with the original route and cloned known-good save. There is
**no read-boundary stop** in this run and no gameplay trigger file. At ordinary
loader breaks use `g`; `sxd av` lets the runtime handle page-tracking first-chance
faults while retaining unhandled exceptions. Do not disable breakpoint exceptions.

1. Wait for **`RpkRequest: STOP AFTER verified 8-byte copy`**. This is different
   from the completed refill experiment. Copy the current marker's values:

   ```text
   .lastevent
   ~.
   kv 8
   bl
   r @$t0 = <caller-return-from-current-STOP>
   r @$t1 = <destination-from-current-STOP>
   r @$t2 = <vertex-prefix-from-current-STOP>
   db @$t1 L8
   db @$t2 L80
   ub @$t0 L10
   u @$t0 L8
   u 0x800591e60 L8
   ```

   The expected native return is `0x800591f1e`, with destination advancement and
   remaining-count subtraction. The buffered-read entry must show `push rbp; mov
   rbp,rsp`. These code sites are specific to the tested guest image. If the marker
   reports another caller, or code differs/unreads, **collect the bounded disassembly
   and stop this procedure**; resume/exit normally rather than assuming that frame
   layout. An unexpected caller is useful evidence, not a reason to trace more.
2. With slot 0 free, set one hardware execution breakpoint at the current return:

   ```text
   ba0 e 1 @$t0
   g
   ```

   Use another free slot consistently if needed. Never install data watchpoints:
   the observer's own verification reads must not be confused with guest access.
3. On that return hit immediately clear the breakpoint:

   ```text
   bc 0
   .lastevent
   ~.
   r
   dq @rbp L2
   db @$t1 L8
   db @$t2 L80
   ```

   Check same thread as the host stop, R14=8, R12=8, and R15 equals the logged
   destination. This is before `add r15,r14`. If any check fails, collect the
   mismatch and exit the procedure without rearming. Do not use deeper `kv` frames
   as proof. The validated native prologue gives `[rbp+8]` as this buffered-read
   invocation's immediate return to its caller. Capture only that parent:

   ```text
   r @$t3 = poi(@rbp+8)
   ub @$t3 L18
   u @$t3 L30
   ```

   If the stack read fails or the return is zero/unmapped/not native guest code,
   stop with these logs. Backward disassembly is a heuristic; forward disassembly
   at a verified return boundary is the stronger evidence.
4. If the parent return is readable native code, use one final execution breakpoint:

   ```text
   ba0 e 1 @$t3
   g
   ```

   At the first hit remove it immediately and collect the parser's live state:

   ```text
   bc 0
   .lastevent
   ~.
   r
   db @$t1 L8
   db @$t2 L80
   ub @rip L18
   u @rip L40
   dq @rbp L2
   bl
   g
   ```

   Check the same thread again. Do not walk additional frames or repeatedly break
   on the reader. No more than one host stop and two execution hits are requested.
   If an unexpected exception interrupts, collect `.lastevent`, `r`, `kv 8` and
   stop the experiment. Clear only breakpoints you installed if ending early.
   Close the game normally, then WinDbg so the save comparison and log collection
   finish. No old heap address is valid for this run.

## Expected output and evidence

```text
RpkRequest: enabled selected=2 ...
RpkRequest: armed ... source=... file-offset=... bytes=8 ...
RpkRequest: AFTER ... bytes=8 caller-return=... destination=verified ... vertex-prefix-now=verified before=... after=...; scope=read-start-request-not-vertex
RpkRequest: STOP AFTER verified 8-byte copy ...
```

A mismatch, unreadable destination, stale XYZ, missing debugger or exhausted budget
is a valid negative outcome. No successful stop is guaranteed. A destination on
an unsupported alternate stack is rejected rather than dereferenced unsafely.
Do not expand budgets or substitute another mesh on a negative result.

The first-eight-byte values help identify what the parser requested. To compare
with the prepared stock RPK **after** the run, use PowerShell with `$Run` still set:

```powershell
$Arm=Select-String -Path "$Run\logs\stderr.log" -Pattern 'RpkRequest: armed' | Select-Object -First 1
if($Arm -and $Arm.Line -match 'file-offset=([0-9]+)') {
  $Offset=[long]$Matches[1]
  $File=[IO.File]::OpenRead("$Run\inputs\mods\worlds\canada_road_circuit_02_n.rpk")
  try {
    if($Offset -lt 0 -or $Offset -gt ($File.Length-8)) { throw 'Invalid logged offset' }
    [void]$File.Seek($Offset,[IO.SeekOrigin]::Begin)
    $Bytes=New-Object byte[] 8
    if($File.Read($Bytes,0,8) -ne 8) { throw 'Short local read' }
    "offset=$Offset bytes=$([BitConverter]::ToString($Bytes))" | Set-Content "$Run\request-file-bytes.txt"
  } finally { $File.Dispose() }
}
```

This reads eight bytes, not an archive export. Compare them with the runtime's
before/after bytes; do not assume they are a header until the native caller/code
supports that interpretation.

Return `logs/windbg.log`, `logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`,
`diagnostic-lines.txt`, `commit.txt`, `hashes.txt`, `inputs/preparation.json`, the
focused `inputs/mesh-scan.json`, `request-file-bytes.txt` if available, build/test
logs, and whether the original-save hash comparison passed. Record both execution
hits' thread IDs and any failed check. Keep save hash CSVs locally; do not upload
saves, archives, executables, shader binaries or bulk memory dumps.

The new milestone would be a verified **small request → its native caller** while
the vertex signature remains present. The caller's code should guide a precise
next observation of resource selection or vertex consumption, rather than following
changed bytes after refill. It is not yet a verified vertex transfer. Persistent
allocation ownership, GPU binding, custom rendering and collision remain unresolved.

## Validation

Verified locally before commit:

* `bash tools/track_import/test.sh`: passed; all 63 Python importer tests, native
  probe/resource tests and Blender 4.3.2 smoke, hierarchy, instance and winding
  checks passed. Blender warns that optional Draco compression is unavailable;
  these exports are uncompressed and their validation passed.
* `bash tools/modkit/test.sh`: passed, including the ATRAC9 encoder checks.
* `SANITIZE=1 bash tools/test_resource_trace.sh`: passed with AddressSanitizer and
  UndefinedBehaviorSanitizer in diagnostics ON/OFF configurations. The final
  request tests also passed separately under both sanitizers after adding malformed
  read-range and concurrent single-winner cases.
* `SANITIZE=1 bash tools/test_runtime_memory.sh`: both memory-lock modes and all six
  profile tests passed under UBSan. This sandbox returns ENOSYS for self
  `process_vm_readv`; successful native-image/stack reads cannot be exercised here.
  Their fail-closed behavior and guest-backing reads were tested. Windows must
  validate the actual current-thread-stack read path.
* LLVM-MinGW cross-compilation: request, copy, trace and read-watch harnesses compile
  for Windows with warnings as errors, diagnostics ON/OFF; the memory backend test
  also compiles. Runtime C integration syntax checks pass for Windows, and modified
  file/memory integration checks pass on Linux. Windows binaries were not executed.
* No Windows gameplay or full Vulkan diagnostics executable build was possible in
  this environment. The commands above perform that required integration check.

New implementation: `src/runtime_rpk_request.h`, `src/runtime_rpk_request_impl.h`.
Integration: `src/runtime.c`, `src/runtime.h`, `src/runtime_memory.c`,
`src/runtime_rpk_copy.h`, `src/runtime_rpk_copy_impl.h`, `src/runtime_rpk_trace.h`.
Regression coverage: `tools/rpk_request_test.c`, `tools/runtime_memory_test.c`,
`tools/test_resource_trace.sh`, plus the stack-reader mock in existing clear, copy,
read-watch and trace harnesses. Documentation: this guide and the resource-trace
index. No proprietary asset or user log is included in the commit.
