# Road742: inspect the request caller offline

## Follow-up: Windows patch-padding rejection

The three returned location reports establish that file offset 113877454 starts
vertex-buffer payload **0003d12ccbd11945**, size **1132**, with a valid magic and
payload ID matching the TOC. The Road742 position payload remains
**0003a8908986f62b** at 113879809; its first sampled position is at payload offset
23324. Thus the verified eight-byte transfer read the opening magic of another
vertex-buffer payload. This still does not identify that resource's owner or the
native function at the recorded parent return.

The Python inspector incorrectly used the strict unrounded `mapped()` predicate
to admit BBPATCH2 entries. `src/probe.c:patchable()` explicitly also permits a
whole patch inside the page-rounded extent of any one segment. The Windows x64
runtime obtains a **4096-byte page size** from GetSystemInfo; 64 KiB allocation
granularity and PS4 page size are not used for this check. The prepared image
stores unrounded segment sizes, so these predicates can differ on valid inputs.

The mismatch is confirmed and reproduced synthetically. The supplied generic
error does not identify the actual failing entry, and the user's binary patch
file is not present here; the retry is needed to confirm that this was the only
reason their file failed. Input hashes were reported unchanged.

The fix adds a separate `patchable()` predicate for patch admission only. Code
reads and relocation validation still require unrounded, unambiguous mappings;
all code-window patch/relocation overlap refusals remain. Patches may cross from
segment bytes into its last-page padding, but cannot span the rounded boundary
into another segment. No patch is applied. Invalid entries now identify zero-based
`patch[index]`, image offset, computed guest address, size and page size.

The screenshot also shows Windows PowerShell treating unittest's ordinary stderr
progress as `NativeCommandError`. That line ending in `ok` is not evidence of a
failed assertion. Use Start-Process's OS-level redirection below and check the
process ExitCode; do not infer success or failure from stderr being nonempty.

