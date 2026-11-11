# External-road prototype

Start with the [technical report](../../docs/modding/external-track-report.md),
[Windows 11 guide](../../docs/modding/external-track-windows.md), and
[private KN5 conversion preparation](../../docs/modding/external-track-conversion.md).

```sh
python3 tools/track_import/track_import.py check tools/track_import/assets/flat_loop/track.json
bash tools/track_import/test.sh
```

`test.sh` builds real dcmod and a synthetic RPK fixture, runs the Python tests,
and runs Blender smoke checks when Blender is on PATH. Run
`bash tools/modkit/test.sh` for the existing modkit suite as well.

This tool validates an original GLB road, checks offline spawn support and stages
an experimental **visual slot replacement** via dcmod. It does not create new
DriveClub physics, spawn points, boundaries, lap timing or AI. Generated RPKs
inherit game assets and must stay local. The committed flat loop and fixture
source contain original data only. Source is GPL-2.0-or-later.

The [continuation report](../../docs/modding/external-track-progress.md) covers
v2 collision/spawn authoring, GLB/glTF dependency preflight, native contact
diagnostics, current status and transfer instructions. V2 is offline only.
