# Road742: bracket refills and verify selected-read copies

## Recovered work and latest evidence

Continued `feat/external-track-foundation` at `e1d022329df891f942f8c8d1ee04a8911dd44a22`.
Recovered the unfinished safe metadata reader, host-read observer, narrowed copy
seeding and tests. Finished validation and this guide without resetting the branch
or changing any earlier experiment. No game assets or private logs are committed.

Reviewed all uploaded logs and metadata: WinDbg 66,293 lines, stdout 1,388 lines,
stderr 75 lines. The uploaded commit matches the starting HEAD. `preparation.json`
and the RPK hash both identify the byte-identical stock copy (SHA256 beginning
`1d7822ef18458025`). The executable hash is recorded by the user, not independently
recomputed here. Stdout reports normal termination. The debugger has two breakpoint-0
hits and no second-chance exception report.

Known event order, with historical addresses shown only to explain the evidence:

1. The second read starts at file offset **113877454**, writes 65536 bytes to
   `0xf0026a9360`, and contains the prefix at `0xf0026af7af` (offset `0x644f`).
   Both prefix and resource header verify. Reader header is consistent with the
   native constructor; its initial logical position is 113877462 (8 bytes ahead),
   window start 113877454, available=0, cursor=0. The initial request is only **8 bytes**.
2. At native refill return `0x800591ede`, thread `3478.1d34` is the same thread,
   R13 equals the candidate object, R12=8 and RBX=65536. Both aliases still show the
   original XYZ bytes after this return. Therefore the resource was still present
   at this point, even though this request does not reach its distant prefix.
3. Later on that thread, a buffered-reader memcpy reads from `buffer+0x2e` with
   size `0x9000` and destination `0x50a8b84700`. A data watch stops in REP MOVS.
   This is not the original 8-byte request. The source cursor alone does not tell
   us how many refills/seeks/reinitializations occurred between the two stops.
4. At the REP stop, progress is `0x9000 - 0x2b80 = 0x6480`. The old prefix lies
   `0x6421` bytes after this copy's source, corresponding to destination
   `0x50a8b8ab21`. The displayed 64 bytes there equal the current source bytes.
   But those bytes start `00 18 62 80 21 06 18 62 80 F0 29 39`, not the original
   `EC 91 01 45 22 FD 42 42 F4 95 47 44`. The entire 36,864-byte copy has not
   completed, and even the original eight strided XYZ samples span beyond the
   copied region at this REP stop. Matching the displayed fragment proves neither
   a full completed copy nor transfer of the original Road742 signature.

The overwrite/remapping event between (2) and (3) was not observed. Hardware data
breakpoints are per-virtual-address/per-thread mechanisms managed by the debugger;
a REP trap is an in-progress instruction observation, not a complete copy boundary.
Kernel-mediated file reads, another physical alias, mapping reuse or another writer
may escape the chosen observation. In particular, runtime `host_read`/`pread` can
replace a buffer through OS file I/O. That is a **testable hypothesis**, not a
conclusion that a refill caused this change.

## Native reader now understood

The newly supplied tail establishes these operations:

* `0x800591e60`: buffered read; logical position advances by the request length.
* If no bytes are available and the remaining request is smaller than capacity,
  the method computes `min(capacity, underlying_length - underlying_position)` and
  reads into the internal buffer through underlying vtable `+0x18`.
* `0x800591ede`: resets cursor; sets available to the refill count.
* `0x800591ef4` onward: computes `min(request_remaining, available)`, takes source
  `buffer + cursor`, updates available/cursor, and calls the memcpy bridge
  `0x80123e3b0` at `0x800591f19`. Return site is `0x800591f1e`; it advances destination
  and subtracts copied length, looping while the request remains.
* `0x800591f2e`: when available is zero and the request is at least capacity,
  calls the underlying read directly with the caller's destination and remaining
  size, bypassing the internal buffer. It returns the underlying result.

