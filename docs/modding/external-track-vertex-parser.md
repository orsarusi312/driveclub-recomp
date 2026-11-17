# Offline follow-up: the 0x78-byte parser candidate

Recovered a clean local/remote branch at
`a7dc250578b4a9115ad934f61412ccdef52e12f1`. Read the complete supplied
resource-consumer.json (28,722 bytes); its prepared-image/patch hashes match the
prior reports. The user reports all 29 focused Windows tests passed. No pending
edits, game assets, saves or previous experiments were changed.

## What the returned report proves statically

The wrapper at 0x800742db0 now has a coherent normal return at 0x800742e33 and a
separately bounded failure-path call. It forwards the reader to 0x800739c10 and
propagates that helper's local output pointer, using reference-count-like updates.
The report no longer treats adjacent functions as ordinary fallthrough.

The complete bounded body at **0x800739c10** shows this sequence:

```text
R15 = incoming RDX                       // preserve reader
RBX = incoming RSI                       // preserve output-storage pointer
RAX = call 0x800728510(incoming RDI)
RAX = call [ [RAX] + 0x28 ](RDI=RAX, ESI=0x78, EDX=0)
R14 = RAX
call 0x800759140(RDI=R14, RSI=R15)         // at 0x800739c3e
if R14 != 0: atomic increment [R14+8]
atomically replace [RBX] with R14
release-like processing of previous [RBX]
return or tail-call previous object's slot +8
```

The 0x78 request and subsequent initialization support an allocation-shaped
interpretation, but the virtual method's signature, alignment/flag meaning,
allocation success and lifetime are not observed. There is no null check before
the call to 0x800759140. Importantly, the published pointer is **the saved R14**,
not the return value of 0x800759140. This identifies the object candidate to trace,
without claiming that it is a GPU buffer or geometry owner.

The live dispatch to 0x800742db0 and the eight-byte ID transfer within its interval
were observed in the preceding Windows run. Individual calls inside 0x800739c10
were not breakpoints in that run; the sequence above is reconstructed from code.

## The common-header candidate is now structurally coherent

The exact direct call at **0x8007730f5** to **0x800760240** matches the observed
scalar-reader return candidate 0x8007730fa. A bounded prologue decode beginning
at **0x8007730c0** reaches that instruction boundary coherently and then tail-jumps
to the four-byte helper:

| Object offset | Static behavior | Interpretation limit |
|---|---|---|
| +0x00 | stores RIP-relative address point 0x8015f1210 | vtable-like store; slots not inspected |
| +0x08 / u32 | initializes zero | later reference-count-like users are consistent; no official field name |
| +0x10 / byte | initializes 1 | purpose unknown |
| +0x18 / u64 | zeroes, then passes its address to eight-byte reader | ID destination candidate |
| +0x0c / u32 | passes its address to four-byte reader via tail jump | next scalar destination candidate |

The stored address is derived exactly from
`0x8007730d4 + 7 + 0xe7e125 + 0x10 = 0x8015f1210`.
No table scan or relocation resolution is needed to establish that arithmetic;
it does not prove a live vptr or class identity.

On successful sequential reads after EVOSLITL, this would consume the u64 ID at
payload +8 and u32 version at +16. That agrees with dcmod's independently known
header layout (`RpkResource::PayloadVersion` in `tools/modkit/src/rpk.cpp`). The
observed ID destination 0x2000b20b98 would correspond to object base 0x2000b20b80
**if this routine supplied it**. That base also appears in the raw debugger stack
display, but the native parent frame was not independently captured. These are
corroborating clues, not proof of the object's allocation or this routine's execution.

No size, stride, vertex count, Road742 ID comparison or GPU handoff is visible in
this candidate's body. The +0x0c scalar is consistent with the serialized version,
not evidence that it is a payload size or native type discriminator.

## The specific missing edge

We must decode **0x800759140**, the function which receives the candidate 0x78-byte
object and reader. Its bytes are absent from the supplied reports and no prepared
game image exists in this workspace. The questions are whether it calls the
common-header initializer (directly or through another routine), how it advances
past the header, and where it stores or forwards the subsequent vertex bytes.

The earlier Windows `kv 8` output also contained **0x8007595ff** one level beyond
the ID-reader caller lead. This remains an unreliable-unwind **candidate**, not a
verified return. A narrow, conditional check of the exact call ending there can
test whether it calls 0x8007730c0 and whether its local prologue lead is referenced
by 0x800759140. This is justified by the captured read path; no arbitrary caller
graph or executable scan is warranted.

For comparison only, dcmod's vertex-buffer decoder currently uses stride at
payload +22, count at +24 and data at +28 (`tools/modkit/src/cmd_mesh.cpp`). If the
native parser reaches the same serialized fields, that may establish a concrete
comparison with the importer. Do not assume those bytes are read unchanged into
the eventual renderer buffers, or change importer validation before seeing code.

