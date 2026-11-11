# External track foundation: findings, prototype and blockers

Investigated 2026-11-11. Main: `ca23464e2d9d2cc8bd887a243d5805d5d492baa3`.
Modkit: `2a7108906b6d44d3179138da1fe04cc266cc5897`.
Development branch: `feat/external-track-foundation`, based on modkit to reuse
its authoring infrastructure. The branches differ substantially; this work does
not merge modkit into main or roll back main's runtime changes.

**There is no new playable track yet.** This implements a tested external road
mesh authoring/validation path and a visual RPK slot replacement bridge. It does
not rename a stock track to Tsukuba or present a cloned route as a new circuit.
No game dump, KN5, third-party track or game-derived pack was available or used.
The implementation was built and exercised on original synthetic packs. Rendering
inside DriveClub and Windows runtime behavior remain unverified.

## Repository investigation

| Area | Evidence and consequence |
|---|---|
| Main | `docs/MODDING.md` and `src/runtime_file.c`: loose files override game paths before archive access. Main does not have `tools/modkit` or `docs/modding`. Overrides do not construct a world or change the physics of a visual model. |
| Modkit CLI | `tools/modkit/src/main.cpp`, `cmd_track.cpp`, `cmd_mesh.cpp`, `rpk.cpp`: archive reads, RPK reconstruction, table/manifest/script patches and glTF round trips. There is no external world/road/collision compiler. |
| Track registration | `cmd_track.cpp::CloneTrack`, `docs/modding/tracks.md`: a row in `newdata/tracks.csv`, world, patch and level-data packs, required country packs, resource names, CRC32 scene keys, layer names, UI/localization and Lua lookups all participate. Clone requires same-length circuit-number renaming and preserves donor geometry, route and physics. It is not new track creation. |
| Model loading/import | `cmd_mesh.cpp::FindSubs`, `ImportSub`, `RunMesh`: meshes reference position/attribute/index streams and existing materials. GLB import matches `ID_N_NAME` prefixes, reads the first primitive, and does not apply node transforms. It replaces known slots, not arbitrary scene nodes. Indices are 16-bit; a stream is limited to 65535 vertices. `--own-buffers` previously relocated only growing parts. |
| Blender integration | `tools/blender/driveclub_modkit.py`: shells out to `dcmod mesh`, keeps slot names, stores the donor pack in the scene. Its export disables normals/UVs; it is an editor of donor geometry, not a complete external-track converter. The new dedicated exporter supplies explicit normals and UVs and bakes world transforms. |
| World/scene | `rpk.cpp` names world, scene, scene_object, hierarchy, octree and road_system resource types, but does not decode their scene placement/culling semantics. `cmd_track.cpp` rewrites existing names/hashes in opaque payloads rather than synthesizing scene graphs. `runtime_scene_debug.inc` hooks time-of-day, not an external scene loader. |
| Road rendering | `docs/modding/models.md` reports a world-pack geometry lift moved scenery while the road remained in place. Therefore replacing world meshes alone cannot establish where all road geometry is generated. `road_system`, `road_data`, `road_network`, DEM resources are leads to investigate, not decoded formats. |
| Collision | `rpk.cpp` lists `havok`, `rigid_body_bindings`, `solids`, road and region resource types. Listing a type does not implement its physics schema. No mesh-to-game collider writer or supported native collider registration interface was found. `src/runtime_pad.c::surface_feel` reads tyre contact surfaces for force feedback; it does not install colliders. Which resources own road contact vs scenery collision still needs real-data tracing. |
| Spawn, limits, timing, AI | Spawn-point, reset/no-reset, road-region, start-finish, split and AI-speed resource types exist but have no semantic writers in the modkit. Stock level data remains authoritative. No supported vehicle teleport/spawn setter or complete route/AI authoring interface was found. |
| Race initialization | `src/runtime_benchmark.c::script_template` uses game Lua `Automation.Go(mode, track, sky, ...)`, waits for prerace and Racing events, and chooses an existing table track name. It does not accept a GLB, collider or external grid. |
| Native extension | `src/dc_plugin.h`, `docs/modding/plugins.md`: Windows-only opt-in memory read/write and code hooks, no public scene/collision registration API. An extension is possible after identifying version-specific guest functions, signatures and lifecycle; random address writes would not be a defensible prototype. |

Relevant docs inspected include `docs/modding/{README,tracks,models,dcmod,formats,
mod-format,plugins,lua-api,lua-names}.md`, the Blender add-on and CLI implementations.
Upstream documentation's game checks are **upstream reports**, not checks rerun here.

## What prevents a completely new driveable circuit

There is no demonstrated GLB-to-world-and-level-data compiler and no runtime
bridge that registers both visual meshes and physical road geometry. Existing
import code depends on a donor slot's draw records, vertex formats, materials
and dependencies. Moving its positions does not update scene bounds/placement,
road-system geometry, physics, grid points, reset volumes, progress or AI.

These are concrete missing interfaces in this repository. The precise binary
schemas and the exact functions needed inside the proprietary game cannot be
established from source alone without the player's dump and runtime traces.
In particular, it would be inaccurate to claim Havok alone accounts for all
road physics, or that implementing one resource type guarantees driveability.

## Implemented prototype

`tools/track_import/track_import.py` is a Python 3.10+ standard-library tool:

- `generate`: reproducible original flat loop GLB and minimal `track.json`.
- `check`: strict GLB 2 parsing, one scene/object/primitive, embedded buffer,
  baked transforms, explicit finite normals, finite positions, view/accessor
  bounds, index range, nondegenerate triangles, 65535-vertex limit, coordinate
  envelope, metre/Y-up metadata and package-relative mesh paths.