Minimal retry, **same PowerShell session**, keeping `$Py`, `$Image`, `$Patches`
from the prior run (no new venv, RPK scan, build, game launch or save access):

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed; stop' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed; stop' }
foreach($File in @($Py,$Image,$Patches)) {
  if(-not $File -or -not (Test-Path -LiteralPath $File -PathType Leaf)) { throw 'Restore the existing Python/image/patch paths' }
}
$Retry="$Repo\work\road742-static-retry-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Retry | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Retry\commit.txt"
$Before=@(Get-FileHash -LiteralPath $Image,$Patches)
$TestArgs=@('-m','unittest','discover','-s',"`"$Repo\tools\track_import\tests`"",'-p','test_native_caller.py','-v')
$Test=Start-Process -FilePath $Py -ArgumentList $TestArgs -Wait -PassThru `
  -RedirectStandardOutput "$Retry\tests-stdout.log" -RedirectStandardError "$Retry\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw "Offline tests failed: $($Test.ExitCode); inspect retry logs" }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Retry\native-caller.json`"")
$Inspect=Start-Process -FilePath $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Retry\inspect-stdout.log" -RedirectStandardError "$Retry\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw "Inspection failed: $($Inspect.ExitCode); return the detailed retry error" }
$After=@(Get-FileHash -LiteralPath $Image,$Patches)
if(Compare-Object $Before $After -Property Path,Hash) { throw 'Input hash changed; stop' }
$After | Select-Object Path,Hash | Export-Csv "$Retry\input-hashes.csv" -NoTypeInformation
Write-Host "Return native-caller.json, commit.txt, hash CSV and the four logs from $Retry"
```

If reopening PowerShell, restore the existing paths first:

```powershell
$Previous=Read-Host 'Full existing road742-static-caller directory containing the venv'
$Py=Join-Path $Previous 'venv\Scripts\python.exe'
$Image='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep\boot-linked.bin'
$Patches='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep\patches.bin'
```

Validation of this fix: all ten offline inspector tests passed with Capstone,
including valid final-page padding (also beyond the on-disk image end), exact end
boundaries, cross-segment refusal, out-of-range/oversized/overflowing patches,
entry-specific errors and continued strict code/relocation rejection. Existing
guard and input-integrity tests passed. Windows retry remains pending; no runtime
or asset files changed.

## Recovered state and Windows evidence

Continued the clean branch at `31bd5480489fc2acd9c41aed72b9dd8d2d8da8ca`, matching
the attached commit.txt and the fork. No unfinished edits were discarded. All
seven attachments were read: stdout (1,650 lines), stderr (66 lines), WinDbg
(234,228 lines), UTF-16 diagnostic summary (111 lines), preparation.json,
mesh-scan.json and commit.txt. The summary repeats existing log records.
Stdout reports 40 stock overrides and normal exit(0). The debugger log contains
233,451 first-chance events and no reported second-chance exception. That is not
evidence of a new startup crash. Existing stub/warning messages remain in the logs.

The preparation report's stock-source and copy hashes agree. The focused scan
reports one structurally compatible slot, 47 vertices, 65 triangles, first vertex
1456, stride 16, and 35,189 vertices in its shared position resource. Structural
compatibility and stock override success still do not prove a draw.

The request observer verified the actual eight-byte transfer:

```text
source       0xf0026a9370
destination  0x7a7ee250 (current-thread-stack read path succeeded)
bytes        45 56 4f 53 4c 49 54 4c = EVOSLITL
copy return  0x800591f1e
XYZ prefix   0xf0026af7bf (all configured samples still match)
thread       182c.5f68, async_1
```

WinDbg hit `0x800591f1e` and then `0x80058bdf3` on this same thread. At the first
hit R12=R14=8 and R15 equals the destination. At the second, RBX=8, R12 equals
the destination, AL=1 and R15B=1. The two explicit frame records were:

```text
[0x7a7ee200] = 0x7a7ee240 ; [0x7a7ee208] = 0x80058bdf3
[0x7a7ee240] = 0x7a7ee270 ; [0x7a7ee248] = 0x8007aafa2
```

`0x8007aafa2` is therefore an observed saved parent return, not an executed
breakpoint in this log. Its instruction bytes are **not included** in the log.
The destination lies 0x20 bytes below that saved parent frame pointer; this helps
locate the eight-byte local but does not identify the function's name or purpose.

## New distinction: the window start is not the target payload start

The read trace logs the target resource overlap, whereas `RpkRequest: armed`
logs the beginning of the whole file-read window. They are different addresses:

| Item | RPK file offset | Offset from read start |
|---|---:|---:|
| Selected 64 KiB read start, source of EVOSLITL | 113877454 | 0 |
| `0003a8908986f62b` payload start | 113879809 | 2355 (0x933) |
| Road742 first position sample | 113903133 | 25679 (0x644f) |

The target payload's prefix displacement is `28 + 1456 * 16 = 23324`, agreeing
with the scanner and preparation report. The second trace's 63,181 overlapping
bytes equal `65536 - 2355`. These independent offsets agree.

Consequently the eight-byte read is **not at the Road742 vertex resource's
header**. `EVOSLITL` also occurs at the pack header, TOC and resource payloads
in dcmod's format implementation. The eight bytes alone cannot identify which
record was requested, much less decode a complete header. The next offline
`rpk locate` checks whether the read start coincides with another parsed payload
and compares its payload identifier with the TOC. If it does, that identifies
the file span, not native selection or later consumption.

## What the observed native wrapper does

The bounded disassembly immediately before `0x80058bdf3` shows:

* Load a reader object from `[r14]`, then its vtable.
* Pass destination R12 in RSI and length RBX in RDX.
* Call vtable slot `+0x18` at `0x80058bdf0`.
* At `0x80058bdf3`, test AL and branch over the failure path when nonzero.
* The failure path changes byte flags at `0x801911b78/79` and may call
  `0x800662260`. The names/semantics of these flags and that function are unknown.
* The common epilogue returns R15B in AL and restores saved registers/RBP.

This is a read/status wrapper by observed behavior. `test al,al` tests the read
result; it is not a comparison with the EVOSLITL bytes. Earlier optional vtable
calls at +0x38/+0x30 cannot be named from these instructions alone.

```text
unknown function containing saved return 0x8007aafa2
  -> wrapper containing return 0x80058bdf3
     -> buffered reader 0x800591e60 through vtable +0x18
        -> existing copy bridge, return 0x800591f1e
           -> verified 8-byte stack copy
