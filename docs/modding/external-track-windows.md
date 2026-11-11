# Windows 11: build and test the external-road prototype

This is an authoring/visual-injection prototype, **not a driveable new circuit**.
The first checks require no game dump. In-game collision, vehicle placement,
checkpoints and AI are not connected to `track.json`. Nothing here requires
writing to the original PS4 Base or Update folders.

## 1. Get the code and build dcmod

Use the development branch `feat/external-track-foundation`. If supplied as a
Git bundle, from PowerShell in a directory containing the bundle:

```powershell
git clone --branch modkit https://github.com/OverkillLabs3/driveclub-recomp.git driveclub-recomp
cd driveclub-recomp
git fetch ..\external-track-foundation.bundle feat/external-track-foundation:feat/external-track-foundation
git switch feat/external-track-foundation
```

The supplied bundle is incremental: it requires modkit commit
`2a7108906b6d44d3179138da1fe04cc266cc5897`, available in that public clone.
The new branch is committed locally and in the bundle; it has not been pushed
to the upstream repository. Alternatively, check out that exact modkit SHA,
create the development branch and `git am` the supplied patch.

Install Python 3.10+ (the Python launcher `py` is used below), Git and MSYS2.
Open the **MSYS2 CLANG64** shell, install the C/C++ toolchain, then build from
the repository root:

```bash
pacman -S --needed mingw-w64-clang-x86_64-clang
cd /c/path/to/driveclub-recomp
bash tools/modkit/build.sh
bash tools/modkit/test.sh
clang++ -std=c++17 -O1 -Wall -Wextra -o out/track-fixture.exe tools/track_import/tests/make_fixture.cpp tools/modkit/src/rpk.cpp
mkdir -p work
out/track-fixture.exe work/track-fixture.rpk
```

`out/dcmod.exe` is the new build; a released older dcmod lacks
`--isolate-buffers`. Building the full game runtime is not needed for importer
tests. Full runtime/launcher builds have additional dependencies in BUILDING.md.

Back in PowerShell, from the repository root:

```powershell
$env:DCMOD = (Resolve-Path .\out\dcmod.exe).Path
$env:TRACK_FIXTURE = (Resolve-Path .\work\track-fixture.rpk).Path
py -3 -m unittest discover -s tools/track_import/tests -v
if ($LASTEXITCODE -ne 0) { throw 'Track tests failed' }
py -3 tools/track_import/track_import.py check tools/track_import/assets/flat_loop/track.json
```

Expect 7 tests passing, with no skipped integration test when both variables
are set. The integration test uses the real dcmod process, replaces one slot
in an original synthetic RPK, checks vertex/triangle round trips, checks the
other shared-buffer part is byte-identical, and verifies overwrite/failure
handling. It tests incoming vertex counts below, equal to and above the slot's.
The fixture is not intended to load as a game world.

The `check` command reports 8 vertices, 8 triangles, bounds
`[-50,0,-30]` to `[50,0,30]` and spawn surface Y=0. Invalid inputs exit 1 with
`track-import:` on stderr. Generate another original asset in a new folder:

```powershell
py -3 tools/track_import/track_import.py generate -o work/my-flat-loop
```

## 2. Inspect/export with Blender

Install Blender 4.3 or later. Import the committed `road.glb` with File > Import
> glTF 2.0. It is a 100 x 60 metre rectangular road ring with a real central
hole, not Tsukuba. Blender displays Z-up; the file uses Y-up metres.

To run the automatic Blender smoke test, adapt the executable path:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 4.3\blender.exe' --background --python-exit-code 1 --python tools/track_import/tests/blender_smoke.py
```

Expect `Road export validated` and `Blender smoke checks passed`. The `--python-exit-code 1` option makes script exceptions fail the process. This verifies transform baking, Y-up coordinates and
surface orientation. It is not a rendering test inside DriveClub.

For your own `.blend`, select road-only object names and use:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 4.3\blender.exe' --background 'D:\PrivateTrack\track.blend' --python tools/track_import/blender_export_road.py -- --objects ROAD_MAIN ROAD_EXTRA --output 'D:\PrivateTrack\road.glb'
```