- Offline upward-triangle XZ support query validates a proposed spawn's surface
  and 0.1–3 m clearance. This is a linear authoring check, not tyre collision,
  wall collision, a collision solver, or an injected vehicle spawn. Highest
  surface is chosen; bridges/tunnels require explicit layer selection later.
- `stage`: selects one exact donor mesh slot via a separate bridge config,
  bakes a translation, creates a canonical GLB named for that slot, calls the
  real modkit importer and re-exports to verify positions/indices. Writes a new
  loose-file mod plus SHA256/bounds/status report. It never edits the template
  or archive and refuses an existing output folder. Logs dcmod operations to
  stderr, report JSON to stdout; failures exit 1.
- `dcmod mesh import --isolate-buffers`: new opt-in allocation path also for
  equal/smaller parts. It preserves shared source vertices and existing default
  import behavior. It refuses unisolatable/shared buffer mentions and parts
  without usable attribute streams. New parts still use the donor material.
- `blender_export_road.py`: evaluates named road mesh objects from a local
  `.blend`, bakes world transforms/modifiers, joins, triangulates, removes
  materials and exports plain GLB with explicit normals. KN5 decoding is not
  implemented; the user must supply a compatible importer.

The GLB contract intentionally rejects general glTF features rather than
silently losing transforms, extra primitives, instances, skins or extensions.
JSON `.gltf` with external buffers is not accepted; export binary `.glb`.
Staged RPKs include donor data and must remain local. No game assets belong in
source control or public mod downloads. New geometry can be generated from the
source script; the committed test asset contains only original data.

## Best next architecture

Use the external package as an authoring boundary, with two independent output
backends: a native world/level-data compiler, if the formats can be decoded, or
an opt-in native runtime adapter if safe registration functions can be identified.
Prefer the native resource compiler because it reuses the existing renderer,
streaming and game physics. The visual slot bridge is an investigation aid,
not the long-term representation of Tsukuba. A graphics overlay alone cannot
meet the driveable-track goal.

Keep coordinates in metres, RH Y-up, near a local origin. Preserve explicit
track-to-world transforms and tile-local origins instead of baking kilometre
coordinates into half-float position streams. Road surface accuracy is a
physics requirement, not just a visible-mesh issue. Avoid blindly mapping
Blender/Assetto Corsa headings to guest vehicle orientation.

| System | Required next implementation and acceptance check |
|---|---|
| Render/scene | Decode scene objects, material compatibility, draw bounds, hierarchy/streaming and road generation. Draw an original road in a minimal scene with verified placement, normals and culling at all camera angles. |
| Collision | Identify wheel raycast/contact and world-body collision owners. Compile/register triangle road, surface friction IDs, walls and acceleration structure. Car must rest, accelerate, brake, steer and hit an original barrier. A visual road over old collision fails this gate. |
| Spawn/lifecycle | Decode or hook grid/spawn resources and reset behavior at the correct race lifecycle boundary; prove car pose and wheel clearance, restart and unload/reload. No per-frame position forcing. |
| Boundaries | Author drivable/off-track regions, reset/no-reset volumes, recovery spawns and walls. Off-road exits and respawns must remain within the new circuit. |
| Timing/progress | New start/finish and ordered checkpoint/split volumes, direction validation, lap distance and shortcut prevention. Original loop must count laps in both valid/invalid-crossing tests. |
| AI | A new DriveClub route representation with racing line, width, speed/braking hints and recovery paths. Validate one opponent before a grid. AC `fast_lane.ai` is a potential reference for a future converter, never a drop-in binary. |
| Registration | Add a distinct track ID with new scene and level-data dependencies; keep donor stock circuits unchanged. Localization/UI can come after a freeroam/time-trial gate. |

## Tsukuba plan and acceptance gates

1. On Windows with the user's own dump, inspect one small stock circuit and the
   three packs plus country-common resources. Inventory resource IDs/dependencies,
   compare road geometry with render captures and tyre contact positions. Use
   the documented lookup/assertion hooks to identify owners, not to fake a race.
2. Prove the flat loop visually loads near a known camera. Then reverse-engineer
   or safely register road contact and one spawn. Do not spend effort on full
   Tsukuba materials while the original loop cannot be driven.
3. Make a genuinely new loop scene/route. Isolate its collision from the donor,
   implement reset, timing and lap progress, then AI. Record loading, driving,
   restarting and returning to another track as regression cases.
4. Convert a privately held, permitted Tsukuba KN5 through Blender. Verify TC2000
   layout and scale using the model's known dimensions; survey centreline,
   elevations, barriers, kerbs, pit entry and grid. See the conversion guide.
5. Add tiled rendering, dedicated physics mesh, surface groups and streaming
   metadata after the runtime/resource backend supports them. Current one-slot
   imports are capped at 65535 vertices and cannot represent a whole detailed
   circuit. Rebuild checkpoints and DriveClub AI rather than copying AC physics.
6. Distribute converter code and original configuration only. Asset redistribution
   requires explicit applicable permissions and is outside this deliverable.

## Verification and limitations

Commands and outcomes are recorded in `external-track-validation.md`.
Automated checks exercise actual dcmod import/export with synthetic RPKs;
there are no mocked successful engine hooks. The Blender smoke test exercises
transform baking and Y-up surface orientation. The normal modkit self-test and
ATRAC9 tests run without game data. Full runtime/game tests, game-data round trips,
Windows execution and KN5 conversion require assets/platform not supplied here.