The verified payload ID remains **0003d12ccbd11945**, not Road742's position buffer
**0003a8908986f62b**. Both have dcmod type 3, making a shared native parsing path
plausible, but Road742's selector, parsed instance and downstream bindings remain
unobserved. Resource-ID consumption is not vertex-array consumption.

## Implemented: one targeted offline profile

`--vertex-parser` verifies the exact direct call at 0x800739c3e and reads only:

* A 1024-byte code window at 0x800759140, bounded to 256 instructions/32 block
  starts, with existing failure-call review boundaries and no recursive following.
* A 384-byte neighborhood of 0x8007595ff, **only if** the five-byte call ending
  there targets 0x8007730c0. At most two prologue-pattern leads from the previous
  128 bytes are considered. Code-to-lead links are explicitly static candidates.

The report lists any direct calls to 0x8007730c0 and any direct calls to coherent
bridge leads. Other branches/indirect edges and budget stops remain in the code
report; absent listed links are not proof that no path exists. A failed optional
bridge check leaves the main parser report usable. A wrong caller guard aborts.

The bounded caller-lead routine is reused from the previous profile without
changing its behavior. The successful wrapper, allocation-shaped helper, scalar
helpers, vtable and caller searches are not repeated. All mapping, executable
permissions, relocation/patch overlap rejection and input-hash checks remain.
The new profile neither reads nor modifies any live process, asset or save.

## Windows: offline inspection only

No game launch, debugger, MSYS2 rebuild, RPK scan or new environment is required.
Reuse `$Py`, `$Image` and `$Patches` from the successful offline session. The block
below also restores documented paths when starting a new PowerShell session.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed' }
if(-not (Get-Variable Py -ErrorAction SilentlyContinue) -or -not $Py) {
  $Prior=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-static-caller-*' |
    Where-Object { Test-Path -LiteralPath "$($_.FullName)\venv\Scripts\python.exe" -PathType Leaf } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if(-not $Prior) { throw 'Existing inspector venv missing; do not replace it' }
  $Py=Join-Path $Prior.FullName 'venv\Scripts\python.exe'
}
if(-not (Get-Variable Image -ErrorAction SilentlyContinue) -or -not $Image) {
  $Image='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep\boot-linked.bin'
}
if(-not (Get-Variable Patches -ErrorAction SilentlyContinue) -or -not $Patches) {
  $Patches='H:\DRIVECLUB TO PC\Launcher\driveclub-recomp-0.4.0-win64\data\prep\patches.bin'
}
foreach($File in @($Py,$Image,$Patches)) {
  if(-not (Test-Path -LiteralPath $File -PathType Leaf)) { throw "Missing existing input: $File" }
}
if((Get-FileHash -LiteralPath $Image).Hash.ToLowerInvariant() -ne '839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971') { throw 'Prepared image changed' }
if((Get-FileHash -LiteralPath $Patches).Hash.ToLowerInvariant() -ne '6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761') { throw 'Patches changed' }
$Run="$Repo\work\road742-vertex-parser-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$TestArgs=@('-m','unittest','test_native_caller','test_native_control_flow','test_resource_consumer','-v')
$Test=Start-Process $Py -ArgumentList $TestArgs -WorkingDirectory "$Repo\tools\track_import\tests" -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline tests failed; inspect logs' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--vertex-parser',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\vertex-parser.json`"")
$Inspect=Start-Process $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw 'Inspection refused input; return logs without bypassing checks' }
Write-Host "Return vertex-parser.json, commit.txt and the four logs from $Run"
```

Do not add earlier profile flags or upload binary images/RPKs/saves. Return a
blocked window, missing candidate or budget stop as-is. The question is the
specific parser-to-header/buffer edge, not another reproduction of the ID copy.

## Validation and remaining blocker

All **98 Python tests passed**, no skips/failures, using the existing dcmod binary
and synthetic RPK integration fixture. The local log is
`work/native-static-validation/vertex-parser-tests.log` (not committed). New tests
cover parser/caller guards, conditional static bridge links, patch/relocation
refusals, absence of false edges from blocked windows, unchanged inputs and
profile exclusivity/no repeated searches. The original profile's regressions
also pass after sharing the bounded caller-lead implementation.
The documented focused invocation also passed **32/32** tests; local log:
`work/native-static-validation/vertex-parser-focused.log`.

Windows execution of the new profile is pending. No runtime/native source or
importer behavior changed, so no game build or native sanitizer run was needed.
Tsukuba remains blocked by the unverified link from **Road742's own vertex bytes**
through native parsing/storage to a recorded draw. Collision and road spawning
require their own native integration after that rendering milestone; this header
analysis does not establish either.
