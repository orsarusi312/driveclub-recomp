# External-track validation record

Executed 2026-11-11 on Linux x86-64, GCC/G++ 14, Python 3.12.14, Blender 4.3.2.
See test logs in the downloadable deliverable for exact output.

| Command | Result |
|---|---|
| `bash tools/modkit/build.sh` | PASS: real `out/dcmod` built with bundled Lua/ATRAC9 |
| `bash tools/modkit/test.sh` | PASS: `all checks passed`; ATRAC9 encoder decode/SNR checks passed |
| `bash tools/track_import/test.sh` | PASS: build, synthetic fixture, 7 Python tests including dcmod integration, Blender smoke |
| `DCMOD=... TRACK_FIXTURE=... python3 -m unittest discover -s tools/track_import/tests -v` after final validation changes | PASS: 7 tests, no skips; legacy RPK byte-identical round trip, smaller/equal/larger isolated imports, indices/positions/normals/UV checks, unchanged shared-buffer neighbor |
| `blender --background --python-exit-code 1 --python tools/track_import/tests/blender_smoke.py` | PASS: world translation baked; Y-up road orientation and offline support verified |
| `python3 tools/track_import/track_import.py check tools/track_import/assets/flat_loop/track.json` | PASS: 8 vertices, 8 triangles; correct hole and spawn support |
| `git diff --check` | PASS |

Python tests also cover malformed/truncated GLBs, nonidentity transforms,
unsupported extensions, multiple primitives, accessor overrun within a view,
external buffers, nonfinite positions, invalid indices, degenerate triangles,
spawn over the hole/below road, path traversal, output overwrite rejection,
failed-target staging without output, slopes and upward/downward winding.

Blender prints a missing optional Draco library message on this host. Plain,
uncompressed GLB export completed and passed the strict reader; compressed
GLB is intentionally unsupported. No Draco output was used.

Not executed: full game/GPU runtime build, game-data `realdata.sh`, a Windows
binary execution, in-game render/collision/race tests, or a Tsukuba KN5 conversion.
The environment supplied no game dump or third-party track model. The Windows
guide is a reproducible procedure, not a claim that Windows/game tests ran.

All test geometry and synthetic RPKs are original. No original game dump was
modified. The staged fixture uses a `worlds/test.rpk` output path only inside a
temporary loose-file mod and is not a valid standalone game world.
