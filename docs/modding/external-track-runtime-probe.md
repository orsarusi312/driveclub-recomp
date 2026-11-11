# Native contact evidence probe

`BB_TRACK_PROBE=1` enables **read-only observation**, compiled only into a
`BB_DIAGNOSTICS=ON` Windows build. It does not register collision, set the car's
pose or inject contacts. It reuses `ffb_inputs_hook` in `runtime_pad.c`, reached
through the existing signature-checked guest `+0xe0f5d0` hook, with caller
`+0xd93c3e`. No additional hook is installed. The hook is installed from the
existing `logi_init` path (`runtime_logiwheel.inc`); absence of that initialization
or the matching signature means there will be no samples. A wheel isn't required
by the probe code itself, but that game initialization path must be exercised.

The helper `runtime_track_probe.h` uses only offsets already read/documented by
the runtime: world wheel/contact positions, wheel loads, up to three contact
surface pointers/weights per wheel, surface kind, two observed grip fields and
bump height/wavelength. It uses checked memory reads, rejects overflow and
nonfinite values, does not cache guest pointers beyond diagnostic identity, and
skips concurrent arrivals. Samples are limited to two per second for a stable
car and 600 per process (identity changes may cause an immediate sample).
A release/identity change starts a new epoch; the total budget is never reset.
Production builds compile the call to a no-op.

`center` is the average of four wheel/contact positions and `heading_rad` is
computed from axle direction. Neither is a verified rigid-body spawn transform.
Contact normals are **unavailable** in this probe. Zero values with invalid
flags are not observed zero contacts. Raw `grip_28`/`grip_30` are observed
native fields, not portable friction coefficients. Pointer reuse within an
undetected lifecycle transition remains possible; `epoch` is diagnostic identity,
not a game resource generation counter. Printing can affect frame pacing, so
use diagnostic runs for evidence and disable the probe for performance tests.

Portable test:

```bash
gcc -std=c11 -Wall -Wextra -Werror tools/track_probe_test.c -lm -o out/track-probe-test
out/track-probe-test
```

These tests exercise the helper against synthetic memory, including bounded
reads, no source writes, overflow/NaN handling, sample limits and restart identity.
They do not execute the Windows hook. In this work the helper additionally
passed GCC AddressSanitizer/UBSan. A Linux syntax check of `runtime_pad.c` passed,
but its Windows-only integration was not compiled or run here.

## Windows build and isolated runtime experiment

Use Windows 11, the MSYS2 CLANG64 dependencies in BUILDING.md and this branch:

```bash
bash tools/modkit/build.sh
BB_DIAGNOSTICS=ON bash build.sh
```

The executable is **`out/DriveClubGame.diagnostics.exe`**, not the release
`DriveClubGame.exe`. Copy it under that distinct name alongside the runtime DLLs
in a separate copy of your installation. Do not overwrite existing executables.
The Linux environment has no MinGW cross-compiler; no Windows executable is
included in the handoff. The following procedure has not run on Windows here.

Use a fresh run directory, private saves and GPU cache. Adapt all paths below.
`Prep` points to already prepared files from your legal installation; reading
those files does not prepare or modify the dump. The example launches the
stock donor track to collect baseline evidence, then can show a visual override.
It does not launch a new native track ID.

```powershell
$Runtime = 'D:\DC-TestInstall\DriveClubGame.diagnostics.exe'
$Prep = 'D:\DriveClub\data\prep'
$Base = 'D:\PS4Dump\Base'
$Update = 'D:\PS4Dump\Update'
$Run = 'D:\DC-Experiments\baseline-001' # must be NEW, outside dump/save/install folders
if (Test-Path $Run) { throw 'Choose a new run directory' }
foreach ($file in @($Runtime, "$Prep\boot-linked.bin", "$Prep\content.bin", "$Prep\patches.bin")) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing $file" }
}
New-Item -ItemType Directory "$Run\user", "$Run\mods", "$Run\logs" | Out-Null
# To test the staged visual mesh, copy ONLY its files/* into this NEW mods directory.
# Copy-Item -Recurse 'D:\repo\work\visual-flat-loop\files\*' "$Run\mods\"
$Names = @('BB_TRACK_PROBE','BB_BENCHMARK','BB_GPU_USER_DIR','BB_CRASH_DIR','DRIVECLUB_DATA_DIR')
$Saved = @{}
foreach ($name in $Names) { $Saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
Push-Location $Run
try {
    $env:BB_TRACK_PROBE = '1'
    $env:BB_BENCHMARK = 'track=canada_road_circuit_02_n,mode=TimeTrial,drive=none,seconds=90'
    $env:BB_GPU_USER_DIR = "$Run\user"
    $env:BB_CRASH_DIR = "$Run\logs"
    $env:DRIVECLUB_DATA_DIR = $Run
    & $Runtime "$Prep\boot-linked.bin" --content-profile "$Prep\content.bin" --patches "$Prep\patches.bin" --app0 $Update --app0-base $Base --user "$Run\user" --mods "$Run\mods" --stub-unresolved --timeout 240 2>&1 | Tee-Object "$Run\runtime.log"
    if ($LASTEXITCODE -ne 0) { Write-Warning "Runtime exit $LASTEXITCODE; retain log" }
} finally {
    Pop-Location
    foreach ($name in $Names) { [Environment]::SetEnvironmentVariable($name, $Saved[$name], 'Process') }
}
Select-String -Path "$Run\runtime.log" -Pattern 'TrackProbe:|Benchmark:|Fault:|FFB physics:|replaced by mods'
```

The runtime may need the same DLLs/prepared content as the standard launcher;
a failed baseline is an environment/setup failure, not proof about custom road
support. See BUILDING.md and the normal runtime diagnostics guide. Start with
no mod and confirm stock loading, then repeat in another new directory with the
visual override. The private profile may require initial setup and may lack
unlocks. This procedure never copies old saves or writes the original profile.
Removing the new experiment directories reverses the test.

Collect the exact branch commit, diagnostic executable SHA256, game/version,
car/track/settings, exit code, `runtime.log`, `import-report.json`, resource
inventory JSON, and screenshots showing the original loop from two viewpoints
with a stock car/known landmark. Record a stationary sample and a slope/kerb
transition if available. Share only reports and relevant log excerpts, not RPKs,
game executables, raw memory or licensed models. For absence of samples, report
whether `logiWheelInit`, `FFB physics: reading` and `TrackProbe: enabled` appeared.

The decisive next reverse-engineering step is to trace the **writers** of the
observed contact points and surface pointers upstream to the native query and
static-world owner, using the exact local executable in a disassembler. Identify
creation/destruction, allocator/thread requirements and spawn/grid initialization.
Only then can we implement a native collider/spawn adapter without guessing an ABI.
