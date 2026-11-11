# Engine evidence and the next native integration gate

This is source-derived evidence at the modkit base `2a710890` and foundation
`ca8f4cd`, with a new metadata collection tool. Guest offsets below are the
repository's documented **eboot 1.28** offsets, not newly discovered or runtime
validated addresses. Add the runtime image base (normally `0x800000000`) only
where the existing code does. They are not stable public interfaces. No game
executable, resource dump, native runtime or Windows test was supplied for this
investigation. No new contact-query ABI, collider schema or spawn setter is
claimed.

## Relevant call paths and structures

| Concern | Source reference and concrete evidence | Implication |
|---|---|---|
| Track selection | `tools/modkit/src/cmd_track.cpp:100`, `CloneTrack`, consumes `newdata/tracks.csv`; matches `Track Name` and `RPKName`, creates a new unique ID and copies world/patch/level-data dependencies. At `:181` it searches scene-object names and `:227` patches world CRC32 keys and layer names. | A table row alone does not load a new scene. Clone retains the donor route and collision. |
| Race startup | `src/runtime_benchmark.c:175` `script_template`; `:208`/`:211` call `Automation.Go` with track, sky, laps, time, weather, car and opponent configuration. `:216` waits for prerace; `:231` waits for `RaceState == Racing`. | Provides an existing developer launch route for a stock or fully registered custom track, not a GLB loader. |
| Loose files | `src/runtime_file.c:203` `runtime_file_add_mods`; `:285` replaces `/app0/` lookups with files under mod roots. | Reversible staged RPKs can enter the game's normal resource path without archive writes. |
| Visual geometry | `tools/modkit/src/cmd_mesh.cpp:130` `FindSubs`; `ImportSub` writes known mesh draw records and vertex/index streams; `MakeGlb` produces one object per submesh. Position formats are float or half-float; index buffers are 16-bit. | Independent scene nodes, transforms, draw bounds, road tessellation and materials are outside this writer. `--isolate-buffers` protects shared geometry, not scene ownership. |
| Scene and level types | `tools/modkit/src/rpk.cpp:12` `kTypeNames`: world=13, scene=15, scene_object=20, solids=24, road_system=27, octree=33, level_data=43, spawn_point=47, road_data=52, road_network=55, dem=57, dem_tile=58. | These are container type names. The code does not establish that any one type is the road collider or decode the listed payloads. |
| Contact positions | `src/runtime_pad.c:116` `runtime_mod_car_state`: reads 4 XYZ contact points from car state `+0x180 + 0x40*i`; reads load/lat/lon arrays at `+0xa50/+0xa60/+0xa70`, slip ratio/angle at `+0xb30/+0xb50`. | Existing observables can compare rendered road with actual tyre support. Averaging contact points gives displayed position, not a rigid-body spawn transform. |
| Orientation | Same function `:137` derives yaw as `atan2(front_axle_x - rear_axle_x, front_axle_z - rear_axle_z)`. `tools/modkit/src/scripts.cpp:50` exposes position and heading to scripts. | Runtime telemetry is metres with Y-up and yaw about Y. This does not prove the stored spawn quaternion convention, origin or body-to-wheel offset. |
| Locating the player car | `src/runtime_telemetry.inc:325` follows the manager at `image+0x1911100`, then `+0x970`, `+0xe0`; slot vtable `+0x48`, controller `+0x80`, driver helper `image+0xd60700`, checks controller vtable `+0xa8 == 3` and obtains simulation interface through driver vtable `+0x350`. Interface `+8` is state, `+0x20` setup; setup `+8` is drivetrain/tyres. | Existing native read path, with no discovered creator/reset/spawn ABI. Reuse telemetry at valid lifecycle boundaries rather than treating a cached pointer as permanent. |
| Physics-thread observation | `src/runtime_pad.c:804` describes guest `+0xd93790` feeding FFB setter `+0xe0f5d0`. `ffb_inputs_hook` at `:1021` accepts caller return `image+0xd93c3e`, obtains interface from caller frame `rbp-0x78`, and clears tracked state when `in[0]==0`. Installation at `:1053` checks instruction bytes before patching. | FFB data is downstream observation of the native simulation. Hooking this setter does not insert road contact into physics. The existing hook is Windows-only. |
| Surface/material output | `src/runtime_pad.c:949` `surface_watch`: each wheel at `vehicle + 0x5d0*i` has up to 3 surface pointers at `+0x2e0/+0x2e8/+0x2f0`, weights `+0x2f8/+0x2fc/+0x300`. Comments identify grip values at surface `+0x28/+0x30`. `surface_feel` at `:980` reads name pointer `+8`, kind `+0x40`, bump height/wavelength `+0x4c/+0x50`. | Candidate output to trace backward to collision and material lookup. These fields do not describe an API for adding a triangle or changing the wheel raycast. |
| Tyre force calculation | `src/runtime_pad.c:872` documents tyre update `+0x7c5a40`, load/surface-factor multiply at `+0x7c6b6f`, with wheel in RBX. `tyre_grip_install` checks `c5 72 59 43 68` before injecting an optional slip-factor change. | Surface grip contributes to native tyre force. Changing this multiplier cannot prevent falling through a missing road. Keep this optional existing behavior out of contact-registration experiments. |
| Spawn and boundaries | `rpk.cpp` names `do_reset_volume`=45, `no_reset_volume`=46, `spawn_point`=47, `road_region_volume`=48, `start_finish_volume`=49, `split_volume`=56. `docs/modding/lua-api.md:18` lists `Actor.SpawnActor`; `lua-names.md:1168` lists `SetupGrid`. | Names alone are not calling conventions. Decode the level-data dependencies and script call sites before invoking a spawn/reset function with invented arguments. |
| Lap timing and AI | `docs/modding/lua-api.md` lists OSD timing/progression callbacks and the RPK types include road network, scripted road line, AI max speed volume. | UI callbacks observe race state. They are not a proven timing/AI route compiler; no native writer was found. |
| Camera/replay | `src/probe.c:1675` installs `replay_seek_install`; camera patches are installed at `:1677`. `src/runtime_modapi.c:260` reads camera/sun data via `bbgpu_camera`; `CameraSequence` methods are listed in the Lua API. | Reuse stock camera behavior for the first drive test. Custom replay routes and track camera resources remain separate work. |
| Weather/time | `src/runtime_scene_debug.inc:8` wraps time update at guest `+0x75a00` (signature checked at `:44`), hour at object `+0xc`, compression at `+0x10`. `src/runtime_benchmark.c:204` sets precipitation before/after `Automation.Go` and sends precipitation events. | Environment can initially remain inherited while road loading is validated; no custom sky renderer is needed. |

