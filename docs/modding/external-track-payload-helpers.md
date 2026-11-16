# Road742: prioritized caller paths and missing payload helpers

Continued the clean branch at `a58fd3988ca9407e22d478c38b3f2bf4d1f6256e`.
The uploaded reader-details.json (239,641 bytes) retains the prior image and patch
hashes; its four vtable slots resolved and its search stopped at eight candidates
after 7,884,800 executable bytes. The Windows log reports all 14 then-current
inspector tests passed. No original game files, saves or prior experiments changed.

## Established static dispatch and helper behavior

At address point `0x8015f3c40`, relative relocations predict:

| Slot | Target | Supported interpretation |
|---|---|---|
| +0 | 0x8007ab020 | cleanup-shaped routine; official name unknown |
| +8 | 0x8007aafe0 | another cleanup-shaped routine |
| +0x10 | 0x8007ab060 | `this += 0x18`, zero-extend length, jump to 0x80058bd70 |
| +0x18 | 0x8007aba70 | clear u32 at `this+0x10`, return |

This closes the **predicted static** slot-10 link to the wrapper whose return was
observed at runtime. It does not identify a live object or prove Road742 consumption.
Slot +0x18 does not itself seek or read bytes: its body is only a state-field clear.
The nearby one-byte-read tail thunk at `0x8007aba80` is a separate routine, not
part of that slot's body after its RET.

Other newly available bodies narrow the interpretation:

* `0x80058bc90` copies `[RSI]` to `[RDI]` and byte `[RSI+8]` to `[RDI+8]`, then
  returns. It copies an adapter's pointer/flag; it does not copy vertex data or
  allocate storage in this body.
* `0x80058bd40` immediately returns. Calls to it in these continuations do not
  consume bytes in the supplied prepared image.
* `0x80058bd10` conditionally calls the referenced object's slot +8, then zeros
  the pointer. That supports a cleanup interpretation but not persistent geometry
  ownership or the identity of the virtual callee.
* `0x80058bd50` gates on a global flag, forwards the referenced object to its
  slot +0x10, and returns a result. Earlier buffered-reader evidence associates
  that underlying slot with a seek-like method at 0x800591de0. The concrete
  object in each static caller is still unknown; do not promote this to a
  verified seek of Road742.

## All eight candidates, grouped rather than counted as eight independent paths

The direct-call search is incomplete (`complete=false`, `candidate-budget`).
Byte-pattern hits are not automatically valid instruction starts or executed paths.
Three pairs have stronger structural support: the second initializer call appears
at a matching boundary in the first call's forward continuation.

| Candidate | Supplied continuation | Priority |
|---|---|---|
| 0x80077bd96 | passes an initialized-reader candidate in RCX to 0x80077bec0; then cleanup/reference-count-like operations | High: common processing helper |
| 0x80077c05e | calls 0x8007ab640, twice 0x8007601d0, then another initializer at 0x80077c0d9 | Highest: explicit repeated read-like sequence |
| 0x80077c0d9 | passes the second reader in RCX to 0x80077bec0 | High: continuation of previous row, not independent |
| 0x80077d5bf | same three-helper sequence; loads final destination as a dword into ESI, EDX=0, calls 0x80058bd50; initializes again at 0x80077d621 | High: field value affects stream-like operation |
| 0x80077d621 | passes initialized-reader candidate in RDX through a virtual call; processes output/reference-count-like state | Secondary: indirect target unresolved |
| 0x8007848bd | same helper sequence; final dword feeds 0x80058bd50; initializes again at 0x800784902 | Corroborates previous pair |
| 0x800784902 | selects an object through slot +0x20 with ESI=1; passes reader in RDX through its slot +8 | Secondary: dispatch selection, not a vertex loader identification |
| 0x800784c2b | similar dispatch using a byte loaded from `[r15+0x2a]` | Secondary: mode-like input, semantics unknown |

The best immediate targets are therefore **0x8007ab640, 0x8007601d0 and
0x80077bec0**. Their bodies are absent from the uploaded windows; no guest image
exists in this workspace. Names such as `ReadU64`, `ReadVersion` or `LoadVertexBuffer`
would currently be guesses, so none is assigned.

The first two helpers receive distinct local destinations. The third result in
the repeated sequence is later loaded as a dword. That does **not** prove the
helpers request 8/4/4 stream bytes, nor that those values are ID/version/size.
At 0x80077c0bf..0x80077c0c9 the last dword instead enters ECX for a virtual call;
the object's method and the parameter meaning are unavailable. At 0x80077d604
and 0x8007848f6 it enters ESI for the forwarding method described above.

The repository's parser provides a useful independent constraint, not a native
execution trace: standard resource payloads start with magic, u64 ID and u32
version; the vertex-buffer decoder reads stride at +22, count at +24 and vertices
from +28 (`tools/modkit/src/cmd_mesh.cpp`). A presumed size or offset at +20
cannot simply be imposed on that vertex format. The caller may be reading an
outer container or indirection record rather than a vertex payload.