This identifies a generic byte consumer and two transfer paths. No native resource
parser, mesh allocation owner, persistent geometry or Road742 draw is established.

## Why the vtable snapshots failed

`runtime_low_map` allocates image/stack/trampoline memory in
`[0x800000000, 0x1000000000)` with VirtualAlloc on Windows. Those mappings are not
entries in the physical-pool VMA table. The old snapshot called
`runtime_memory_read_backing`, which only translates pool VMAs. Both reported
vtables (`0x8015e0820`, `0x8015e1c30`) lie in the low native-image range. Thus this
API cannot read them; its failure does **not** demonstrate corrupt vtables.

New `runtime_memory_debug_read` is separate from the upload/backing reader. It
rejects zero/overflowing/over-4096-byte requests, keeps pool reads through the
existing backing translation, and uses an OS-checked read for low native mappings.
Windows checks committed/readable regions with VirtualQuery, rejects guard/no-access
pages, and uses ReadProcessMemory on this process with an exact-byte result check.
It never directly dereferences an unvalidated native pointer or changes protection.
Linux uses self `process_vm_readv` and returns failure if unavailable. High host
addresses and the backing view itself are not treated as guest addresses.
Reader metadata remains limited to 264 bytes at the one selected debugger stop.

## New bounded observation

`BB_RPK_READ_WATCH=2` arms on the second fully verified resource read. It considers
at most 4096 subsequent host read/pread calls and claims only the first whose
requested destination overlaps a sampled XYZ byte, either at the same guest VA or
through a recognized single-VMA physical alias. It verifies the original signature
before and after that existing OS read, records destination/requested/returned bytes,
file path and offset, and prints the first sampled row before/after.

`changed-across-read=1` requires fresh matching samples before the call, a real byte
mismatch afterward, the same backing alias snapshot and returned bytes overlapping
at least one sample. Unreadability is not a mismatch. Failed/short reads that do not
reach samples, already-stale sources or remapping cannot get that classification.
This is a temporal observation around one read; concurrent writers are not excluded.
Read offsets from `read` are observed file positions and can race shared-file cursor
users; pread offsets are explicit. Other-file `read` offsets may be -1 (not observed).
It is not a global first-write recorder: inline/other-API writes, multi-VMA aliases,
unknown mappings and concurrent ownership changes remain uncovered.

`BB_RPK_COPY_TRACE=1` with new `BB_RPK_COPY_READ=2` seeds existing copy tracing only
from the second fully verified read. Verified destinations may still propagate
within the existing 16-address / 64-candidate / 262144-call budgets. It now logs
actual source/destination bases, total copy size and immediate guest return address.
Only fresh whole-prefix source verification followed by successful destination
verification supports a matching signature transfer. `matched-prefix-first` displays
the common first sample when verified; it is not a claim about every byte copied.
Small partial-window copies remain unproven. Invalid ordinal settings disable
copy tracing rather than silently broadening it. Without the new selector, existing
copy behavior is preserved. All observers preserve errno/Windows LastError.

No extra game-file I/O, guest copy/write, protection change, file offset change,
GPU fence or broad memory scan is performed. Existing OS calls are bracketed; metadata
and sampled bytes are read. State publication/claiming is atomic; snapshots do not
freeze other writers or grant allocation ownership. The new options are off by
default. Do not install data watchpoints in the new experiment: observer sampling
could itself trigger them and obscure the native consumer.

## Build in MSYS2 CLANG64

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/refill-provenance-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
bash tools/test_runtime_memory.sh 2>&1 | tee "$RunLog/memory.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

## Prepare and launch under WinDbg: PowerShell

Use a new directory and the original Forward RPK. This run brackets subsequent host reads and verifies HLE copies from only the second verified resource read. It uses one existing host debugger stop and no hardware watchpoints.
The small CMD launcher redirects the actual game's streams, while WinDbg's `-o`
option also debugs its child process. WinDbg must be installed locally; supply its
executable path (`WinDbgX.exe` or the x64 Windows SDK `windbg.exe`). This wrapper uses
ASCII paths and rejects CMD expansion characters rather than guessing escaping.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\road742-refill-provenance-$([guid]::NewGuid().ToString('N'))"
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

