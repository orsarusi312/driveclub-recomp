# Private Assetto Corsa input inventory

`tools/track_import/ac_inventory.py` creates metadata from a locally supplied
track directory. It discovers `models*.ini`, reads each `[MODEL_n]` `FILE`
reference, records raw `POSITION`/`ROTATION` strings, hashes KN5 files without
loading them into memory, and reports missing or ambiguous references. The
script does not change the input folder or copy model/texture data.

This is an implemented preparation step, not a KN5 decoder or an engine
collision converter. A file called `road.kn5` is not accepted as proof of the
real drivable road. Header/version parsing is deliberately absent: there is
no supported KN5 decoder dependency in this implementation. Each file report
has `format_validated: false`; `inventory_ready` only means that a layout was
selected and its referenced model files exist.

## Windows usage

From the repository root with Python 3.10+:

```powershell
py -3 tools/track_import/ac_inventory.py 'D:\PrivateTracks\tsukuba' --models models_tc2000.ini -o 'D:\TrackWork\tsukuba-inventory' --licence-note 'Local record of applicable licence and permission'
if ($LASTEXITCODE -eq 1) { throw 'Inventory failed; read stderr' }
Get-Content 'D:\TrackWork\tsukuba-inventory\inventory.json'
Get-Content 'D:\TrackWork\tsukuba-inventory\blender-preparation.json'
```

`models_tc2000.ini` is an example, not an asserted name in your mod. Omit
`--models` on the first run to discover layouts; a unique INI is selected
automatically, while multiple INIs leave selection pending. Use the exact
package-relative path from the report. Output must be a **new directory outside
the source track folder**. Existing outputs and symlink outputs are refused.

Exit codes:

| Code | Meaning |
|---|---|
| 0 | One layout selected and all of its declared model paths present |
| 2 | Reports written, but layout selection, model declarations or dependencies need attention |
| 1 | Invalid input, unsafe path, malformed INI, existing output or filesystem error |

`FILE` references resolve relative to the containing INI. The normal case of
an INI at the track root therefore resolves to that root. Windows separators
are accepted; absolute paths, drive letters and `..` are rejected. References
are matched case-insensitively to support Windows-authored mods on Linux;
multiple case variants are reported as ambiguous rather than guessed.
Symlink files/directories are recorded and skipped. UTF-8/BOM and Windows-1252
INI text are supported. Duplicate keys/sections are rejected. The inventory
caps discovery at 20,000 files and each INI at 1 MiB; KN5 hashing streams data
in 1 MiB chunks.

## Use the preparation scaffold

The generated `blender-preparation.json` has empty role lists for `road_render`,
`road_collision`, `walls`, `barriers`, `terrain` and `scenery`, and unset scale,
origin and spawn observations. It deliberately is not a runtime `track.json`.
It does not assign physical roles from object or file names.

1. Confirm the intended layout is TC2000, with the approximate 2.045 km length
   as a survey sanity check, not a scale factor to force onto the mesh. Record
   the applicable licence and allowed private conversion/distribution scope.
2. Import the referenced KN5 models with a compatible user-supplied Blender
   add-on or authorized local conversion tool. Record its name/version and the
   Blender version. The inventory does not launch unknown importers.
3. Inspect actual meshes and model placements. Record exact Blender object
   names by role. Verify the true road contact surface, elevation, barriers and
   start area. A scenery render mesh can differ from the collision surface.
4. Verify model pivots, units and travel direction. `POSITION` and `ROTATION`
   were preserved as strings, not applied or interpreted. Record how the chosen
   importer handles those transforms and inspect the combined scene before
   baking them.
5. Export a small road-only section through the existing
   `blender_export_road.py` and validate it as described in
   [the private conversion guide](external-track-conversion.md). Full-track
   preparation must follow the current importer/backend limits.
6. Define track-local spawn and inspect its real supporting surface. Empty
   metadata is not replaced with an invented safe point. Game spawn/collision
   resources still require the native integration described in the engine
   investigation.

The report lists loose texture **candidates**, not verified material
assignments. KN5 may embed textures; those dependencies are uninspected.
`surfaces.ini`, `fast_lane.ai`, `pit_lane.ai` and `ui_track.json` are listed as
reference files only. No AC friction/AI values are copied into DriveClub.
The existing road-only Blender exporter removes materials, so this inventory
does not claim texture preservation through to the game.

## Validation

Eight standard-library tests run entirely on original synthetic input files:

```powershell
py -3 -m unittest discover -s tools/track_import/tests -p test_ac_inventory.py -v
```

They exercise complete/missing layouts, raw placement preservation, explicit
non-decoding status, case ambiguity, source immutability, output overwrite
protection, symlink exclusion, traversal/absolute path rejection, missing FILE
keys, duplicate INI declarations and CLI failure codes. No third-party track
models are part of the fixtures or tests.

Environment observation: Blender 4.3.2 `addon_utils.modules()` returned no
add-on whose module/name contained `kn5` or `assetto`. This bounded discovery
check does not prove that an unusually named converter is unavailable; no KN5
importer was invoked or tested. The capture is `work/ac-blender-capabilities.log`.