## Road742 connection still missing

The initial eight-byte runtime observation belongs to vertex resource
`0003d12ccbd11945`. Road742's `0003a8908986f62b` was only verified farther inside
the same refill. None of these candidate continuations supplies that resource ID,
a corresponding verified vertex transfer or a GPU binding.

The next useful evidence is the helper bodies' actual read lengths, swap branches,
field stores, calls and dispatch decisions. If a helper reads a resource ID and
uses it for a typed dispatch, that would supply a concrete boundary for a future
target-ID-conditioned observation. If it only handles container metadata, follow
the specific returned resource/stream path instead. No runtime experiment is
prescribed until this distinction is resolved statically.

## Implemented targeted offline follow-up

`--payload-details` is mutually exclusive with `--reader-details`. It verifies
the three recorded direct-call encodings at 0x80077c075, 0x80077c084 and
0x80077c10c, then examines only their three helper targets. It does **not** repeat
the return-site windows, vtable inspection or executable-image caller search.

Each helper is limited to 1024 code bytes, 256 instructions and 32 block starts.
A small static block walk follows direct conditional/unconditional branches only
within that window, stops at returns/traps and records external/indirect edges.
Calls are listed but never followed. Fallthrough after a call is a syntactic
possibility, not proof that the callee returns. Loops are visited once; malformed
decoding, branches into instructions and budget exhaustion are explicitly reported.
It is not a complete control-flow recovery or an execution trace.

Strict image mapping, relocation/patch overlap rejection, instruction guards and
before/after file hashes remain. A blocked window stays blocked. No game memory,
assets, GPU synchronization or save data are accessed or modified. No game build
or launch is needed. The block-walk code is separate in
`tools/track_import/native_control_flow.py`; tests use original synthetic bytes.

## PowerShell: inspect only the missing helpers

Reuse `$Py`, `$Image` and `$Patches` from the successful offline run. If starting
a new PowerShell session, the commands restore the documented prepared-file paths
and find an existing inspector venv. The existing Capstone venv is sufficient.
Use a new directory so all previous
reports remain intact; Start-Process avoids PowerShell's native-stderr issue.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
if(-not (Get-Variable Py -ErrorAction SilentlyContinue) -or -not $Py) {
  $Prior=Get-ChildItem "$Repo\work" -Directory -Filter 'road742-static-caller-*' |
    Where-Object { Test-Path -LiteralPath "$($_.FullName)\venv\Scripts\python.exe" -PathType Leaf } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if(-not $Prior) { throw 'Existing inspector venv not found; use the $Py path from the successful offline run' }
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
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed' }
if((Get-FileHash -LiteralPath $Image).Hash.ToLowerInvariant() -ne '839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971') { throw 'Prepared image changed' }
if((Get-FileHash -LiteralPath $Patches).Hash.ToLowerInvariant() -ne '6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761') { throw 'Patch file changed' }
$Run="$Repo\work\road742-payload-helpers-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$TestArgs=@('-m','unittest','discover','-s',"`"$Repo\tools\track_import\tests`"",'-p','test_native_*.py','-v')
$Test=Start-Process $Py -ArgumentList $TestArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline tests failed; inspect logs' }
$ArgsList=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--payload-details',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\payload-details.json`"")
$Process=Start-Process $Py -ArgumentList $ArgsList -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Process.ExitCode -ne 0) { throw 'Inspection refused input; return the detailed error' }
Write-Host "Return payload-details.json, commit.txt and the four logs from $Run"
```

Do not add `--reader-details`, rerun RPK preparation, change budgets or launch the
game for this step. `blocked`, budget stops or external edges are valid evidence;
return them without bypassing checks. The file hashes are verified internally
before report creation. Keep the image, patch binary, RPKs, saves and venv local.

## Validation

Recovery found all six unfinished source/test/documentation files intact at
`a58fd398`, with a completed 86-test passing log and no staged changes. Final
review also found that Capstone 5 does not classify LOOP/LOOPE/LOOPNE as JUMP;
the block walk now follows those conditional edges explicitly and recognizes
interrupt returns as terminal. Synthetic coverage checks both behaviors.

Final validation: all **87 Python tests passed**, with the existing `out/dcmod`
and synthetic `work/track-fixture.rpk` enabled for integration tests; no skips or
failures. This includes 21 offline inspector/block-walk tests covering call
guards, strict mapping and patch/relocation rejection, no repeated prior search,
input preservation, conditional branches, loops, returns, unresolved targets,
malformed instruction boundaries and both walk budgets. The local log is
`work/native-static-validation/payload-helper-final-tests.log` (not committed).
Actual helper instruction semantics and Windows execution of this new profile
remain pending the local report. No C/C++ or runtime behavior changed; game
rebuilds and native sanitizer runs are not needed for this Python-only extension.