## Resource ownership is already a native synchronization problem

`src/runtime_resource_entry.inc:1` describes the native manager at image
`+0x274b110`; entry function `+0x77e7b0`. State is at manager `+0x100`, active
entry count `+0x104`, owner thread `+0x108`, cleanup thread `+0x110`, entry mutex
pointer `+0x200` and cleanup mutex pointer `+0x208`. Cleanup begins at documented
sites `+0x77b951`, `+0x77b5d6`, `+0x77cf6a`, `+0x77d258`. The current patch
prevents a race between entering and cleanup. Reentrant destruction during
cleanup caused a deadlock in upstream code.

This rules out registering/unregistering external native resources from an
arbitrary worker or plugin callback without the correct manager protocol.
The plugin API (`src/dc_plugin.h:27`) contains log/read/write/hook only. It has
no resource ownership, unload, allocator or collision registration abstraction.
The plugin documentation explicitly says callbacks run on the game thread and
must not call into the game. A future adapter must identify safe entry points,
allocator ownership, thread affinity and the complete unload sequence first.

## Renderer and performance boundaries

The repository's Vulkan backend consumes the game's GPU commands. Useful
inspection points are:

- `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:1296`,
  `Rasterizer::Draw`; vertex binding `:2571`, index binding `:2745`, resources
  `:2434`. Frame-capture draw diagnostics appear at `:1636`.
- `gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp:1296`, `ObtainBuffer`;
  `:1626` `SynchronizeMemory`; `:1648` `UploadCopies`.
- `gpu/shadps4/video_core/texture_cache/texture_cache.cpp:742`, `FindImage`;
  `:1020` `RefreshImage`; `:1533` `DeleteImage`.
- `gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_cache.cpp:440`,
  `GetGraphicsPipeline`; `:834` `CompileModule`; `:1069` shader patches.
- `gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.cpp:371`, `Flush`;
  `:382` `Finish`; `:397` `Wait`; `:487` `SubmitExecution`.

An extra Vulkan draw at these sites would not create scene culling, a physics
surface, a native material or a vehicle. Prefer completing native resource
loading so these existing caches/synchronization paths continue to work.
Measure first load time, subsequent loads, CPU/GPU memory and frame-time
percentiles on the user's PC before adding streaming. A 20.8 km route should
use track-local coordinates and independently owned spatial chunks, but road
length by itself is not evidence that a new streaming subsystem is required.
Half-float visual precision and 65535-vertex stream limits must be addressed
per tile independently of the native physics mesh format.

## Metadata-only evidence collection

