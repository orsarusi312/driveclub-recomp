# Multi-stream road decoding and the next Windows scan

This work continues `5936751` and preserves the dependency resolver and original
flat-loop asset. The full uploaded report exceeded the environment's 32 MiB
transfer limit; it was not repeatedly downloaded. Analysis uses the two slot
entries pasted by the user. No proprietary RPK or native runtime is available
here. The report is diagnostic data, not execution instructions.

## Evidence and cause

Both road parts use a position format with `kind=1,type=9` followed by
`kind=2,type=15` at a stride of 16 bytes. The existing decoders establish a
12-byte XYZ prefix followed by the 4-byte packed normal at offset 12.
Previously `FindSubs` classified this buffer as positions and read normals
only from one nearest non-position attribute stream. The declaration was
available but excluded by the single-stream representation.

| Reported slot | Position/normal stream | Matching other stream | Expected decoding after local rescan |
| --- | --- | --- | --- |
| `000862187dac760f_0_polyRoadShape742` (47 vertices, 65 triangles) | `0003a8908986f62b`, 35,189 buffer vertices, stride 16 | `0003409379c4d442`, same count, stride 12: float UV at offset 0 then kind 6/type 19 | Normal at offset 12; UV bytes 0–7; opaque suffix bytes 8–11 |
| `000862187dac760f_1_polyRoadShape742` (82 vertices, 108 triangles) | `0003c9c607361b51`, 104,824 buffer vertices, stride 16 | `0003dca53d7b3b01`, same count, stride 4: kind 6/type 19 only | Normal at offset 12; no declared matching UV stream |

These are expectations from descriptor metadata, **not a declaration that the
real slot passes all structural checks**. The source RPK must still establish
unambiguous draw bindings, safe replacement references and valid ranges. Part 1
must remain incompatible with the UV-bearing flat-loop asset; part 0's UVs
cannot be borrowed across different buffer populations.

## Type 19 and bounded partial decoding

No source/documentation in this repository establishes the RPK type 19 value
encoding or maps it to an external graphics API enum. Numeric coincidences with
DXGI/Vulkan/GNM enums are not evidence. `MeshFieldWidth(19)` remains unsupported.
The two observed layouts bound an opaque suffix to four bytes. That does not
prove a universal four-byte type width, padding rules, color channels,
normalization or shader meaning.

`ParseFields` now retains offsets only through a fully known sequential prefix:

- Validate descriptor header, count and complete field records first.
- Check each known field fits within the stream stride.
- At the first unknown type, stop assigning offsets. No later field is decoded.
- Reject suffixes that cannot fit even their known widths plus a conservative
  nonempty lower bound for each unknown field. Do not infer the remaining widths.
- Fully known layouts still must match stride exactly.

A stream beginning with an unknown field yields no decoded attributes. A UV
prefix followed by an opaque tail can safely supply UVs without interpreting
that tail. The scanner explicitly reports `partial_layout`, per-stream offsets,
`known_prefix_decodable`, opaque suffix offset/byte budget and a warning.
Native materials and opaque attribute meanings remain unverified.

## Bindings, import and isolation

Normals, UVs and native tangent channels carry separate buffer IDs and strides,
including normals interleaved with positions. Additional streams must be named
in the slot's bounded record block and match the position buffer's vertex count.
Unmentioned buffers elsewhere in a mesh's dependencies are not borrowed.
Multiple providers of the same supported attribute are rejected as ambiguous;
inconsistent duplicate-record bindings and mismatched bound streams block import.
The existing fallback for exporting legacy shared-reference slots is retained;
it does not make those slots independently replaceable.

Isolated import clones every bound vertex stream with its original descriptor
reference, stride and full donor rows, then writes known positions/normals/UVs
and rebuilds supported packed tangents. All streams use the same source-row
selection: corresponding old rows for the existing range, nearest donor vertex
for added rows. Opaque bytes are copied, not synthesized or interpreted. The
index buffer appends a new range; original ranges and unrelated slot references
remain intact. Descriptor resources from dependencies remain external and read-only.

Multi-stream/partial-layout imports require `--isolate-buffers`. Known legacy
single-attribute imports retain their normal round-trip path. Buffer-reference
checks now also reject changing a binding that another slot inherits, even if
that other slot cannot itself be isolated. Unknown skin declarations cannot
silently become static-road candidates. Normal and UV requirements are unchanged.
GLB export includes supported normals and UVs; native tangent channels are
recomputed during import, but four-component glTF tangent handedness is not
claimed or invented.

## Exact Windows commands

Use PowerShell. These paths match the user's existing extracted files. Every
native command is checked; the output folder is new, and original RPK hashes
are checked before/after. Stop if your working tree is dirty.

