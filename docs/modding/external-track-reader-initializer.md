# Road742: static reader initialization and the next payload boundary

Continued a clean `feat/external-track-foundation` at
`92e158699e3f51efc5cd4de848f625fc49ec2c80`. The returned native-caller.json is
107,411 bytes; all three sites decoded. The Windows test log reports all ten
then-current inspector tests passed. No asset, runtime, save or experiment was
changed. These are original guest instructions, not available Evolution Studios
source or recovered official function names.

Input identity from the successful report:

```text
image SHA256   839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971
patch SHA256   6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761
image format   BBPROBE6
data offset    3441024
guest base     0x800000000
patch page     4096
```

## Reconstructed behavior at 0x8007aaf40

The prologue, coherent forward decode and balanced return at `0x8007aafc7` support
an initializer-shaped routine. This is a descriptive interpretation, not a symbol
name or proof of its complete C++ type. RDI is retained in RBX as an object pointer.
The observed body corresponds to the following limited pseudocode:

```text
save/compare a value loaded through a global pointer       // purpose not resolved
obj.u32_at_10 = 0
obj.u32_at_14 = 0
obj.u32_at_08 = 0
obj.byte_at_0c = 0
obj.vptr = 0x8015f3c40
call 0x80058bc90 with RDI = obj + 0x18                     // member setup candidate
obj.byte_at_28 = 0
call obj.vptr[+0x10] with RSI = local[8], EDX = 8
if little_endian_u32(local + 4) == 0x20474942:             // bytes "BIG "
    obj.u32_at_08 = 1
    obj.byte_at_0c = 1
compare saved/global value; mismatch calls 0x80123e420
return
```

The stack comparison looks like a protection check, but the failure callee has
not been resolved and is not assigned a name. There is no visible comparison of
the first four bytes with `EVOS` and no visible branch on the eight-byte read's
return value before the tag comparison. Validation/error handling may be elsewhere;
this body is not proof that every eight-byte input is a valid resource header.

`EVOSLITL` has `LITL` at offsets 4..7, so the BIG branch is not taken for the
observed data. The initializer's own writes therefore imply zero for both fields;
no live object-field snapshot was supplied. The table below deliberately separates
field behavior from guessed meanings:

| Offset | Established writes/use | Remaining interpretation |
|---|---|---|
| +0x08, u32 | initialized 0; set 1 for BIG | byte-order mode candidate; no scalar conversion observed |
| +0x0c, byte | initialized 0; set 1 for BIG | companion flag candidate; not proved to be a swap-enable flag |
| +0x10/+0x14, u32 | cleared | counters/state unknown |
| +0x18 | address passed to 0x80058bc90 | embedded reader-adapter candidate |
| +0x28, byte | cleared; adjacent routines test it before calling 0x80058bd10 | cleanup/ownership gate hypothesis, not a lifetime proof |

Adjacent coherent routines at `0x8007aafe0` and `0x8007ab020` reinstall the same
vptr and conditionally call `0x80058bd10`, then call/jump to `0x80058bd40` with
`obj+0x18`. They look cleanup-related; the callees' meanings remain unknown.
Linear bytes between functions are not interpreted as extra operations of the
initializer. In particular, the isolated `or al,0xc3` after its return is not a
reason to infer a getter or byte-order conversion.

## Stronger candidate dispatch chain

RIP-relative address calculation is explicit:

```text
0x8007aaf75 + instruction length 7 + displacement 0xe48cb4 + 0x10
  = 0x8015f3c40
```

The initializer calls this object's slot `+0x10`. A nearby thunk has exact behavior:

```text
0x8007ab060: add rdi, 0x18
0x8007ab064: mov edx, edx       // zero-extends the 32-bit length
0x8007ab066: jmp 0x80058bd70
```

The report now contains a coherent prologue at `0x80058bd70` through its return.
That wrapper saves destination/length, handles zero length, gates calls using
global flags, and invokes the underlying object's slot `+0x18`; its return
`0x80058bdf3` was already hit in gameplay. However **the value in slot +0x10 is
not in the uploaded report**. It is premature to assert that it points to the
thunk; resolving that one static slot is the next decisive check.

The resulting candidate chain is:

```text
initializer 0x8007aaf40
  -> slot +0x10 at address point 0x8015f3c40       [target unresolved]
  -> candidate thunk 0x8007ab060, this += 0x18
  -> read/status wrapper 0x80058bd70
  -> underlying slot +0x18 -> buffered reader 0x800591e60
  -> verified eight-byte transfer
```

## What has not advanced to vertex consumption

The selected eight bytes belong to vertex-buffer payload `0003d12ccbd11945`,
not target position payload `0003a8908986f62b`. The target happens to be included
in the same 64 KiB refill. This initializer requests only eight bytes and contains
no subsequent visible resource-ID/version or vertex-array read.

