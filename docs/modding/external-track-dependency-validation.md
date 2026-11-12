# Dependency resolution and placement: validation record

Verified 2026-11-12 on Linux, continuing the preserved `a934a32` branch.
No DriveClub archive, Windows execution or native rendering evidence was
available in this workspace. The uploaded pasted text was the continuation
request, not the user's scan JSON. Real-pack findings remain user-reported.

## Saved milestones

- `8a033ac`: explicit descriptor catalogue; dependency-aware scan/export/import/
  staging; provider/conflict diagnostics; input/output protection. Pushed and
  verified on `orsarusi312/driveclub-recomp`, development branch only.
- `95cd71d`: unknown attribute widths fail decoding; direct GLB shape/range/
  alignment checks; widened index arithmetic. Pushed and remote SHA verified.
- This follow-up: measured donor vertex bounds, optional mesh-local centering,
  culling-related warnings, quantized placement preview and Windows handoff.

All original commits are retained. No reset, force push, main/modkit change,
original game data modification or copyrighted asset inclusion occurred.

## Test evidence

| Check | Result |
| --- | --- |
| `bash tools/modkit/build.sh` after final C++ changes | PASS: `Built /workspace/driveclub-recomp/out/dcmod` |
| Full Python suite with DCMOD and TRACK_FIXTURE set | PASS: `Ran 46 tests in 1.016s`, `OK`; no skips |
| Same suite with ASan/UBSan dcmod | PASS: `Ran 46 tests in 4.284s`, `OK`; no sanitizer diagnostics |
| `bash tools/modkit/test.sh` | PASS: `all checks passed`; ATRAC9 `ok ATRAC9 encoder`, SNR 45.0 / 67.8 dB |
| Native contact probe compiled with ASan/UBSan | PASS: gating, lifecycle, bounded reads, pose, materials, invalid/overflow, read-only |
| Blender 4.3.2 smoke test | PASS: road export; hierarchy, instances and reflected winding |
| Portable motion-blur regression | PASS: 27 cases |
| `git diff --check` | PASS |
| Windows-only motion-blur clock/launcher tests | Not run: require Windows headers/libraries; no Windows cross-compiler here |
| `tools/modkit/tests/realdata.sh` | Not run: requires local proprietary game data |
| Windows runtime, Vulkan rendering and PowerShell execution | Not run in this Linux environment |

The existing build/fixture gate is `bash tools/track_import/test.sh`. For this
iteration its build, Python, Blender and native-helper checks were run as
separate commands to avoid repeating unchanged builds. Reproduce the full
suite with that script and `bash tools/modkit/test.sh`.

Sanitizer commands used (after the normal build prepares Lua/ATRAC9 objects and
`out/track-fixture work/track-fixture.rpk` creates the synthetic fixture):

```bash
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -fno-pie -no-pie -Ithird_party/LibAtrac9/C/src tools/modkit/src/*.cpp \
  out/lua51/*.o out/atrac9/*.o -o out/dcmod-asan
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  DCMOD="$PWD/out/dcmod-asan" TRACK_FIXTURE="$PWD/work/track-fixture.rpk" \
  python3 -m unittest discover -s tools/track_import/tests -v
gcc -std=c11 -O1 -g -fsanitize=address,undefined -fno-pie -no-pie \
  -Wall -Wextra -Werror tools/track_probe_test.c -lm -o out/track-probe-asan
out/track-probe-asan
blender --background --python-exit-code 1 --python tools/track_import/tests/blender_smoke.py
g++ -O2 -std=c++20 tools/motion_blur_test.cpp -o out/tests/motion_blur_test
out/tests/motion_blur_test
```

Third-party Lua/ATRAC9 object files came from the normal build; they were not
recompiled with sanitizers. No multi-gigabyte geometry or real-pack performance
claim follows from the synthetic suite.

## Confirmed fixes and scope

- Missing local descriptors can be resolved by exact ID from explicitly supplied
  packs. Identical duplicates are accepted, conflicting payloads/dependency
  lists rejected, malformed headers/layouts refused. This is a descriptor
  resolver, not a general scene/material/resource merge or game loader.
- Descriptor magic and embedded IDs are checked. Unknown field types no longer
  inherit a guessed four-byte width. This can reject formerly accepted slots;
  raw codes/providers remain visible for further investigation.
- Direct import rejects absent JSON members that could be dereferenced, and
  accessor reads outside their declared bufferView that previously remained
  inside the global BIN allocation. Fractional/negative/oversized fields,
  unsupported encodings and multiple primitives fail before output publication.
- Index byte offsets are widened before arithmetic. Short resource searches
  avoid size underflow. Huge-pack arithmetic changes were source-reviewed;
  tests do not allocate gigabytes merely to exercise those boundaries.
- Output alias checks cover exact paths and hardlinks in tests; canonical path
  checks also account for symlinks. Scans and staging preserve target/dependency
  bytes. Dependency formats are not embedded into the staged RPK. Existing
  required-pack references, untouched shared-buffer neighbors and non-isolated
  byte-identical round trips are regression-tested.
- Nonfinite donor positions and values that the legacy exporter would sanitize
  are excluded from compatibility candidates. Vertex bounds remain measured
  mesh-local data, not inferred native culling structures.
- Centering is optional and translates only. No guessed rotation, scale,
  normal transform or scene/culling rewrite. Existing explicit translations
  remain supported. Generated preview geometry passes the staging round trip.

Modified code: `tools/modkit/src/cmd_mesh.cpp`, new `mesh_formats.hpp`,
`tools/track_import/track_import.py`, `bridge.example.json`,
`tests/test_mesh_dependencies.py`, `tests/test_mesh_scan.py`.
Documentation: modding README/dcmod/scanner guides plus dependency handoff and
this validation record. Existing assets and runtime hooks are unchanged.

## Status and next gates

| Area | Implemented / tested offline | Remaining runtime gate |
| --- | --- | --- |
| Dependency-aware geometry | Resolver, scanner, normal/UV decode, recognized tangent layout, isolated staging | Real polyRoadShape742 resolved report pending |
| Visual placement | Vertex bounds, explicit translation or donor-center mode, preview GLB | Inherited transform, materials and culling need Windows evidence |
| Rendering | Native RPK visual override prepared | NOT TESTED in DriveClub; no visibility claim |
| Collision | Existing separate authoring/BVH checks and read-only contact probe | BLOCKED: native collider registration/query ABI and lifecycle |
| Spawning | Existing metadata/offline support validation | BLOCKED: native spawn transform/setter and initialization path |
| Playable flat loop / Tsukuba | Original test mesh and private conversion preparation retained | NOT PLAYABLE; visual gate first, then native contact/spawn |

Review of the existing [engine evidence](external-track-engine-evidence.md) and
[contact probe](external-track-runtime-probe.md) still identifies downstream
wheel contacts, not a native road query/registration ABI. Resource-manager
entry/cleanup synchronization prevents safely inventing a registration hook.
No new collision/spawn hook was added. The next native investigation remains
tracing writers of contact points/surface pointers to their owner and allocator,
and separately tracing grid/spawn initialization on the exact game executable.

Execute the [complete Windows commands](external-track-dependencies.md) at
`H:\DC-Track-Test`. Return the resolved scan, both polyRoadShape742 entries,
import report, commit/runtime hashes, baseline/override logs and screenshots.
If formats resolve but remain unsupported, inspect the reported field codes;
if staging works but no geometry is visible, compare local placement and
baseline/override evidence before decoding native scene transforms/culling.
No copyrighted game archive is needed for the report review.