```powershell
Set-Location H:\DC-Track-Test
function Assert-Native { if ($LASTEXITCODE -ne 0) { throw "Native command failed: $LASTEXITCODE" } }
if (git status --porcelain) { throw 'Preserve your local edits before updating.' }
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
Assert-Native
git switch feat/external-track-foundation
Assert-Native
git merge --ff-only FETCH_HEAD
Assert-Native
git rev-parse HEAD
& C:\msys64\usr\bin\bash.exe -lc 'cd /h/DC-Track-Test && bash tools/modkit/build.sh'
Assert-Native
$DC = (Resolve-Path .\out\dcmod.exe).Path
$Pack = (Resolve-Path .\work\visual-source\worlds\canada_road_circuit_02_n.rpk).Path
$Global = (Resolve-Path .\work\dependencies\global.rpk).Path
$Common = (Resolve-Path .\work\dependencies\worlds\canada_common.rpk).Path
$Deps = @('--dependency', $Global, '--dependency', $Common)
$Work = Join-Path $PWD ('work\multistream-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $Work | Out-Null
$Inputs = @($Pack, $Global, $Common)
$Hashes = @{}
foreach ($Path in $Inputs) { $Hashes[$Path] = (Get-FileHash $Path -Algorithm SHA256).Hash }
$Lines = & $DC mesh scan $Pack @Deps --target '*polyRoadShape742*'
Assert-Native
$Scan = ($Lines -join "`n") | ConvertFrom-Json
$Scan | Select-Object scanned_slots,reported_slots | Format-Table
$Lines | Set-Content -Encoding UTF8 "$Work\mesh-compatibility-multistream.json"
$Road = @($Scan.slots | Where-Object target -Like '*polyRoadShape742*')
$Road | Format-Table target,normal_decoded,uv_decoded,tangent_decoded,partial_layout,independent_buffer_mentions,compatible
$Road | Select-Object target,normal_status,normal_buffer,uv_buffer,blockers | Format-List
# Drop potentially huge lists of other slots sharing buffers from the small report.
$Small = @($Road | Select-Object target,vertices,triangles,position_buffer,attribute_buffer,normal_buffer,uv_buffer,tangent_buffer,normal_status,normal_decoded,uv_decoded,tangent_decoded,partial_layout,independent_buffer_mentions,relocation_buffers,vertex_streams,blockers,limitations)
[IO.File]::WriteAllText("$Work\polyRoadShape742-small.json", (ConvertTo-Json -InputObject $Small -Depth 20), [Text.UTF8Encoding]::new($false))
foreach ($Path in $Inputs) {
    if ((Get-FileHash $Path -Algorithm SHA256).Hash -ne $Hashes[$Path]) { throw "Input changed: $Path" }
}
```

`--target` filters only reported slots. Discovery, binding resolution and all
sharing checks still include the complete pack, so hidden neighbors cannot make
an unsafe slot look compatible. Omit this flag if you also want a full-pack
report; do not overwrite your earlier report. The reduced JSON above is the
preferred attachment for the next review.

Stage **part 0 only if the new scan says compatible**, rather than switching to
an unrelated mesh or dropping required UVs:

```powershell
$Target = '000862187dac760f_0_polyRoadShape742'
$Slot = @($Scan.slots | Where-Object target -EQ $Target)
if ($Slot.Count -ne 1 -or !$Slot[0].compatible) { throw 'Part 0 is not compatible. Return the small report; do not bypass blockers.' }
& $DC mesh export $Pack @Deps -o "$Work\donor-preview.glb" $Target
Assert-Native
$Bridge = @{
    schema = 'dc-visual-bridge-1'
    template = $Pack
    dependencies = @($Global, $Common)
    game_path = 'worlds/canada_road_circuit_02_n.rpk'
    target = $Target
    placement = 'donor_bounds_center'
    translation = @(0,0.2,0)
}
[IO.File]::WriteAllText("$Work\bridge.json", ($Bridge | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
py -3 tools/track_import/track_import.py stage tools/track_import/assets/flat_loop/track.json --bridge "$Work\bridge.json" --dcmod $DC -o "$Work\staged"
Assert-Native
git rev-parse HEAD | Set-Content "$Work\commit.txt"
foreach ($Path in $Inputs) {
    if ((Get-FileHash $Path -Algorithm SHA256).Hash -ne $Hashes[$Path]) { throw "Input changed: $Path" }
}
Get-Content "$Work\staged\import-report.json"
```

Inspect `donor-preview.glb` and `staged/placement-preview.glb` together in Blender.
Centering is mesh-local; no native scene/culling bounds or material was guessed.
Return the **small** JSON, commit and import report first. If staging succeeds,
the [isolated baseline/override commands](external-track-dependencies.md#isolated-baseline-and-visual-override-runs)
reuse `$Work` and `$Work\staged\files`. Supply your existing legal Base/Update,
prepared-file and runtime paths. Do not copy the override into the original dump.
Collect stock/override logs and two matching viewpoints before judging visibility.
The native game remains responsible for original collision and spawning.

## Verified milestone

`bash tools/track_import/test.sh` passed on Linux: dcmod build, all 55 Python
tests with no skips, native probe tests and Blender 4.3.2 smoke/hierarchy/instance
checks. Nine new multi-stream tests cover layout splitting, opaque suffixes,
separate tangents, ambiguous providers, mismatched counts, malformed descriptors,
inherited references, shared index ranges, isolated round trips and opaque-byte
preservation. The former missing-normal diagnostic assertion initially failed
because a generic layout error took precedence; diagnostic ordering was fixed
and the complete suite passed. Isolated UV writes now always publish supplied
values, avoiding retention of donor NaNs hidden by the legacy export sanitizer.

The stricter shared-reference regression now rejects both the inheriting slot
and the owner whose reference it inherits. The unknown-width regression now
places the unknown field before the required normal, testing that its offset
cannot be guessed; known-prefix behavior is separately tested. No test was
skipped to obtain a pass. Windows/Blender 5.3 execution remains user-side.

## Focused reporting and final hardening

`mesh scan --target NAME` accepts repeatable case-insensitive wildcard filters.
It fails on a filter with no matching slots. The report records `scanned_slots`
and `reported_slots`; sharing context is retained for filtered-out slots.
`--target` is rejected on non-scan commands to avoid silently ignoring it.

Descriptor metadata bytes 3 and 4 must be zero under the supported sequential
layout schema; nonzero values are reported and block decoding instead of being
assumed to have no effect on offsets. Raw field diagnostics now expose those
bytes. Present malformed position descriptors also block compatibility, even
though known position-format IDs can still support legacy geometry inspection.
Normal/tangent writes preserve the upper two packed bits whose meaning has not
been established; only the verified three 10-bit components are replaced. This
does not establish glTF tangent handedness or a shader meaning for those bits.

## Final validation and remaining gate

Verified on Linux on 2026-11-12:

| Check | Result |
| --- | --- |
| `bash tools/track_import/test.sh` | PASS: build, 57 Python tests without skips, native probe, Blender 4.3.2 smoke/hierarchy/instances/reflection |
| Same 57 Python tests with ASan/UBSan dcmod | PASS: 5.686 seconds, no sanitizer findings |
| `bash tools/modkit/test.sh` | PASS: all checks; ATRAC9 SNR 45.0 / 67.8 dB |
| Native probe rebuilt with ASan/UBSan | PASS: bounded reads, lifecycle, invalid/overflow and read-only checks |
| Portable motion-blur test | PASS: 27 cases |
| `git diff --check` | PASS |
| Windows 11 / Python 3.14 / Blender 5.3 / real RPK execution | NOT TESTED here; requires the user's local commands above |

The original 46 tests remain in the suite, alongside 11 multi-stream/focused
report tests. The one intermediate missing-normal error-message regression was
fixed. The final suite has no failures or skips. Sanitizers instrumented dcmod's
C++ sources; bundled Lua/ATRAC9 objects were from the normal build.

Reproduce the sanitizer check after the regular test script has built its fixture:

```bash
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -fno-pie -no-pie -Ithird_party/LibAtrac9/C/src tools/modkit/src/*.cpp \
  out/lua51/*.o out/atrac9/*.o -o out/dcmod-asan
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  DCMOD="$PWD/out/dcmod-asan" TRACK_FIXTURE="$PWD/work/track-fixture.rpk" \
  python3 -m unittest discover -s tools/track_import/tests -v
```

The decoder milestone was preserved and remotely verified as `6713b3d` before
the focused-report/packed-bit hardening follow-up. Changed implementation files
are `tools/modkit/src/cmd_mesh.cpp` and `mesh_formats.hpp`; regression files are
`tools/track_import/tests/test_multistream.py`, `test_mesh_scan.py` and
`test_mesh_dependencies.py`. Documentation changes cover the modding README,
dcmod/scanner reference and this handoff. No game dumps, private logs or large
scan reports are committed; all test payloads are original synthetic data.

| Area | Status |
| --- | --- |
| Multi-stream decode and isolated staging | IMPLEMENTED / TESTED with synthetic RPKs |
| Real polyRoadShape742 part 0 structural compatibility | NOT TESTED after this change; expected fields are supported, binding/range checks must pass locally |
| Real polyRoadShape742 part 1 with UV-bearing flat loop | BLOCKED by no matching UV declaration in supplied metadata |
| Native rendering, culling and material behavior | NOT TESTED; retain existing native scene/material references |
| Native collision and custom vehicle spawning | BLOCKED on the documented native ABI/lifecycle investigation; unchanged |
| Playable custom flat-loop or Tsukuba | NOT IMPLEMENTED in-game |

The next experiment is ready **conditionally**: rescan, verify part 0 is
compatible, stage and inspect its round trip, then run the isolated stock/override
comparison. If binding checks still fail, return the reduced report rather than
relaxing the checks. If staging succeeds but geometry is absent, the next gate
is measured world placement, native scene/culling ownership and GPU draw evidence;
source names and successful file replacement alone are not rendering proof.

Windows has since confirmed startup/driving and replacement of the Road742 RPK,
but not visible custom geometry. Continue with the
[bounded visual-marker and draw-recording experiment](external-track-visual-marker.md),
using separate stock, isolated-control and inset-marker runs.