```

No symbol or function entry for the upper two frames has been recovered. A normal
five-byte CALL would start at 0x8007aaf9d, but its encoding has not been observed;
do not use that arithmetic as proof. The new decoder enumerates candidate CALLs
ending at the recorded return instead of asserting one.

## Static inspection available here versus on the Windows PC

No `boot-linked.bin`, `eboot.elf`, game executable or original RPK is present in
this workspace. The source tree does not contain the missing original engine
function bodies or a symbol for these addresses. Searching the source is not a
substitute for decoding the local guest image. We therefore cannot presently
determine what instructions follow `0x8007aafa2` or name its containing function.

Repository evidence used:

* `launcher/prepare.cpp: Link` serializes BBPROBE6's linked image and tables.
* `src/probe.c: main` reads that format and maps image-relative segment offsets.
* `src/probe.c: apply_patches` applies BBPATCH2 later; simply treating the boot
  file offset as a guest address, or ignoring patch overlays, would be incorrect.
* `tools/modkit/src/rpk.cpp: RpkPack::Parse` provides validated file payload spans,
  `PayloadId` and `PayloadVersion`; no second RPK parser is introduced.
* `src/runtime_rpk_trace.h` distinguishes resource overlap from the read window.
* `src/runtime.c: guest_memcpy/guest_memmove` and the existing request observer
  explain the observed copy. No new runtime instrumentation is needed yet.

## Implemented tools and bounds

`dcmod rpk locate PACK OFFSET` emits JSON for the parsed payload containing a
decimal or `0x` file offset. It reports TOC ID, type, size, start, relative offset,
header magic, payload ID agreement and version. A null resource means no payload
contains that offset (for example TOC, EOF or beyond EOF). It omits names, original
source paths and asset payload bytes. It uses the existing whole-pack parser,
so its memory cost is the same class as `rpk info`; it is not a streaming reader.

`tools/track_import/inspect_native_caller.py` reads local BBPROBE6 and BBPATCH2
files without executing or modifying them. It locates the actual image-data file
offset from the tables, checks known instruction bytes against the WinDbg evidence,
and uses Capstone 5.0.6 to decode three 512-byte windows around the recorded returns.
It also emits at most four 128-byte direct-call target previews per site.

* Code must be wholly inside an unambiguous executable segment.
* A window overlapping a relocation or supplied patch is blocked, not decoded
  as if it represented live code. Patch bytes are never applied to any image.
* The report hashes both inputs before/after and refuses existing output files.
* Forward decode starts at the recorded return. It is a linear listing, not an
  executed path or reconstructed control-flow graph; bytes after RET may belong
  to another function or data. Undecodable tails are not silently skipped.
* Backward CALL and prologue patterns are explicitly candidates. A pattern match
  does not establish a function boundary. Function names remain null.
* Guards and patch checks do not prove whole-image runtime equivalence. Runtime
  import binding, other fixups and dynamically modified bytes are not simulated.
* Only BBPROBE6 is supported. Original SELF, ELF and other prepared versions fail
  closed. No recursion, broad code search, live tracing or new breakpoint exists.

## Next concrete experiment: offline only

Question A: **Which parsed resource, if any, starts at the selected read window?**
Question B: **What CALL can end at 0x8007aafa2, and what code follows that return?**

No gameplay run, WinDbg session, BB environment variables or save cloning is needed.
Leave all earlier experiment directories and the working diagnostics executable
untouched. Do not re-run the successful request or refill experiments.

Update and build **dcmod only** in MSYS2 CLANG64:

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
bash tools/track_import/test.sh  # also builds dcmod
bash tools/modkit/test.sh
```

The ordinary test suite reports skipped optional decoder cases if Capstone is not
installed in that shell's Python. The commands below run all decoder cases in an
isolated Windows Python environment; there is no change to Blender or game Python.

