# Private Assetto Corsa KN5 to external-road preparation

This pipeline prepares original or permitted user-held assets for the prototype.
It does not implement KN5 decoding or make AC physics/AI compatible with
DriveClub. No KN5/track mod was supplied or converted in this implementation.

1. Keep the Tsukuba mod and working files outside the repository and public
   downloads. Check the applicable track licence before converting or sharing.
   Identify the **TC2000** layout in its layout/model configuration, including
   referenced KN5s; do not accidentally select TC1000 or another variant.
2. Use a KN5-capable Blender importer that supports your Blender version and
   that you have permission to use, or an authorized local intermediate export.
   This project does not supply or endorse a specific decoder. Import into a
   private `.blend`; confirm all model placements from the AC layout config,
   not only the first KN5's vertices. Keep a record of tool versions, source
   hashes and model transforms. Textures stay local.
3. Establish scale from known dimensions in the model. The importer expects
   metres, RH Y-up GLB. Blender itself uses Z-up. The provided Blender exporter
   handles Blender-to-glTF axis conversion; do not add a second arbitrary
   90-degree rotation. Place a local track origin near the start/finish area
   and record that transform. Check handedness with a labelled asymmetric
   landmark, travel direction and triangle normals. Do not infer scale merely
   from unit display settings.
4. Start with a small road section or a simplified complete road-only mesh
   below 65535 vertices **after** export (UV/normal seams can duplicate them).
   Keep scenery, kerbs, walls and collision layers separate in the master
   `.blend`. Current prototype supports one joined road primitive and retains
   a donor material; it is not a PBR/texturing converter. The Blender export
   intentionally removes AC material assignments.
5. Export using `tools/track_import/blender_export_road.py` with exact road
   object names. It evaluates modifiers, bakes world transforms, compensates
   reflected transforms, joins and triangulates the meshes, and exports normals
   and UVs. The strict parser rejects remaining transforms, multiple primitives,
   extensions, skins and malformed buffers. Reduce/split assets if necessary;
   automatic multi-slot tiling is not implemented.
6. Beside the GLB, create the following `track.json`, replacing the spawn with a
   surveyed point on the actual road. The clearance is a provisional origin
   offset, not a suspension/collision calibration. Heading zero is authoring
   +Z; positive headings turn toward +X. Engine orientation mapping is pending.

```json
{
  "schema": "dc-external-track-1",
  "id": "tsukuba_tc2000_private",
  "units": "metres",
  "axes": "RH_Y_UP",
  "mesh": "road.glb",
  "spawn": {"position": [0, 0.75, 0], "heading_degrees": 0}
}
```

```powershell
py -3 tools/track_import/track_import.py check 'D:\PrivateTrack\track.json'
```

A successful check proves supported geometry and an upward-facing triangle
below the proposed spawn. It does not prove correct TC2000 dimensions,
connected road topology, car clearance, game collision or lap timing. The
highest-surface offline probe is unsuitable for stacked-road layer selection.
If you stage a small section into a local RPK slot, follow the Windows guide
and keep the generated pack private.

## Data to author after the flat-loop driveability gate

Create a dedicated road physics mesh, kerb/wall collision meshes, surface
classes, track bounds and reset regions. Preserve elevations and cambers;
decimation suitable for scenery can damage tyre contact. Add a start/finish
plane, ordered checkpoints, grid and recovery spawns with measured directions.
Build a DriveClub centreline/progress graph and AI route once their schemas or
runtime interfaces are established.

AC `surfaces.ini`, physics configuration, grid/pit metadata and `fast_lane.ai`
are evidence/reference inputs for future conversion tooling only. They are
not copied into DriveClub resources by this pipeline. Surface IDs/friction,
checkpoint semantics, opponent control and road progress must be mapped and
tested explicitly. A full Tsukuba import is gated on scene/material creation,
streaming/tiling and new collision/route support, not on a successful KN5 export.