`tools/track_import/inspect_resources.py` calls the real `dcmod rpk extract`
in a private temporary directory, reads decoded manifest metadata and payload
headers, then deletes all temporary payloads. It emits only a new JSON report:
source SHA256, basename, size, TOC/version/root, required pack names, per-type
counts/bytes, resource IDs and dependency links, raw-header validity and
payload versions. It intentionally excludes raw payloads, names and original
source paths. Binary/XML wrapper evidence comes from dcmod's decoded manifest,
which is marked separately from a directly read raw header. Nothing is guessed
for a missing/truncated header. The input is hashed before and after inspection.

An unresolved ID means only "not supplied in this run". Dependencies may be in
country/common or other packs. Duplicate providers are reported because a
patch pack can override a resource; the tool does not guess load precedence.
This tool does not classify a mesh as road merely because its name resembles
road, and it does not claim a schema from a resource's type name.

From PowerShell, with a built `dcmod.exe` and your own extracted packs:

```powershell
py -3 tools/track_import/inspect_resources.py `
  work/visual-source/worlds/canada_road_circuit_02_n.rpk `
  work/visual-source/worlds/canada_road_circuit_02_n_patch.rpk `
  work/visual-source/data/leveldata/canada_road_circuit_02_n.rpk `
  --dcmod out/dcmod.exe -o work/evidence/stock-track.json
```

The arguments are examples of the three documented track paths. Add the actual
required country/common packs shown in the report in a second run. Inputs
must be local RPK files; output must not already exist. Use `dcmod archive
extract PATTERN -o work/visual-source --verify` for read-only extraction of
files from your legal dump. Do not redirect binary stdout with Windows
PowerShell 5. All extracted packs stay private. Only the report, screenshots
and selected diagnostic logs need to be shared for investigation.

## Narrow next experiment: obtain the missing collider owner

1. Record game version, executable hash, graphics backend/settings, car, track,
   weather and exact source pack hashes. Start a stock time trial without any
   visual override. Record stationary tyre loads and contact positions, then
   a short flat, slope and grass/kerb transition. `dcmod.car()` currently exposes
   average position and per-wheel load/slip; native contact points require a
   scoped diagnostic read if they are not already in the selected log.
2. Build with `BB_DIAGNOSTICS=ON` for existing diagnostic gates. Where available,
   use the existing `BB_FFB_TRACE` surface logs to correlate surface kinds and
   friction with contact. This FFB path may require a supported active wheel;
   absence of output is not absence of physics. Avoid shipping raw memory dumps.
3. Inventory world, patch, level-data and common packs with the collector. Map
   dependency neighborhoods of road_system, road_data, road_network, havok,
   solids, spawn_point, boundaries and DEM types. A source type listing is not
   enough to select the physics owner.
4. Using the user's exact executable locally in a disassembler, trace **writers**
   of wheel contact points and surface pointers upstream from the known tyre
   update. Identify the contact query call, its complete inputs/outputs,
   broadphase/static-world owner, surface selection, body collision path and
   lifetime. Capture a small trace with offsets/callers, not redistributed code.
   Check whether road contact uses the same owner as Havok scenery collision.
5. Correlate that owner with resource creation/destruction using the stock
   resource lookup. Existing docs identify lookup guest offset `+0x780f30`
   (ID at `[rsi]`), failed return `+0x78104b`, world-name map `+0x6954ec`, and
   assertion `+0x662260`. These are upstream leads requiring signature and
   version checks; no new hook is installed by the collector.
6. Only after the schema or registration ABI is proven, compile/register one
   original plane and one original barrier. Disable donor collision in that
   small test region so the car cannot accidentally be supported by stock
   geometry. Car must rest, cross a slope/bank, accelerate, brake, steer and hit
   the barrier using native suspension/tyres. Then verify reset, race restart,
   unload/reload, and returning to a stock circuit.
7. Trace native grid initialization to the identified spawn_point payload or
   actor setup arguments, including orientation, body origin and suspension
   height. Implement one grid/respawn only after this evidence; never enforce a
   pose every frame to imitate support.

The first required user evidence is a successful stock baseline and the three
pack inventory report, followed by a screenshot/log showing the staged original
mesh actually appears. Native query/registration and spawn ABI evidence is the
critical additional work needed to claim the flat loop is driveable. A passed
offline triangle query or RPK round trip is not that evidence.

## Tests executed for the evidence tool

Five Python tests pass, including an actual dcmod synthetic-RPK invocation:
raw version/ID inspection; omitted private data; invalid header reporting;
path/missing-file/duplicate-ID failures; unresolved and resolved cross-pack
dependencies; source-byte preservation and output overwrite refusal. The real
integration case uses the original generated fixture and does not need game
assets. Run with the same `DCMOD` and `TRACK_FIXTURE` environment variables as
the foundation integration tests:

```powershell
py -3 -m unittest discover -s tools/track_import/tests -p test_inspect_resources.py -v
```
