# Vertex setup: exact virtual slots and the reader byte-count edge

Continued clean local/remote `feat/external-track-foundation` at
`99d7c1e0a817df3c3f5550836c21f2b6f2b49ff9`; no unfinished changes were present.
Read the complete supplied vertex-parser.json (41,120 bytes). Input hashes match
earlier prepared-image/patch reports; the user reports all 32 focused Windows
tests passed. No runtime experiment, save, asset or original archive changed.

## Evidence-backed data dependencies

The coherent 0x800759140 body includes a normal return at 0x800759249 and a
separately bounded failure-path call. Its first ordinary call is important:
**0x800759165 -> 0x800742b70**, before it reads object+0x20/+0x28. At that call,
RDI still holds the incoming object and RSI the reader (also preserved in R14).
This is a justified candidate for the missing initialization link; its body is
absent from the supplied report. The empty direct-call lists only ruled out a
direct call to 0x8007595d0/0x8007730c0 in this inspected body. They did not rule out
this intermediate call. No such connection is asserted until its code is read.

The constructor-like routine then stores this **derived address point**:

```text
0x80075916a + 7 + 0xe94ccf + 0x10 = 0x8015ede50
```

The separate 0x8007595d0 candidate stores **0x8015edeb0**, a different address
point. Resolving its slots would not answer the calls in 0x800759140. The exact
slots needed for that latter body are:

| Native call | Offset | Slot address to interpret through relocation metadata |
|---|---|---|
| 0x80075920a | +0x30 | 0x8015ede80 |
| 0x800759228 | +0x40 | 0x8015ede90 |

These are static predictions conditional on the object retaining the stored
vptr. The report contains the store, **not these slots' relocation records or
live values**, so their concrete code targets are not yet available here.

### Pointer-returning helper and local setup

0x800753bf0 is called with EDI=object+0x28, ESI=0x10, EDX=3, and ECX=reader+0x14.
Its RAX result goes to object+0x58. The nibble `(object.u32_at_20 >> 20) & 0xf`
controls a second identical call: it occurs only when the nibble is neither 0
nor 5, and the second result goes to +0x60. Otherwise +0x60 remains zero.

After that branch, RAX is the **first** result (reloaded from +0x58 on the
two-call path). At 0x8007591ed, 0x800b4b2d0 receives:

```text
RDI = object+0x68
RSI = first pointer result
EDX = 0x4401
ECX = object.u32_at_2c
R8D = object.u32_at_30
```

This is consistent with a descriptor setup candidate. The remaining extent
0x78-0x68 is 16 bytes; the runtime's `AmdGpu::Buffer` representation in
`gpu/shadps4/video_core/amdgpu/resource.h` is also a descriptor with base address,
stride and record-count fields. Size/argument resemblance is **not identification**:
the helper's actual bit stores must be compared before calling it a GPU descriptor
constructor. Neither returned pointer is established as GPU memory, staging,
mapped storage, an allocation owner or a durable resource. No alignment or memory
pool meaning is assigned to arguments 0x10, 3 or reader+0x14 yet.

### The strongest candidate byte-transfer edge

The call and stores immediately afterward are now precise:

```text
call object.slot_30(
    RDI=object, RSI=&local_at_rbp_minus_40,
    EDX=0, ECX=object.u32_at_28, R8D=1)

EAX = low32(object.u32_at_2c * object.u32_at_30)
local.u32_at_08 = EAX
call 0x8007abd00(RDI=reader, RSI=&local)
call object.slot_40(RDI=object)
```

Thus the product **does become part of the local data passed to a reader-related
helper**. What is still missing is whether slot +0x30 writes a destination pointer
there, whether 0x8007abd00 interprets that local as pointer/count and performs a
read, and what slot +0x40 finalizes. Do not label the calls map/read/unmap or upload
without their bodies. The product is 32-bit IMUL; no overflow check is visible in
this body. This does not justify weakening dcmod's geometry/range validation.

## Separate header/parser candidate and serialized layout

The body at 0x8007595d0 calls 0x8007730c0 with the reader, then:

1. Incorporates its incoming ESI low nibble into object+0x20 bits 16..19, sets
   bit 24 and preserves bits 28..31. These flags' meanings remain unknown.
2. Calls 0x800760160, then loads a 16-bit local, masks its low nibble and stores
   that nibble in object+0x20 bits 20..23.
3. Calls the same helper again, zero-extends the 16-bit local and writes +0x2c.
4. Calls the established four-byte reader, stores its output at +0x30 and stores
   the low 32-bit product of +0x2c and +0x30 at +0x28.

The 16-bit load/store behavior is visible, but 0x800760160's actual requested
read width and swap behavior have not been inspected. The new profile includes
only that small helper to close this specific assumption. The common-header
candidate already reads ID and then a four-byte scalar consistent with version.
If these calls perform successful sequential reads from that position, the next
fields align with dcmod's vertex-buffer layout:

| Serialized offset | Existing dcmod interpretation | Native candidate destination/use |
|---|---|---|
| +20 | two-byte field preceding stride | low nibble into object flag bits 20..23 |
| +22 | u16 stride | object+0x2c |
| +24 | u32 count | object+0x30 |
| +28 | vertex records begin | subsequent bulk-read hypothesis |

This is a layout comparison, not a verified execution chain or byte transfer.
For Road742, `16 * 35189 = 563024`, exactly its payload size 563052 minus 28.
That arithmetic is consistent with the hypothesis, but the only observed native
ID read remains **0003d12ccbd11945**, not **0003a8908986f62b**.

## Implemented offline follow-up: --vertex-transfer

The new profile verifies the exact RIP-relative vptr store, direct-call encodings
and two virtual-call instructions from the supplied report. It then resolves
only the two exact slots through existing guarded kind-0 relocation metadata and
inspects their predicted targets, at most 512 bytes each. It also inspects five
specific direct dependencies, without recursion:

| Target | Maximum code bytes | Concrete question |
|---|---:|---|
| 0x800742b70 | 512 | Does the initial call connect to 0x8007595d0? |
| 0x800753bf0 | 512 | What operation produces the two stored pointer results? |
| 0x8007abd00 | 512 | Does the local+8 product control an actual reader request? |
| 0x800b4b2d0 | 256 | What is written at object+0x68? |
| 0x800760160 | 128 | Is the observed 16-bit scalar handling backed by two-byte reads? |

Per-window limits remain 256 instructions/32 block starts. Returns, traps and
reviewed failure-path calls stop decoding; unresolved external edges and truncation
remain explicit. Shared targets with the same window size are read once. A slot
with missing/ambiguous/import/addend/patched metadata is reported blocked and is
never guessed. An unexecutable, patched or relocated target window is refused.
Wrong provenance guards abort. No existing input/patch/mapping checks are relaxed.

This follows no unrelated caller graph and repeats none of the successful previous
profiles. The method targets and missing helper bytes exist only in the user's
prepared image; no original executable is available in this workspace. A further
game run is not justified until this offline evidence is available.

## PowerShell: offline only

No game build, launch, RPK scan, new preparation, dependency install or save access.
Reuse the established interpreter/image/patch paths; restore them automatically
if opening a new PowerShell session:

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
$Run="$Repo\work\road742-vertex-transfer-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$TestArgs=@('-m','unittest','test_native_caller','test_native_control_flow','test_resource_consumer','test_vertex_transfer','-v')
$Test=Start-Process $Py -ArgumentList $TestArgs -WorkingDirectory "$Repo\tools\track_import\tests" -Wait -PassThru `
  -RedirectStandardOutput "$Run\tests-stdout.log" -RedirectStandardError "$Run\tests-stderr.log"
if($Test.ExitCode -ne 0) { throw 'Offline tests failed; inspect logs' }
$InspectArgs=@("`"$Repo\tools\track_import\inspect_native_caller.py`"",'--vertex-transfer',
  '--image',"`"$Image`"",'--patches',"`"$Patches`"",'--output',"`"$Run\vertex-transfer.json`"")
$Inspect=Start-Process $Py -ArgumentList $InspectArgs -Wait -PassThru `
  -RedirectStandardOutput "$Run\inspect-stdout.log" -RedirectStandardError "$Run\inspect-stderr.log"
if($Inspect.ExitCode -ne 0) { throw 'Inspection refused input; return logs without bypassing checks' }
Write-Host "Return vertex-transfer.json, commit.txt and the four logs from $Run"
```

Return blocked slots/windows or budget stops as-is. Do not include earlier profile
flags, raise budgets, upload game binaries/RPKs or run a new debugger experiment.
The inspector verifies image/patch hashes again before writing the fresh report.

## Validation and remaining evidence gap

All **103 Python tests passed**, no skips/failures, using the existing dcmod binary
and synthetic RPK fixture. Local log:
`work/native-static-validation/vertex-transfer-tests.log` (not committed). Added
synthetic tests cover exact slot/target budgets, all guard categories, absent/
duplicate/partial/import/addend/patched slot metadata, blocked code windows,
deduplicated method reads, unchanged inputs and exclusive profiles/no repeated
searches. No native runtime, importer or asset-format behavior changed.
The documented focused invocation passed **37/37** tests
(`work/native-static-validation/vertex-transfer-focused.log`). All eight new
instruction guards also matched the supplied report's instruction bytes locally;
that checks guard transcription, not the missing method bodies or live dispatch.

The next report can close a static byte-count-to-read edge and a constructor-to-
header-parser edge. It cannot alone prove that Road742's particular resource
instance executes that path, that its bytes survive to a recorded GPU draw, or
that the isolated replacement is selected. Those remain the rendering blocker.
Native collision and spawning are still separate, unverified integration work.
