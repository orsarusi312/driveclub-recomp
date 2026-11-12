# Explicit dependency packs for the visual track experiment

This continues `a934a32`. The user's single-pack Fraser Valley scan reported
2,641 slots, 464 candidates and 2,013 unresolved attribute-format resources;
both `polyRoadShape742` slots were unresolved. These are user-reported results,
not a scan reproduced in this Linux workspace. No real RPK or scan JSON has
been supplied here. `global.rpk` and `worlds/canada_common.rpk` are candidate
providers because the donor lists them as required; their names alone do not
prove they contain the missing IDs.

## Implementation and evidence

`dcmod mesh scan`, `info`, `export` and `import` accept repeatable
`--dependency PATH`. Paths must be explicitly supplied local RPKs. The
`MeshFormats` catalogue in `tools/modkit/src/mesh_formats.hpp` indexes only
`stream_format` resources by full 64-bit identifier. `FindSubs` and the scanner
use that catalogue for attribute decoding. Geometry and indices still come
only from the target pack. Dependencies' unrelated geometry/materials are not
imported or enumerated. Each dependency is parsed then released after retaining
its descriptors; the catalogue owns its copies throughout the operation.

There is no recursive loading, basename matching, network lookup, automatic
patch precedence, or archive merge. Same-ID descriptors must have identical
payloads and resource dependency lists; differing names/source metadata are
harmless. Conflicts anywhere in the supplied descriptor set fail with ID and
provider paths, including conflicts with the target. Use packs extracted from
the same game/update set and resolve conflicts before proceeding. Missing
explicit files or invalid RPK containers fail immediately. Missing IDs remain
unresolved; malformed descriptor headers/counts fail decoding and appear in
the scan report. No new normal encoding is inferred.

Each vertex stream now includes `format_sources` and `format_problem`. Top-level
`dependency_packs` records supplied paths and their own required-pack names.
`normal_decoded`, `uv_decoded` and `tangent_decoded` retain the scanner contract.
The known packed normal and float UV decoders are reused; packed tangent fields
are recognized and recomputed during isolated imports with UVs. GLB export
still does not export native tangents/handedness. Required pack names are
informational, not proof that all runtime resources are resolved.

The visual bridge JSON accepts optional `dependencies: ["path.rpk", ...]`,
relative to the bridge JSON or absolute. Staging forwards the same list to
scan, export, import and verification export. It records dependency SHA256s and
the selected slot report, and verifies inputs remain unchanged before publishing.
Output contains only the staged target RPK and existing bridge metadata, never
the dependency archives or descriptors. The target's required-pack list remains
intact. The game must still load its original required packs at runtime.

Safety fixes: descriptor payload IDs/magic are validated before interpreting
fields; mesh output cannot alias target/dependency files (including hardlinks
and symlinks); direct isolated imports reject undecoded required normals/UVs.
Existing non-isolated round-trip behavior is preserved on synthetic fixtures.
Do not redirect stdout over an input file: a shell can truncate it before dcmod
starts. All examples below use new report/output locations.

## Windows 11: update, build, extract and rescan

Run PowerShell from the existing clone. The first block requires a clean working
tree and stops on a failed native command. No main/modkit changes or force push.

```powershell
Set-Location H:\DC-Track-Test
function Assert-Native { if ($LASTEXITCODE -ne 0) { throw "Native command failed: $LASTEXITCODE" } }
if (git status --porcelain) { throw 'Preserve local edits before updating.' }
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
$Work = Join-Path $PWD ('work\dependency-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $Work | Out-Null
```

Adjust the MSYS2 path if it is installed elsewhere. Find the legal Base/Update
folders containing `game.ndx`/`gameNNN.dat` using your existing installation's
`launcher.ini` or known dump paths. Set only these two placeholders; do not
choose an output directory inside either source. The archive reader searches
Update before Base. `list` confirms the two paths before extracting:

```powershell
$Base = 'H:\YOUR-LEGAL-DUMP\Base'
$Update = 'H:\YOUR-LEGAL-DUMP\Update'
& $DC archive list global.rpk worlds/canada_common.rpk -l --base $Base --update $Update
Assert-Native
& $DC archive extract global.rpk worlds/canada_common.rpk -o "$Work\deps" --verify --base $Base --update $Update
Assert-Native
$Global = (Resolve-Path "$Work\deps\global.rpk").Path
$Common = (Resolve-Path "$Work\deps\worlds\canada_common.rpk").Path
$Deps = @('--dependency', $Global, '--dependency', $Common)
$Inputs = @($Pack, $Global, $Common)
$Hashes = @{}
foreach ($Path in $Inputs) { $Hashes[$Path] = (Get-FileHash $Path -Algorithm SHA256).Hash }
$Lines = & $DC mesh scan $Pack @Deps
Assert-Native
$Scan = ($Lines -join "`n") | ConvertFrom-Json
$Lines | Set-Content -Encoding UTF8 "$Work\mesh-compatibility-resolved.json"
foreach ($Path in $Inputs) {
    if ((Get-FileHash $Path -Algorithm SHA256).Hash -ne $Hashes[$Path]) { throw "Input changed: $Path" }
}
$Scan.slots | Group-Object normal_status | Sort-Object Count -Descending | Format-Table Count,Name
$Road = @($Scan.slots | Where-Object target -Like '*polyRoadShape742*')
$Road | Format-Table target,normal_status,normal_decoded,uv_decoded,tangent_decoded,compatible
$Road | ConvertTo-Json -Depth 15 | Set-Content -Encoding UTF8 "$Work\polyRoadShape742-resolved.json"
$Road | ForEach-Object { $_.vertex_streams | Where-Object selected_attributes } | ConvertTo-Json -Depth 10
```

If already extracted, assign `$Global` and `$Common` to those files instead of
extracting them. Keep the before/after hash checks. A missing ID with both packs
supplied is still unresolved: inspect their required packs and `format_id` /
`format_sources`. Supply another explicitly extracted pack only when local
evidence identifies it. Do not mix arbitrary base/update versions.

## Select, inspect and stage

Prefer one of the two road slots only **if the resolved scan says compatible**.
Otherwise use the first structurally compatible candidate and inspect it. A
name, decoded normal or candidate rank does not establish visibility or position.

```powershell
$Candidate = $Road | Where-Object compatible | Select-Object -First 1 -ExpandProperty target
if (!$Candidate) { $Candidate = $Scan.candidates | Select-Object -First 1 }
if (!$Candidate) { throw 'No compatible slot. Preserve the reports; do not bypass checks.' }
$Candidate
& $DC mesh export $Pack @Deps -o "$Work\candidate-preview.glb" $Candidate
Assert-Native
# Inspect candidate-preview.glb in Blender before the rendering experiment.
$Bridge = @{
    schema = 'dc-visual-bridge-1'
    template = $Pack
    dependencies = @($Global, $Common)
    game_path = 'worlds/canada_road_circuit_02_n.rpk'
    target = $Candidate
    translation = @(0,0,0)
}
[IO.File]::WriteAllText("$Work\bridge.json", ($Bridge | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
py -3 tools/track_import/track_import.py stage tools/track_import/assets/flat_loop/track.json --bridge "$Work\bridge.json" --dcmod $DC -o "$Work\staged"
Assert-Native
foreach ($Path in $Inputs) {
    if ((Get-FileHash $Path -Algorithm SHA256).Hash -ne $Hashes[$Path]) { throw "Input changed: $Path" }
}
Get-Content "$Work\staged\import-report.json"
```

Translation is in the inherited mesh-local frame, not a verified world frame.
Zero is only a controlled initial experiment. Materials, scene transform,
visibility, culling, native collision and vehicle position remain inherited.
No bounds or scene records are guessed or rewritten.

For the baseline/override game experiment use the isolated run procedure in
[external-track-runtime-probe.md](external-track-runtime-probe.md), pointing its
staged `files` source to `$Work\staged\files`. Build the diagnostic game with
`BB_DIAGNOSTICS=ON bash build.sh` in MSYS2 CLANG64. Keep baseline and override
runs in separate new folders with private `--user`, `--mods`, GPU cache and logs;
never copy into the dump or original save folder. Keep both run directories and compare the override log against the stock baseline.

## Validation and remaining evidence

Synthetic tests cover missing files/IDs, corrupt packs, malformed descriptor
headers/counts, unknown normal encodings, exact duplicate providers, conflicting
providers (including local/external conflict), resolved normal/UV/tangent
layouts, no dependency-mesh enumeration, read-only input preservation,
byte-identical legacy round trips, output aliases, isolated normal requirements,
staging round-trip/isolation and malformed bridge dependency lists.

The real Fraser Valley rescan is pending local execution. Share only the two
JSON reports, `import-report.json`, commit SHA and runtime log excerpts/screenshots;
no copyrighted archives are requested. Resolution alone does not establish road
compatibility or in-game visibility. Collision, spawning and a playable custom
track remain blocked on native integration evidence.

Initial resolver milestone verified on Linux: dcmod build passed; 43 Python
tests passed without skips; modkit self-tests/ATRAC9 passed; Blender 4.3.2
smoke/hierarchy/instance/reflection tests passed; native probe tests passed
under AddressSanitizer/UBSan. Full dcmod sanitizer checks and subsequent
hardening are recorded separately when completed. Windows runtime and
proprietary-data tests have not been executed here.

### Decoder/accessor hardening follow-up

Review confirmed that the old field decoder treated every unknown attribute
encoding as four bytes. It now accepts widths only for implemented types
6, 8, 9, 15 and 24. `attribute_format_contains_unsupported_field_type` means
one of the other fields prevents safe offset calculation even if the normal
itself is type 15. Raw field codes remain available in the report. This may
reduce candidates formerly accepted through an unsupported width assumption.
It does not add new encodings.

Direct GLB import now rejects missing primitive/attribute/accessor tables,
multiple primitives, noninteger or negative accessor fields, unsupported
component/normalized/sparse encodings, misalignment and view overruns before
writing output. Previously some missing members could be dereferenced, and
an accessor could read beyond its declared view into another view in the BIN
chunk. Index byte arithmetic is promoted before multiplication, and short
resource mention searches avoid unsigned size underflow. No game ABI changes.

The resolver-only snapshot passed all 43 Python tests with dcmod built under
AddressSanitizer/UBSan (no sanitizer findings). After the decoder/accessor fixes,
the normal dcmod build and all 45 Python tests passed without skips. Two new
tests include 15 malformed-GLB variants and an unknown-width descriptor that
would previously have appeared decodable. Final sanitizer coverage is recorded
in the subsequent validation report.
