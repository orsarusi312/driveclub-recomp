# Startup memory fault at guest +0x67c2ed

## Finding and scope

The supplied Windows log from commit `d74b763` establishes a **memory patch / direct-pool
budget mismatch** in the direct CLI launch:

* The guest reports `DIRECT MEMORY SIZE = 5056 MB`.
* `runtime_memory.c` line 491 in that commit rejects an allocation with `0x80020023`
  (`NO_SPACE`), before any successful direct allocation. That branch rejects a
  request whose lower bound or size exceeds the available physical range.
* The guest then declares `VideoPrivate` at `[0x5000000000,0x5180000000)`:
  **6144 MiB**, already larger than the entire configured direct pool.
* The successful direct maps in the log are at `0x2000000000` and `0xa0000000000`;
  there is no successful direct map of VideoPrivate.
* The later write of `8` at `0x513b600000` faults in heap initialization. That address
  is **inside** VideoPrivate, at offset 5046 MiB. Its coincidence with a subheap
  boundary does not make it invalid by itself. Headerless subheap declarations
  are not evidence that their underlying pages were committed.
* No draw or dispatch has been recorded. Vulkan initialization is not the failed
  operation in this log. Disabling implicit layers did not change the failure.

The bundled **Memory for up to 1440p** patch changes the VideoPrivate size to
`0x180000000` (6144 MiB). The normal launcher selects that patch **and** sets
`BB_DMEM_MB=8056`. Copying its prepared `patches.bin` into a direct-launch workflow
does not carry the environment variable with it. The minimal launch correction
for the supplied log is `$env:BB_DMEM_MB='8056'` before starting the runtime.
This is the launcher's tested budget, not a claim that 8056 is the mathematical
minimum for every custom configuration.

The original failing run did not capture `VirtualQuery`; its exact host state
is therefore **not measured**. The source-backed expectation after the refused
allocation is a reserved placeholder, not a committed VideoPrivate section.
The new reporter can distinguish that from committed read-only/GPU-protected
pages if another access violation occurs. Windows startup after the launch
correction remains **unverified until retested**.

## Source evidence and release comparison

* `src/runtime_memory.c`: `direct_allocate_impl` checks the physical budget;
  `runtime_memory_reserve` reserves the guest virtual range, and `host_map` /
  `view_map` replace placeholders with committed section views. Reservation
  alone does not supply writable storage. The CPU uses the guest VA directly;
  the backing alias is `backing_base + phys + (VA - vma.start)`.
* `patches/DriveClub.xml`: the three `Memory for up to ...` entries. The XML
  addresses have base `0x400000`; `launcher/prepare.cpp` subtracts it when writing
  `BBPATCH2` offsets. Both redirections and both patch bodies matter.
* `launcher/driveclub_launcher.cpp`: `MemoryTier`, `SaveSettings`'s patch selection,
  and the `BB_DMEM_MB` assignment before process creation. Budgets are 5056
  (default), 8056 (1440p), 10056 (4K), 17056 (8K) MiB.
* The pre-change `src/runtime_memory.c` is byte-for-byte identical to tag `v0.4.0`
  (`3e7f6d8543f8a426fdbbb323cf822f36b5c3a11a`). `build.sh` is identical too.
  The development branch contains other runtime changes, so this is not a claim
  that the entire release and development runtime are identical.
* `build.sh` defaults to RelWithDebInfo, PGO off, LTO off; diagnostics uses a
  separate build directory and `BB_DIAGNOSTICS=ON`. `packaging/package.sh` enables
  PGO/LTO when the recorded profile exists. The uploaded CMake excerpt confirms
  diagnostics ON and RelWithDebInfo, but omits the actual PGO/LTO settings.
  These build differences do not supply missing guest physical memory.
* The launcher also supplies `BB_CONFIG`, `BB_TOGGLE_FILE`, `BB_GPU_USER_DIR`,
  `BB_CRASH_DIR`, graphics/upscaler/frame pacing/input settings, provider paths,
  and a working directory containing runtime resources/DLLs. CLI `--user`
  alone is not a complete copy of this environment. The isolated test below
  sets the required paths and uses the existing runtime directory.

## Changes

1. `src/runtime_memory_profile.h` recognizes only the **complete, exact** four
   writes for each bundled memory patch in the final patched image. `probe.c`
   refuses guest entry when the known VideoPrivate request alone exceeds the
   entire direct pool. It recommends the paired launcher budget. Custom budgets
   above that proven lower bound are permitted; passing is not a guarantee that
   all other heaps fit.
   It never changes the budget, protection, patch file, or original game assets.
   Unknown/partial/custom patch layouts are not classified; this is not a
   general validator of third-party memory patches.