The buffered-reader evidence shows an eight-byte logical advance, cursor 8 and
65528 bytes remaining. That is an advance past the first payload's magic, not
selection of Road742. A caller above this initializer must drive any subsequent
reads or seeks. Its identity and control flow are still missing. There is no
verified byte swap, vertex decoding, persistent allocation or rendering handoff.

## Next justified step: optional offline reader-details report

The inspector now accepts **`--reader-details`**. Default output remains compatible.
It uses the existing safe image/patch reader and adds:

1. Three guarded code windows for the initializer, member setup helpers and thunk.
2. Four bounded slots at the observed vtable address point. A slot is resolved only
   from one exact, unpatched, kind-0 relocation with zero addend and an executable
   target. This follows `src/probe.c`'s `image_base + relocation.value` rule as
   metadata arithmetic; no bytes are relocated or written. Import, missing,
   overlapping, duplicate or patched pointer records produce explicit refusals.
   This predicts linked dispatch, not live object identity.
3. At most 32 MiB of declared executable bytes searched for direct E8 calls to
   `0x8007aaf40`, with at most eight candidates and 192 bytes after each return.
   No recursive call graph, heap/asset scan or live tracing. Pattern matches may
   occur inside other instructions or embedded data and are labelled candidates.
   Indirect calls, tail calls and calls beyond the budget are not covered. A
   `complete` result describes only the bounded pattern scan, not all callers.

All code-window mapping, patch/relocation refusal and 4 KiB patch-padding rules
remain intact. Kind-0 pointer interpretation is a separate metadata operation;
it does not permit decoding relocated or patched bytes as code. Slot previews are
limited to 256 bytes and never establish full function boundaries automatically.

This report can reveal the dispatch method and caller continuation that reads the
resource ID/version or changes the resource selection. It may also show uses of
the BIG-related fields. No new runtime diagnostic is justified before those
static results are examined. The guest image is still not available in this
workspace, so those additional bytes cannot be decoded here.

## Minimal Windows commands: no rebuild or game launch

Use the existing `$Py`, `$Image`, `$Patches` from the successful offline run. See
the [previous guide](external-track-native-caller-static.md) to restore these paths
if reopening PowerShell. Keep the existing Python venv with Capstone 5.0.6.

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
git -C $Repo switch feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Branch switch failed' }
git -C $Repo pull --ff-only https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
if($LASTEXITCODE -ne 0) { throw 'Update failed' }
if((Get-FileHash -LiteralPath $Image).Hash.ToLowerInvariant() -ne '839d2995699c9179d6510ba773702b47a44eac8196038f23c27abe2ce3cbb971') { throw 'Prepared image changed' }
if((Get-FileHash -LiteralPath $Patches).Hash.ToLowerInvariant() -ne '6c74a934406609495aa36ae0f71ee84a976f6018c37a535790d8af6a0917a761') { throw 'Patch file changed' }
$Run="$Repo\work\road742-reader-details-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$TestArgs=@('-m','unittest','discover','-s',"`"$Repo\tools\track_import\tests`"",'-p','test_native_caller.py','-v')
$Test=Start-Process $Py -ArgumentList $TestArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline tests failed; inspect logs' }
$ArgsList=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--reader-details',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\reader-details.json`"")
$Process=Start-Process $Py -ArgumentList $ArgsList -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Process.ExitCode -ne 0) { throw 'Inspection refused input; return the detailed error' }
Write-Host "Return reader-details.json, commit.txt and the four logs from $Run"
```

The inspector compares both file hashes before/after internally. No RPK scan,
new preparation, dependency install, BB variables, WinDbg run or save access is
needed. Return only the generated report and logs, not binaries, assets or venv.

## Validation

All **80 Python importer/inspection tests passed**, using the existing dcmod and
synthetic RPK fixture (no rebuild). This includes **14 offline-inspector tests**.
New coverage verifies exact relative-pointer resolution from serialized metadata,
unchanged pointer bytes, refusal of import/partial/duplicate/patched/nonexecutable
targets, E8 matches across chunk boundaries without duplicates, byte/candidate
budgets, data-segment exclusion, patch-overlap refusal and guard failure. A
synthetic linked-image integration test exercises all four slots and a caller
candidate through the optional profile. Existing padding, code-window and default
report tests still pass. `git diff --check` passes.

Only the offline Python tool, its tests and documentation changed. No native code
or GPU path changed, so no game build, launch or native sanitizer run was needed.
Windows execution of the new optional profile remains unverified; the previous
report and its ten tests are real user Windows evidence, not results of this new
profile. No full guest binary, proprietary resource or private log is committed.
