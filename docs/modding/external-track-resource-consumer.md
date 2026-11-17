# Confirmed resource-ID consumption and the missing vertex parser

> Completed: the resource-consumer report succeeded. Continue with the targeted
> [0x800759140 parser follow-up](external-track-vertex-parser.md); do not repeat
> the successful wrapper/helper inspection or ID-copy game experiment.

Continued clean local/remote `feat/external-track-foundation` at
`4b43a065857f85bf966c3f27ccb76ee106ebad74`, matching uploaded commit.txt. No
unfinished modifications were present. Read all uploaded evidence: stderr 64
lines, stdout 1,380 lines, WinDbg 68,186 lines and dispatch-target.json (3,498
bytes). The debugger log has 67,433 first-chance mentions and no second-chance
mention. Stdout reports normal termination. No numeric exit code is inferred.
Offline image/patch hashes match the previously verified prepared inputs.

## New verified edge, precisely scoped

At 0x80077bf2b on thread `1848.48f8` / `async_1`:

```text
manager             0x20073cb410 (R12)
array               0x20073cf470 ([manager+0x48])
selector            3 (R14D and descriptor byte +0x2a)
array[3]            0x2000720080 (RSI)
vtable              0x8015ed950 (RAX)
slot +8 target      0x800742db0
reader              0x797fdfc0 (RDX = R13)
dispatcher output   0x797fdf58 (RDI)
saved caller return 0x80077bdce
```

The offline kind-0 relocation at **0x8015ed958** independently predicts the same
target. This is stronger than matching a shader or guessing a vtable from a slot
number: an actual selected object, invoked interface and reader argument have
been identified for this request.

During that selected method's execution interval, before its monitored return,
the same thread hit the reader thunk 0x8007ab060 with RDI=the same reader,
RSI=0x2000b20b98, EDX=8 and immediate return 0x8007602af. It then reached that
return with the expected RSP (+8), AL=1 and consistent buffer state:

| Observation | Before | After |
|---|---|---|
| source 0xf0026a9358, 8 bytes | `45 19 D1 CB 2C D1 03 00` | unchanged |
| destination 0x2000b20b98, 8 bytes | all zero | `45 19 D1 CB 2C D1 03 00` |
| logical position | 113877462 | 113877470 |
| buffered cursor | 8 | 16 |
| buffered remaining | 65528 | 65520 |
| window start | 113877454 | unchanged |
| buffer base/capacity | 0xf0026a9350 / 65536 | unchanged |

The before-source/after-destination match, advance and return checks establish a
concrete eight-byte read/copy through the selected reader path. The value is
**0003d12ccbd11945**, matching the independently located payload's ID at file
offset +8. This is resource-ID consumption, **not Road742 position consumption**.
The destination is outside the captured native stack range; its allocation size,
owner, lifetime and relationship to a mesh are still unknown. No claim of a
persistent geometry allocation follows from its address.

These are historical addresses from this one run. The copied data and metadata
establish only this edge; they are not a lifetime lease or proof excluding every
possible concurrent writer.

## Smallest coherent parsing chain

The supplied static code and runtime observation support this separation:

```text
observed caller 0x80077bd96 -> reader initializer
confirmed selected dispatch at 0x80077bf2b -> 0x800742db0
  static: 0x800742de2 -> 0x800739c10, reader still in RDX
  [intervening parsing calls not yet reconstructed]
  observed: 0x800760240's non-swapping read -> reader thunk 0x8007ab060
       -> wrapper/buffered read -> verified 8-byte ID destination
       <- observed return 0x8007602af
```

The body of 0x800742db0 preserves its incoming output-storage address in RBX,
clears local `[rbp-0x20]`, loads RDI from the global pointer slot at
**0x802749f50**, and passes RSI=&that local into **0x800739c10**. No instruction
before the call changes RDX, so the reader is forwarded unchanged. The global
pointer's live value/type is not supplied; do not name it an allocator, factory
or resource registry yet. The selected method does not use its incoming selected
object in RSI before overwriting RSI on this path.

After that direct call, 0x800742de7 loads the local result and stores it through
the original output-storage pointer. The remaining supplied instructions adjust
reference-count-like fields and conditionally call a virtual slot on that result.
The return/result was not captured; output type, successful allocation and geometry
ownership cannot be inferred from this wrapper.

The already available complete eight-byte helper at **0x800760240** preserves its
incoming destination in RBX. It branches on reader byte +0x0c. The observed return
**0x8007602af** follows the non-swapping `call rax` at **0x8007602ad**: EDX=8,
RSI=RBX. The swapping branch instead reads a local eight-byte buffer and reverses
its bytes into the destination. No repeat inspection of this helper is required.

The `kv 8` display also contains **0x8007730fa**, near the captured scalar-helper
frame. This is a useful **candidate return**, not a verified frame walk: deeper
`kv` entries are unreliable across guest frames. We will test whether the prepared
code has an exact direct call to 0x800760240 ending there, then inspect only its
bounded neighborhood. A matching call encoding or prologue pattern still does
not prove runtime execution, a function name or an allocation base. The further
displayed value 0x8007595ff is not followed in this step.

## Descriptor: layout evidence versus provenance

The saved caller R15 at `[dispatcher RBP-8]` was **0x200776b390**. This comes from
the verified prologue's saved register, not an arbitrary pointer scan. Its 56-byte
snapshot, together with existing read-start.json, yields:

| Offset / width | Captured value | Supported interpretation |
|---|---|---|
| +0x00 / u64 | 0x20072cf100 | pointer-shaped value; referent unknown |
| +0x08 / u32 | 0x06c9a1ce = 113877454 | equals this payload's RPK file offset; prior caller code loads it into ECX before reader creation |
| +0x0c / u32 | 0x46c = 1132 | equals independently located payload size |
| +0x10 / u32 | 0 | unknown |
| +0x14 / u32 | 0x0003d12c | matches high dword of copied/TOC ID |
| +0x18 / u32 | 0xcbd11945 | matches low dword of copied/TOC ID |
| +0x1c / u32 | 0 | unknown |
| +0x20 / u64 | 0 | unknown |
| +0x28 / byte | 1 | unknown |
| +0x29 / byte | 0 | unknown |
| +0x2a / byte | 3 | selector actually used for manager array lookup |
| +0x2b / byte | 3 | separate byte; not established to have the selector's meaning |
| +0x2c / u32 | 0 | unknown |
| +0x30 / u64 | 0x20077d8298 | earlier caller invokes its +0x18 slot with ESI=0; effect unknown |

`(u32(+0x14)<<32) | u32(+0x18)` reconstructs the ID. Reading eight bytes directly
at +0x14 would reverse those dword roles on a little-endian CPU and give the wrong
result. This is **not a raw on-disk TOC entry**: dcmod's RPK parser reads contiguous
u64 ID, u32 size, then u32 offset (`tools/modkit/src/rpk.cpp`), whereas this live
descriptor has different order and intervening fields. Its fields correlate with
TOC metadata, but the native constructor/writer and any transformations remain
unobserved. Do not edit serialized RPKs using this in-memory layout.

For Road742 the independently known comparison tuple is offset **113879809**
(0x06c9ab01), size **563052** (0x8976c), ID high **0x0003a890**, low
**0x8986f62b**. It was not observed in this descriptor. Both IDs have dcmod type
3 (`vertex_buffer`), making reuse of the same native consumer a useful hypothesis;
one selector-3 observation does not prove that mapping for every resource.

## Implemented bounded offline follow-up

`--resource-consumer` checks the known linked slot and exact call encoding at
0x800742de2, then examines only:

1. 256 bytes from **0x800742db0** to complete the selected wrapper.
2. 1024 bytes from its justified direct callee **0x800739c10**.
3. A 384-byte neighborhood of candidate return **0x8007730fa**, only if the
   expected five-byte direct ID-helper call is present. It reports forward code
   and at most two prologue-pattern leads from the preceding 128 bytes. A lead
   is marked separately for whether its decode reaches the expected call boundary.

There is no recursive following, executable/vtable search, runtime access or
repetition of previous profiles. The common failure-path call at 0x80123e420 is
an explicit review boundary; walking stops there without assuming a noreturn
symbol or decoding adjacent routines. Other calls remain listed but unfollowed.
Existing mapping, patch/relocation overlap and before/after input-hash checks are
unchanged. A missing optional candidate is reported without discarding the two
confirmed-target windows; known slot/call mismatches abort. Blocked windows remain
blocked. All tests use original synthetic fixtures, not supplied game bytes.

This is the next necessary **offline** experiment. The prepared image is not
available in this workspace, and the attached first-block report ends at the
call to 0x800739c10; its missing bytes cannot be reconstructed from names or IDs.
Do not run the game to reproduce the confirmed ID copy. The new report should
show whether 0x800739c10 creates/obtains an object, which parsing method receives
the reader, where ID/version/size fields are stored, and whether the scalar-read
caller lead joins that chain. Unknown indirect edges must remain unknown.

## PowerShell: offline only, using existing prepared files

No MSYS2/game build, preparation, RPK scan, dependency installation, save access,
BB variables or WinDbg session is needed. The script retains existing variables
when available and otherwise restores the documented prepared paths/venv.

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
$Run="$Repo\work\road742-resource-consumer-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$TestArgs=@('-m','unittest','test_native_caller','test_native_control_flow','test_resource_consumer','-v')
$Test=Start-Process $Py -ArgumentList $TestArgs -WorkingDirectory "$Repo\tools\track_import\tests" -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline tests failed; inspect logs' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--resource-consumer',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\resource-consumer.json`"")
$Inspect=Start-Process $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw 'Inspection refused input; return error logs without bypassing checks' }
Write-Host "Return resource-consumer.json, commit.txt and the four logs from $Run"
```

Do not add previous profile flags. Return blocked/candidate-mismatch results as
they are. No original image, patch file, RPK, executable or save should be uploaded.
The inspector rechecks file hashes internally before creating its report.

## Validation and actual custom-track blocker

All **95 Python tests passed**, no skips/failures, with the existing dcmod binary
and synthetic RPK integration fixture. Local log:
`work/native-static-validation/consumer-tests.log` (not committed). New coverage
checks fixed-target guards, slot/call mismatches, strict overlaps, optional-candidate
failure, bounded prologue leads, unchanged inputs, exclusive profiles/no repeated
searches, and explicit call-boundary stops without changing the default walker.
Windows execution of this new offline profile is pending; no runtime code changed.

Interruption recovery found these seven source/test/documentation changes intact,
with the complete passing 95-test log, and the fork still at 4b43a06. Final review
also ran the documented module-based test command from the tests directory:
**29/29 passed**, logged locally in
`work/native-static-validation/consumer-focused-final.log`. No completed Windows
experiment was repeated and no asset, save or prepared image was changed.

The visible custom-track blocker is still the missing link from **Road742's own
resource payload** to its native decoded buffer and draw submission. We now have
a real parser path for another vertex-buffer payload, which provides a much
stronger route to that link than shader guesses or broad memory scans. The next
parser bodies may establish a precise resource-ID-conditioned boundary or expose
a discrepancy with dcmod's vertex-buffer layout. Neither is assumed now. No
current evidence justifies changing the importer, declaring native collision
compatible, or saying Tsukuba is rendered/drivable.