The script evaluates and joins the named meshes, bakes world transforms,
triangulates, drops materials/textures and exports one road primitive with
normals/UVs. Existing output is refused. The `.blend` is not saved or changed.
Use an empty new filename on each run. Validate with `track_import.py check`
after creating the adjacent `track.json` described in the conversion guide.

## 3. Optional game-data visual experiment (local only)

Use a separate `work` authoring folder. Keep Base/Update as read-only sources.
Set the folder paths for archive reads (adapt these to your own valid dump):

```powershell
$env:DRIVECLUB_BASE_DIR = 'D:\PS4Dump\Base'
$env:DRIVECLUB_UPDATE_DIR = 'D:\PS4Dump\Update'
New-Item -ItemType Directory -Force work/visual-source | Out-Null
.\out\dcmod.exe archive extract worlds/canada_road_circuit_02_n.rpk -o work/visual-source --verify
.\out\dcmod.exe mesh info work/visual-source/worlds/canada_road_circuit_02_n.rpk
```

Do not use PowerShell `>` to extract binary archives: Windows PowerShell 5 can
transcode native stdout. Use `archive extract -o` as above.

Inspect a few candidate parts with `dcmod mesh export ... -o work/slot.glb
EXACT_NAME` and Blender. Choose a small visible, independently referenced part
with a usable attribute stream and compatible existing material. A world road
part may not exist because road rendering can come from another resource.
Do not pick a random name and assume it represents the road.

Create `work/bridge.json` based on `tools/track_import/bridge.example.json`:

```json
{
  "schema": "dc-visual-bridge-1",
  "template": "visual-source/worlds/canada_road_circuit_02_n.rpk",
  "game_path": "worlds/canada_road_circuit_02_n.rpk",
  "target": "EXACT_FULL_NAME_FROM_DCMOD_MESH_INFO",
  "translation": [0, 0, 0]
}
```

`template` is relative to this bridge JSON. `game_path` is the archive path
being overridden. `translation` is in the target mesh's local coordinates;
its scene transform and bounds are inherited and not decoded here. Use your
inspection/camera reference to position the loop. The example target is a
placeholder, not a claimed known-good slot. The importer requires the actual
`16hex_subindex_partname` name, not a wildcard.

```powershell
py -3 tools/track_import/track_import.py stage tools/track_import/assets/flat_loop/track.json --bridge work/bridge.json --dcmod $env:DCMOD -o work/visual-flat-loop
if ($LASTEXITCODE -ne 0) { throw 'Staging failed; read stderr' }
Get-Content work/visual-flat-loop/import-report.json
```

The command refuses an existing output. New buffers may be impossible if a
slot shares buffer mentions with a previous part; choose another slot rather
than forcing the edit. A successful report establishes a verified RPK geometry
round trip, not visibility or driveability.

In the modkit launcher's Mods page, import the **local** folder
`work/visual-flat-loop`, enable it, and load the donor Fraser Valley track in
Time Trial. Review logs for the RPK override (`Runtime: replaced by mods
folder`). Look for the original rectangular road mesh from multiple angles.
If absent, inspect local placement, inherited scene transform, culling bounds,
material compatibility, patch pack overrides and the chosen slot's use.
Record screenshots and logs; no visibility has been verified by this work.
Disable the mod to restore the stock view.

**Expected physics behavior:** the original track remains the physical world.
The car spawns on the original grid. The new loop can be non-solid; driving
onto it is not a success test for new collision. Do not claim a playable track
on the basis of this experiment. Do not copy generated RPKs to Base/Update or
upload them: they contain game-derived data. The stage report states the
missing integration explicitly.

## 4. Runtime investigation needed for driveability

Use the technical report's acceptance gates. Collect resource inventories and
version-specific collision/spawn/road traces with your own dump before adding
native hooks or a collider compiler. Start with the original loop, one car and
no AI. Test rest/acceleration/braking, an original wall, restart and returning
to a stock circuit before adding checkpoints or Tsukuba scenery.