2. Fatal Windows access violations now report neighboring guest VMAs, host
   section/placeholder records, physical offsets, and native `VirtualQuery`
   results at address-1, address, and address+7. The table snapshot uses try-locks,
   declines if busy/uninitialized, and never reads guest bytes. Native queries
   are independent samples, not an atomic snapshot with the tables. Symbols
   and WinDbg `!address` are not required.
3. Optional `BB_MEMORY_TRACE=1` records direct allocation/map inputs and results,
   including failed requests and batch direct maps. It is not an exhaustive
   trace of every protection/flexible-memory operation.
4. `vma_erase` skips an empty erase. UBSan demonstrated a null-pointer, zero-length
   `memmove` on the first mapping before the VMA array existed. This separate
   defect is fixed; it is **not attributed to the reported guest write fault**.

Native query values: state `0x1000` = MEM_COMMIT, `0x2000` = MEM_RESERVE;
protection `0x1` = PAGE_NOACCESS, `0x2` = PAGE_READONLY, `0x4` = PAGE_READWRITE.
For reserved/free pages, protection values are not proof of committed storage.
Guest protection can differ from native protection during GPU tracking.

## Verification completed in Linux

* Real synthetic mappings at `0x513b600000`: boundary writes, section split/remap,
  backing alias reads, preserved neighbors, holes, overlap refusal, guest/GPU
  protection separation, repeated loading, busy diagnostic snapshots. Both
  memory-lock modes pass, including UBSan. The oversized 6144/5056 MiB request
  is rejected without attempting the invalid write.
* Six Python tests build independent synthetic image fixtures from the public
  patch XML: all three profiles, exact/larger/insufficient budgets, every missing
  or corrupted write, truncated images, and final-write selection. Pass normally,
  with UBSan, and with ASan+UBSan. Fixed guest-address mapping tests use UBSan
  because their address range conflicts with ASan shadow addressing.
* Existing external-track suite: **57 tests pass**; track-probe checks and Blender
  hierarchy/instance/winding smoke pass. Modkit self-test and ATRAC9 encoder pass.
* LLVM-MinGW 20261104 cross-build: Windows boundary-test and profile-test executables,
  and `probe.c` with diagnostics both ON/OFF, compile successfully. These binaries
  have **not been executed on Windows** here. Full Windows runtime linking and
  RTX 4090 startup require the user's MSYS2 build and retest.
* Linux `probe.c` compiles in both configurations with the project's Linux warning
  policy. A stricter exploratory `-Werror` compile found pre-existing unused
  parameters in the Linux scene-debug stub; no unrelated source was changed.

Local verification logs are under ignored `work/startup-*.log`. The supplied
private startup log, prepared images, dumps, and archives are not committed.

## Update and rebuild on Windows

PowerShell, preserving local modifications (Git stops if a fast-forward is unsafe):

```powershell
Set-Location H:\DC-Track-Test
git status --short --branch
git switch feat/external-track-foundation
if ($LASTEXITCODE -ne 0) { throw 'Branch switch failed; preserve and review local changes.' }
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if ($LASTEXITCODE -ne 0) { throw 'Fetch failed' }
git merge --ff-only FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw 'Fast-forward failed; do not reset local work.' }
git rev-parse HEAD
```

MSYS2 **CLANG64**, same terminal used for the successful runtime build:

```bash
cd /h/DC-Track-Test
set -euo pipefail
mkdir -p work
clang --version
bash tools/test_runtime_memory.sh 2>&1 | tee work/windows-memory-tests.log
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
BB_DIAGNOSTICS=OFF BB_PGO=off BB_LTO=OFF bash build.sh
git rev-parse HEAD
```

The standalone Windows test reserves the normal guest VA range and creates the
normal 5056 MiB direct pool plus flexible span, requiring sufficient Windows
commit budget. It uses no game files or Vulkan. Output binaries are
`out/DriveClubGame.diagnostics.exe` and `out/DriveClubGame.exe`.

## Isolated PowerShell retest

Use the **same prepared directory and original Base/Update paths as the failing
run**. These commands prompt for the machine-specific paths; no re-preparation
is needed. Keep the existing installation's DLLs/providers in place. New runtime
copies have unique names; the existing executable and player data are preserved.