$Runtime=Join-Path $RuntimeDir "DriveClubGame.refill-provenance-$([guid]::NewGuid().ToString('N')).exe"
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
  $env:BB_RPK_READ_WATCH='2'
  $env:BB_RPK_COPY_TRACE='1'
  $env:BB_RPK_COPY_READ='2'
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
Select-String -Path "$Run\logs\*.log" -Pattern 'RpkTrace:','RpkDebugRead:','RpkReader:','RpkReadWatch:','RpkCopy:','replaced by mods folder' |
  Out-File "$Run\diagnostic-lines.txt"
$SaveAfter=@(Get-SaveHashes $KnownSave)
$SaveAfter | Export-Csv "$Run\save-source-after.csv" -NoTypeInformation
if(Compare-Object $SaveBefore $SaveAfter -Property Relative,Hash) { throw 'Original save changed; retain evidence and stop' }
Write-Host "Original save unchanged. Return logs and metadata from: $Run"
```

Do not set BB_RPK_CLEAR_TRACE, BB_RPK_DEBUG_CLEAR, BB_GUEST_TRACE, BB_GUEST_SAMPLE, BB_MESH_SEARCH or BB_BUFFER_PATH for this experiment. The wrapper clears inherited BB settings only for
launch and restores them afterward. Existing files, saves and experiments remain
untouched. No graphics or geometry change is expected.

## WinDbg: one host stop, no data or execution watchpoints

1. Start the fresh process using the script above. At loader breaks use `g`.
   Use `sxd av` for the runtime's handled page-tracking faults; leave unhandled
   exceptions and breakpoint events visible. Choose Fraser Valley → Road Circuit →
   Forward using the cloned known-good save. There is no gameplay trigger file.
2. At **STOP selected=2 / WATCH**, collect the following. Use only this run's
   addresses, and use the vtable printed by the snapshot, not a historical heap VA:

   ```text
   .lastevent
   ~.
   r
   kv 8
   bl
   db <current-guest-prefix> L80
   db <current-backing-prefix> L80
   dq <current-wrapper-vtable> Lc
   dq <current-underlying-vtable> Lc
   g
   ```

   Skip unreadable/zero pointer inspections and retain the error. Expect the reader
   table to become readable if the mapping/protection checks succeed. `bl` should
   have no watchpoints installed by this procedure. Do **not** run the preceding
   experiment's `ba` commands; this test captures completed host operations in code.
   If unrelated breakpoints are already present, preserve them and document them;
   prefer the fresh debugger process specified above.
3. Drive the same route, then exit normally. ReadWatch has at most one overlapping
   read observation and never installs a breakpoint. Copy tracing has fixed limits;
   do not increase them or repeatedly stop on native memcpy. On an unexpected
   unhandled exception, collect `.lastevent`, `r`, `kv 8` and the logs, then stop the
   experiment. No game-memory patch or debugger breakpoint cleanup is required by
   this procedure. Close WinDbg so the wrapper records the exit code and save check.

Expected markers in **stderr**, separate from stdout override messages:

```text
RpkReadWatch: enabled selected=2 ...
RpkCopy: initial seed restricted to verified read 2
RpkReadWatch: armed selected=2 ...
RpkReader: wrapper ... table=read ...
RpkReader: underlying ... table=read ...
RpkReadWatch: AFTER ... before-match=1 after-match=0 after-status=prefix-mismatch ... mapping-same=1 returned-overlap=1 ... changed-across-read=1
```

The last line is a hypothesis to test, not an expected guaranteed outcome. It would
locate signature loss across a specific host read with an actual file path/range.
`before-match=0` instead says the earlier change remains outside this read boundary.
A budget-exhausted or absent observation is inconclusive. Keep all outcomes.

A useful copy result is:

```text
RpkCopy: ... source-before=verified ... destination=verified ... source-base=... destination-base=... copy-bytes=... caller=... matched-prefix-first=...
```

Field order in the actual line may differ. A rejected source or mismatching
post-copy destination is **not** a Road742 transfer. A matching post-copy signature
is a sampled data-transfer edge only; destination persistence and GPU use still
need separate evidence. If both refill and copy events occur, preserve their order
and addresses instead of assuming they describe the same lifetime.

Return `logs/windbg.log`, `logs/stdout.log`, `logs/stderr.log`, `logs/exit-code.txt`,
`diagnostic-lines.txt`, `commit.txt`, `hashes.txt`, `inputs/preparation.json`, the
focused `inputs/mesh-scan.json`, build/test logs and the result of the save-source
comparison. Keep the save hash CSVs locally for comparison; **do not upload saves,
archives, binaries or bulk memory dumps**. Mention any rejected vtable, budget limit
or unexpected debugger stop. Original game files and the known-good save remain
untouched; only the fresh save clone is used by the game.

## Validation

* Importer: **63 Python tests passed**, native probes and Blender **4.3.2**
  smoke/hierarchy/instances/reflected-winding checks passed.
* Modkit synthetic checks and ATRAC9 encoder checks passed.
* Resource diagnostics passed ASan/UBSan with diagnostics ON/OFF. New tests cover
  selected-read publication, actual trace-callback seeding, partial/padding ranges,
  physical aliases, remapping, stale/unreadable data, failed/short/oversized returned
  counts, preserved errors, default/OFF gates and fixed budgets. Copy tests verify
  selected ordinal, complete original ranges, unchanged memory and fresh matching
  destinations. Existing clear/search/BufferPath tests remain intact.
* Memory regressions passed with UBSan under atomic and pthread locks; six memory
  profile tests passed. New metadata tests cover pool translation, native mapping
  separation, malformed ranges, null output, no-access/unmapped regions and error
  preservation. **Native-low successful reads were skipped on Linux**: this
  environment returns ENOSYS for self process_vm_readv. The first attempted assertion
  exposed that limitation; the test now explicitly reports the skip and checks the
  fail-closed result. Production code has no unsafe fallback for this condition.
* Windows x64 observer executables and the real memory test cross-compiled with
  warnings as errors. Runtime C syntax checks passed ON/OFF on Linux and LLVM-MinGW.
  The Windows VirtualQuery/ReadProcessMemory success path is **not executed here**.
* Final resource sanitizer run and cross-compilation completed with exit 0;
  the final added trace-seeding assertion also passed ASan/UBSan and cross-compilation.
  `git diff --check` passed. No remaining automated failure; the above platform skip
  and Windows integration are explicit limitations.

Validation logs are preserved locally in `work/rpk-refill-validation/`. Full
Vulkan/game diagnostics builds and gameplay cannot run in this environment. The
Windows commands above are required to validate vtable reads, refill observations
and the fresh-save launch. Neither tests nor cross-compilation prove native Road742
transfer or visible geometry.

Changed source: `src/runtime_memory.c`, `src/runtime.h`, `src/runtime_file.c`,
`src/runtime.c`, `src/runtime_rpk_debug.h`, `src/runtime_rpk_trace.h`,
`src/runtime_rpk_copy.h`, `src/runtime_rpk_copy_impl.h`, and new
`src/runtime_rpk_read_watch.h`. Tests: new `tools/rpk_read_watch_test.c`,
`tools/runtime_memory_test.c`, `tools/rpk_copy_test.c`, `tools/rpk_debug_test.c`,
`tools/rpk_reader_test.c`, `tools/rpk_trace_test.c`, `tools/rpk_clear_test.c`, and
`tools/test_resource_trace.sh`. Documentation: this report and the latest-experiment
link in `external-track-resource-trace.md`. Importer geometry, native game assets,
original saves, physics and rendering behavior remain unchanged.
