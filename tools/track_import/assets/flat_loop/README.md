# Original flat loop

Original procedural geometry created for this prototype; GPL-2.0-or-later, like
the importer. No DriveClub, Assetto Corsa or Tsukuba data is included.

100 x 60 metre outer rectangle, 80 x 40 metre inner hole, 10 metre road width,
Y=0, 8 vertices, 8 upward-facing triangles. Spawn suggestion [0, 0.75, -25],
heading 90 degrees (authoring convention: zero faces +Z, positive turns to +X).
The heading is metadata, not a confirmed DriveClub heading convention.

Reproduce in a new directory:

```sh
python3 tools/track_import/track_import.py generate -o work/my-flat-loop
python3 tools/track_import/track_import.py check work/my-flat-loop/track.json
```

Import `road.glb` in Blender to inspect it. The package is authoring input,
not an independently playable DriveClub track. See
[Windows guide](../../../../docs/modding/external-track-windows.md).