```powershell
$ErrorActionPreference = 'Stop'
$Repo = 'H:\DC-Track-Test'
$RuntimeDir = Read-Host 'Existing runtime folder containing the release EXE and its DLLs'
$Prep = Read-Host 'Prepared folder containing boot-linked.bin, content.bin and patches.bin'
$Update = Read-Host 'Original Update folder (same --app0 path as before)'
$Base = Read-Host 'Original Base folder (same --app0-base path as before)'
foreach ($p in @($RuntimeDir, $Prep, $Update, $Base)) {
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { throw "Missing folder: $p" }
    if ($p.Contains('"')) { throw 'A path contains a quote' }
}
$Id = [guid]::NewGuid().ToString('N')
$Evidence = Join-Path $Repo "work\startup-memory-$Id"
New-Item -ItemType Directory -Path $Evidence | Out-Null
$Diag = Join-Path $RuntimeDir "DriveClubGame.memory-diagnostics-$Id.exe"
$Normal = Join-Path $RuntimeDir "DriveClubGame.memory-control-$Id.exe"
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.diagnostics.exe" -Destination $Diag
Copy-Item -LiteralPath "$Repo\out\DriveClubGame.exe" -Destination $Normal
git -C $Repo rev-parse HEAD | Set-Content "$Evidence\commit.txt"
Get-FileHash -Algorithm SHA256 -LiteralPath $Diag,$Normal,
    "$Prep\boot-linked.bin","$Prep\content.bin","$Prep\patches.bin" |
    Select-Object Path,Hash | ConvertTo-Json | Set-Content "$Evidence\hashes.json"

function Invoke-MemoryCheck([string]$Exe, [int]$MiB, [string]$Label) {
    $Run = Join-Path $Evidence $Label
    $UserDir = Join-Path $Run 'user'
    New-Item -ItemType Directory -Path $UserDir | Out-Null
    $Saved = @{}
    Get-ChildItem Env:BB_* | ForEach-Object { $Saved[$_.Name] = $_.Value }
    $OldLayers = $env:VK_LOADER_LAYERS_DISABLE
    try {
        Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
        $env:BB_DMEM_MB = [string]$MiB
        $env:BB_MEMORY_TRACE = '1'
        $env:BB_GPU_USER_DIR = $UserDir
        $env:BB_CONFIG = Join-Path $UserDir 'driveclub.ini'
        $env:BB_TOGGLE_FILE = Join-Path $UserDir 'toggles.txt'
        $env:BB_CRASH_DIR = $Run
        $env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
        $ArgsList = @("$Prep\boot-linked.bin", '--content-profile', "$Prep\content.bin",
            '--patches', "$Prep\patches.bin", '--app0', $Update, '--app0-base', $Base,
            '--user', $UserDir, '--stub-unresolved', '--timeout', '300')
        $Quoted = ($ArgsList | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $Process = Start-Process -FilePath $Exe -ArgumentList $Quoted -WorkingDirectory $RuntimeDir `
            -RedirectStandardOutput "$Run\stdout.log" -RedirectStandardError "$Run\stderr.log" -Wait -PassThru
        $Process.ExitCode | Set-Content "$Run\exit-code.txt"
        Write-Host "$Label exited $($Process.ExitCode): $Run"
    } finally {
        Get-ChildItem Env:BB_* | ForEach-Object { Remove-Item -LiteralPath "Env:$($_.Name)" }
        foreach ($Name in $Saved.Keys) { Set-Item -LiteralPath "Env:$Name" -Value $Saved[$Name] }
        $env:VK_LOADER_LAYERS_DISABLE = $OldLayers
    }
}

# Known mismatch: expect a clear preflight error and exit 1, not the old access violation.
Invoke-MemoryCheck $Diag 5056 'diagnostics-mismatch'
# Correct launch budget for the supplied 6144 MiB VideoPrivate log:
Invoke-MemoryCheck $Diag 8056 'diagnostics-corrected'
Invoke-MemoryCheck $Normal 8056 'normal-corrected'
Get-ChildItem $Evidence -Recurse -Filter '*.log' | Select-String -Pattern `
    'Memory profile:','Memory request:','mapped direct memory','Fault:', 'VirtualQuery','STOP:'
Write-Host "Evidence: $Evidence"
```

No `--mods` or benchmark is supplied. Fresh user/cache/config folders isolate
the test from existing saves. The timeout bounds each run; reaching its limit
is not by itself proof of success. Record whether menus appeared and close the
window normally if ready before the timeout. If the preflight identifies a
different known profile, use the budget it reports instead of 8056.

Expected corrected evidence: `Memory profile` shows 8056 MiB, the 6144 MiB
allocation succeeds, and a direct mapping covers VideoPrivate starting at
`0x5000000000`. Capture a screenshot of the first menu if reached. Send the
three runs' stdout/stderr/exit-code files, commit/hash metadata, and the standalone
test log. Do not send game binaries or a full memory dump. Any remaining fault's
`Memory fault context`/`VirtualQuery` block is the next mapping evidence.

Rollback is closing these test processes and deleting only the two unique
`DriveClubGame.memory-*.exe` copies and the newly created evidence directory
after retaining its logs. No original installation file needs restoration.

## Remaining work

Verify corrected startup on Windows, then resume the isolated flat-loop visual
override. This change does not establish in-game track rendering, native collision,
vehicle spawn support, or drivability. Custom-track importer code is unchanged.
