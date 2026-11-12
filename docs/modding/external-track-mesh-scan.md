# Finding a compatible visible-mesh slot on Windows

`dcmod mesh scan PACK` reads the pack and emits JSON to stdout. It never writes
the RPK or accepts `-o`. It uses the same submesh discovery as import/export.
The report covers recognized slots only, not every possible engine geometry type.

The reported `polyRoadShape742` staging failure proves only that the selected
attribute stream had no **decoded** normal. Export only emits NORMAL when
`Sub.normal_off >= 0`; the pipeline correctly requires it. The decoder recognizes
normal kind 2/type 15, tangent kind 5/type 15/index 6 and UV kind 5/type 8.
It discards all field offsets when its layout sizes do not match buffer stride.
At the original scanner milestone, `FindSubs` chose a non-position stream with the same vertex count; its format
must be a resource available in that pack. Formats in required/common packs are
not automatically resolved. Explicit `--dependency` support is now available; see
[dependency resolution and Windows commands](external-track-dependencies.md). Therefore successful position/triangle export does
not distinguish absent normals from unsupported or unresolved attributes.
The source evidence is `ParseFields`, `FindSubs`, and `ScanMeshes` in
[`cmd_mesh.cpp`](../../tools/modkit/src/cmd_mesh.cpp); the mandatory staging
preflight is `stage` in [`track_import.py`](../../tools/track_import/track_import.py).

The user's real RPK is not available in the development environment. No claim
has been made about which cause applies to either polyRoadShape742 slot.

`normal_status` distinguishes:

- `decoded`: selected descriptor recognized and stride matched.
- `attribute_format_resource_missing_from_pack`: dependency cannot be inspected
  here; inspect `required_packs` and `vertex_streams[].format_id`.
- `attribute_format_descriptor_truncated`: incomplete field records; now also
  rejected by the decoder rather than accidentally accepting a partial layout.
- `normal_semantic_present_but_type_unsupported`: kind 2 exists, encoding unknown.
- `normal_present_but_decoded_layout_does_not_match_stride`: known normal type,
  but overall stream layout cannot safely be decoded.
- `no_normal_semantic_in_selected_descriptor`: no kind 2 in this descriptor;
  this is NOT proof there are no normals elsewhere or shader-derived normals.
- `no_attribute_stream_with_matching_vertex_count`: importer cannot select a
  usable attribute stream. Other listed vertex streams are evidence to inspect.

Each slot reports vertex/triangle counts, decoded normal/tangent/UV flags,
position/attribute/index IDs, raw field kind/type/index, stream counts/strides,
format completeness, other slots sharing vertex and index buffers, independence of buffer ID
mentions, draw-table presence, blockers and limitations. Shared vertex buffers
are not inherently blocked: isolated imports can allocate new buffers when the
mesh has independently patchable references. Skinned slots are excluded from
static-road candidates. Tangents are optional, with a material-compatibility
warning. Known material/scene/culling limitations remain. No new encoding is
guessed and no normal requirement is relaxed.

`candidates` lists slots suitable for the current normal+UV flat-loop contract.
Ranking prefers a road name hint, then fewer source vertices (less nearest-vertex
attribute-copy work), then target name. No name or rank establishes visibility,
world placement, road ownership, material suitability or in-game success.

## Commands

Update your existing development branch from the fork (PowerShell; stop if any
command fails or the working tree has edits you need to preserve):

```powershell
git status --short
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git switch feat/external-track-foundation
git merge --ff-only FETCH_HEAD
```

Then rebuild in MSYS2 CLANG64:

```bash
bash tools/modkit/build.sh
```

In PowerShell from the repository root:

```powershell
$DC = (Resolve-Path '.\out\dcmod.exe').Path
$Pack = (Resolve-Path 'D:\YourExtractedFiles\worlds\canada_road_circuit_02_n.rpk').Path
New-Item -ItemType Directory -Force work | Out-Null
$Before = (Get-FileHash -Algorithm SHA256 $Pack).Hash
$Json = & $DC mesh scan $Pack
if ($LASTEXITCODE -ne 0) { throw 'Scan failed' }
$Scan = ($Json -join "`n") | ConvertFrom-Json
$Json | Set-Content -Encoding UTF8 work/mesh-compatibility.json
if ((Get-FileHash -Algorithm SHA256 $Pack).Hash -ne $Before) { throw 'Input hash changed' }
$Scan.slots | Where-Object target -Like '*polyRoadShape742*' | ConvertTo-Json -Depth 12
$Scan.slots | Where-Object compatible | Format-Table target,vertices,triangles,normal_decoded,tangent_decoded,uv_decoded,independent_buffer_mentions
$Candidate = $Scan.candidates | Select-Object -First 1
if (!$Candidate) { throw 'No compatible slot found. Review blockers; do not bypass them.' }
& $DC mesh export $Pack -o work/candidate-preview.glb $Candidate
if ($LASTEXITCODE -ne 0) { throw 'Candidate export failed' }
```

Inspect the candidate GLB in Blender. It is the best *structural* candidate,
not a verified visible road. To inspect another candidate, set `$Candidate`
to another exact name from `$Scan.candidates`. Preserve the entire scan report.

Stage the original loop using that candidate (new output folder required):

```powershell
$Bridge = @{
    schema = 'dc-visual-bridge-1'
    template = $Pack
    game_path = 'worlds/canada_road_circuit_02_n.rpk'
    target = $Candidate
    translation = @(0,0,0)
}
[IO.File]::WriteAllText([IO.Path]::GetFullPath('work/bridge-auto.json'), ($Bridge | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
py -3 tools/track_import/track_import.py stage tools/track_import/assets/flat_loop/track.json --bridge work/bridge-auto.json --dcmod $DC -o work/flat-loop-scan-test
if ($LASTEXITCODE -ne 0) { throw 'Staging failed; read the compatibility reason' }
```

The bridge now scans before importing and names exact incompatibility reasons,
with up to three candidate alternatives. If formats are unresolved it does not
try to copy a guessed stream from another pack. Translation remains in the
slot's inherited local frame; zero is only an initial experiment. Use the
isolated Windows test procedure in `external-track-runtime-probe.md` to check
actual rendering. Keep generated RPKs private. Collision and spawn are unchanged.

## Regressions

The existing full test command is `bash tools/track_import/test.sh`, followed
by `bash tools/modkit/test.sh`. Scanner tests exercise actual synthetic RPKs:
read-only hash preservation, supported shared buffers with independent mentions,
unsupported normal encoding, absent normal declaration, missing external format,
stride mismatch, truncated descriptor, unsafe shared references, missing draw
table and rejection of output arguments. Existing isolated-import regressions
also run through scanner preflight.

If Python reports skipped integration tests on Windows, build the fixture as
shown in `external-track-windows.md`, then set these before running unittest:

```powershell
$env:DCMOD = (Resolve-Path '.\out\dcmod.exe').Path
$env:TRACK_FIXTURE = (Resolve-Path '.\work\track-fixture.rpk').Path
py -3 -m unittest discover -s tools/track_import/tests -v
```

For further format investigation, provide `mesh-compatibility.json` (or both
polyRoadShape742 entries including `vertex_streams` and `required_packs`) and
any stage error. Raw game assets are not needed to identify the reported decoder
failure category. Runtime screenshots/logs are still required to establish
that the original flat loop is visible in-game.

## Development verification

Verified on Linux with GCC and Blender 4.3.2 using synthetic assets only:

| Check | Result |
| --- | --- |
| `bash tools/track_import/test.sh` | dcmod built; 36 Python tests passed, no skips; native probe and Blender smoke/hierarchy/instance/reflection checks passed |
| `bash tools/modkit/test.sh` | All checks passed; ATRAC9 encoder passed (45.0 / 67.8 dB SNR) |
| `g++ -O2 -std=c++20 tools/motion_blur_test.cpp -o out/tests/motion_blur_test` followed by that executable | 27 cases passed |
| `bash tools/test_motion_blur.sh` | Cannot complete here: `clang++` unavailable; GCC fallback clock test requires `windows.h`, and launcher test requires Windows libraries |
| Real Canada RPK scan and Windows runtime | Not tested; user's scan report and in-game evidence required |

Seven new scanner tests cover the scenarios above plus missing attribute buffers,
truncated vertex buffers, shared index-buffer reporting, and staging rejection
before publication. No normal-decoding requirement was removed. This change
does not implement native road collision or vehicle spawning.

The current [multi-stream decoder](external-track-multistream.md) additionally
resolves per-attribute bindings, including normals in position streams, and
permits bounded known prefixes while retaining opaque suffixes. Refer to that
guide for current compatibility rules and focused reports.