Run in PowerShell with the same prepared image/patches used in the successful run:

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Prior="$Repo\work\road742-request-caller-78ec1736cd994017895154f92de8b547"
$Prep='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep'
$Pack="$Prior\inputs\mods\worlds\canada_road_circuit_02_n.rpk"
$Image="$Prep\boot-linked.bin"
$Patches="$Prep\patches.bin"
$Dcmod="$Repo\out\dcmod.exe"
foreach($File in @($Pack,$Image,$Patches,$Dcmod)) {
  if(-not (Test-Path -LiteralPath $File -PathType Leaf)) { throw "Missing input: $File" }
}
$Expected='1d7822ef18458025a83bd733d429fa630bf70953c41298cbb722f6c104cdb564'
if((Get-FileHash -LiteralPath $Pack -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Expected) {
  throw 'Stock-copy RPK differs from the uploaded preparation report; stop'
}
$Run="$Repo\work\road742-static-caller-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
$Before=@(Get-FileHash -LiteralPath $Pack,$Image,$Patches -Algorithm SHA256)
$Before | Select-Object Path,Hash | Export-Csv "$Run\input-hashes-before.csv" -NoTypeInformation
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"

$Locations=@(@('read-start',113877454),@('vertex-resource',113879809),@('vertex-prefix',113903133))
foreach($Location in $Locations) {
  $Json=& $Dcmod rpk locate $Pack ([string]$Location[1])
  if($LASTEXITCODE -ne 0) { throw "rpk locate failed: $($Location[0])" }
  $Json | Set-Content "$Run\$($Location[0]).json" -Encoding UTF8
}

python -m venv "$Run\venv"
if($LASTEXITCODE -ne 0) { throw 'venv creation failed' }
$Py="$Run\venv\Scripts\python.exe"
& $Py -m pip install 'capstone==5.0.6'
if($LASTEXITCODE -ne 0) { throw 'Capstone install failed' }
$TestArgs=@('-m','unittest','discover','-s',"`"$Repo\tools\track_import\tests`"",'-p','test_native_caller.py','-v')
$Test=Start-Process -FilePath $Py -ArgumentList $TestArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline decoder tests failed' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\native-caller.json`"")
$Inspect=Start-Process -FilePath $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw 'Inspection refused the inputs; return the error logs without launching the game' }
$After=@(Get-FileHash -LiteralPath $Pack,$Image,$Patches -Algorithm SHA256)
$After | Select-Object Path,Hash | Export-Csv "$Run\input-hashes-after.csv" -NoTypeInformation
if(Compare-Object $Before $After -Property Path,Hash) { throw 'Input changed during inspection; retain evidence and stop' }
Write-Host "Offline evidence ready: $Run"
```

If the existing prep directory was regenerated after the Windows run, record that
fact: instruction guards alone cannot restore the previous executable's identity.
Do not upload the boot image, patches, RPK, saves, venv or large memory dumps.

Return `native-caller.json`, the three location JSONs, `commit.txt`, both inspection
logs, both decoder-test logs, and the before/after hash CSVs. These reports contain
small bounded disassembly excerpts, not a redistributed game image. If a guard or
patch overlap blocks decoding, return that result rather than disabling the check.

## Interpretation and next decision

* A read-start payload with a different ID establishes which file resource's
  opening eight bytes were copied. It still does not prove what the caller does.
* A unique direct CALL candidate and consistent target prologue provide a static
  call-site lead. A matching linear decode from a candidate entry is stronger
  than a backward pattern alone, but without symbols/unwind metadata its function
  boundary remains provisional.
* Code following 0x8007aafa2 may compare the magic, read additional fields, select
  another resource, or delegate. Interpret the actual instructions, not the
  constant's spelling. Follow-up observation must be selected from those results.
* Another Windows run is justified only if static bytes leave a specific dynamic
  value unresolved, such as a dispatch target or next requested resource ID. No
  new live experiment is prescribed until these offline reports are examined.

This narrows why Road742 bytes were prefetched alongside an eight-byte request
without tracking already overwritten data. The missing milestone remains a
verified vertex-byte consumer/transfer; persistent allocations, draw identity,
visible custom geometry and collision remain unproven.

## Validation

Verified locally:

* `bash tools/track_import/test.sh` passed with the isolated Capstone environment:
  native probe/resource tests, the Python suite, and Blender 4.3.2 smoke, instance,
  hierarchy and winding checks. After the last added header-mismatch case, the
  full Python suite passed **74 tests with no skips**.
* `bash tools/modkit/test.sh` passed, including ATRAC9 encoder checks.
* An ASan/UBSan dcmod build passed all three `test_rpk_locate.py` tests, including
  malformed offsets/packs, payload boundaries, mismatched header identity and
  unchanged input files. No sanitizer findings. GCC emitted existing libstdc++
  regex `-Wmaybe-uninitialized` compilation warnings.
* All eight native-image inspector tests passed with Capstone 5.0.6: mapping and
  execution permissions, malformed/truncated metadata, patch/relocation overlap,
  guarded decoding, call-site candidates, undecodable tails, hashes and refusal
  to overwrite a report.
* The changed C++ command passes an LLVM-MinGW Windows syntax check with warnings
  as errors. Windows execution of the new offline tools remains untested here.
* The environment lacks proprietary guest code/RPKs: real-image analysis at
  0x8007aafa2 and real read-start payload identity remain pending the local reports.
  No runtime source changed; no new diagnostics executable build is required.

Modified files: `tools/modkit/src/cmd_rpk.cpp`, new
`tools/track_import/inspect_native_caller.py`, its `test_native_caller.py` and
`test_rpk_locate.py` tests, this report and the resource-trace documentation index.
No user log, game code image or proprietary asset is included in the commit.
