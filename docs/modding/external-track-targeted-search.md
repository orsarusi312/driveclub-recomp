# Target Road742 after the broad search exhausted its budget

## Findings from both Windows logs at 04fe187

The branch was clean at `04fe187ba8b2121936b4684984a64d771279078e`; no unfinished
changes needed recovery. Both uploaded logs were examined. `stderr.log` has the
probe, search and resource trace diagnostics; `stdout.log` has 40 loose-file
override messages and ends with `Runtime: the process ended normally`. Those 40
messages are not evidence of 40 distinct resource allocations.

The full stderr log continues past the user's 925-binding snapshot:

| Search observation | Reported intermediate snapshot | Last logged sample |
|---|---:|---:|
| Descriptor layouts admitted to cache | 925 | 1,024 |
| Scanned bytes | 67,108,768 | 67,108,768 |
| Truncated scans | 523 | 621 |
| Backing read failures | 0 | 0 |
| Prefix copies / referenced prefixes | 0 / 0 | 0 / 0 |
| Cache-full skips | 0 | 357,558 |

There were only **96 bytes left** of the 64 MiB scan budget. The old implementation
could admit additional entries without enough budget for its next chunk and cache
them as truncated. Neither the zero matches nor the number of cached bindings
means that all those buffers were fully inspected. The cache then filled too.

The actual recorded positive control passed at serials **3519794 / 3520699**.
This control used **18 indices, format 97, stride 12**, not the Road742 float/16
layout. There is no reported guest memory fault in either log. Other GPU feature
warnings, network stubs, a missing fresh-user profile and thread-join warnings do
not establish the reason for the missing road.

Three resource read events were reported, all with verified headers:

| Resource | File offset | Guest destination | Bytes in this observed interval | Interval end |
|---|---:|---|---:|---|
| index `0004f1b2dfbf0361` | 80,394,197 | `0xf0026d9e58` | 26,840 | `0xf0026e0730` |
| vertex `0003a8908986f62b` | 113,879,809 | `0xf0026d1ca0` | 60,048 | `0xf0026e0730` |
| same vertex ID | 113,879,809 | `0xf0026d1063` | 63,181 | `0xf0026e0730` |

These overlapping intervals have **the same end address**. They are observations
of file-read destinations, not three persistent resource allocations. Reused I/O
storage is consistent with this pattern; its lifetime and subsequent copies are
not observed. They are partial read intervals, not necessarily resource sizes.

With header size 28, first vertex 1456 and stride 16 from the actual signature,
the Road742 position prefix would start at relative byte **23,324**. Its expected
addresses in those two reads are `0xf0026d77bc` and `0xf0026d6b7f`, respectively.
Both sampled windows fit inside their logged intervals. The old trace verified
only the header, not those position bytes; the new trace closes that gap.

Logged GPU examples instead use index `0x509a9723ce` and vertex `0x509d83c880`.
The control uses vertex `0x508d6e8be0` and index `0x508d6f1274`. None overlaps the
three observed read intervals. These examples are not identified Road742 draws,
and most draw addresses were not logged. Different addresses therefore do not
prove a copy, transform, repack or lack of use. No mesh-resource read event was
logged for the watched Road742 mesh ID; this also does not establish non-selection
because the trace does not cover every native access path or retained allocation.

## Implemented changes

### Independent cheap checks and donor-shaped full scans

`dcmod mesh scan` now emits `position_resource_vertices` and `mesh_search_target`.
The preparation tool writes the latter to `mesh-search-target.txt`. The strict
configuration is `t1:<position resource ID>:<shared vertex count>:<slot vertex count>`.
For the supplied Road742 layout this is expected to be:

```
t1:0003a8908986f62b:35189:47
```

The actual source scan generates the value; no guest address is hard-coded.
A malformed target disables searching rather than silently reverting to broad mode.
Omitting the target retains the existing broad mode.

In targeted mode, each newly observed supported descriptor gets cheap byte-exact
checks at the original vertex offset and at vertex zero. Those checks have a
separate **2 MiB read budget per lane** and stop at the first differing sample.
They can find an unchanged shared-buffer layout or a copied/isolated prefix even
when the total descriptor size or draw count differs. They retain the runtime
stride, input rate, format and bounds validation; there is no numeric tolerance.

Only a descriptor with the original shared-buffer vertex count or the isolated
slot vertex count receives the full search at arbitrary vertex offsets. The count
is derived from its descriptor size, attribute offset, stride and XYZ width.
`AmdGpu::Buffer::GetSize()` uses stride times record count for strided buffers.
A different stride is allowed, so a byte-preserving repack can still qualify.
**Count equality prioritizes work; it does not establish resource identity.**

The full search still has a **64 MiB/lane total and 2 MiB/binding cap**. It can now
use a short final chunk when enough budget remains for a complete prefix window.
When even one window cannot fit, it records a budget skip without admitting a new
zero-work full-scan cache entry. `scan-complete` distinguishes successful search
completion from failed/truncated work. Cached truncated results remain incomplete.
Targeted mode permits 4,096 layouts; broad mode retains its 1,024-layout limit.

Up to 12 distinct donor-shaped candidate bindings are reported at command recording,
with direct/indirect path, shader hashes, addresses and scan outcome, even without
a matching prefix. Fresh prefix matches still use the existing separate logs and
sampled-index reference check. The caps of 16 hit lines and 4,096 hit validations
remain. Summary counters are per lane every five seconds; do not sum cumulative
lines. No GPU waits, downloads, writes, guest code patches or memory protection
changes were added.

### Verify position bytes at the file-read boundary

The preparation tool now writes a **v2 RPK trace manifest**, containing only the
bounded expected position prefix and its layout, in addition to the three spans.
It checks these bytes against the local source file before writing the manifest.
At a read covering the complete sample window, RpkTrace compares those bytes via
the backing reader and reports `position-prefix=verified`, `MISMATCH`, `unreadable`
or `not-covered`, and `prefix-guest`. This is separate from `header=verified`.
Version 1 manifests remain accepted but cannot perform the new position check.

There are still at most 64 trace events per process. Each can make one 20-byte
header read and at most eight XYZ reads (maximum 96 additional bytes), strictly
within the observed read interval. Split windows are reported as not covered;
reads are not stitched across potentially reused buffers. Sequential read offsets
remain observations subject to concurrent descriptor use, not allocation tracking.

## Exact next experiment

In **MSYS2 CLANG64**:

```bash
cd /h/DC-Track-Test
set -euo pipefail
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
mkdir -p work
bash tools/track_import/test.sh 2>&1 | tee work/targeted-search-tests.log
bash tools/modkit/test.sh 2>&1 | tee work/targeted-search-modkit.log
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
git rev-parse HEAD
```

Then run the complete, updated [PowerShell preparation and launch block](external-track-resource-trace.md#prepare-and-run-powershell).
It creates a **new** `road742-targeted-search-<GUID>` folder, prepares a byte-identical
stock override, preserves existing user/config paths, and sets:

```powershell
$env:BB_DMEM_MB='8056'
$env:BB_RPK_TRACE_FILE="$Input\rpk-trace.txt"
$env:BB_MESH_PROBE=(Get-Content -Raw "$Input\stock-probe.txt").Trim()
$env:BB_MESH_SEARCH=$Trigger
$env:BB_MESH_SEARCH_TARGET=(Get-Content -Raw "$Input\mesh-search-target.txt").Trim()
$env:BB_MESH_PROBE_CONTROL=$Trigger
```

Use the full linked block to supply `$Input`, `$Trigger`, launch arguments, fresh
folders and environment restoration. Reusing the previous v1 manifest would omit
the new file-read prefix verification. Arm after starting to drive Fraser Valley
Road Circuit Forward, as before. No visible change is expected from the stock copy.

Save **both stderr and stdout**. Required evidence: the first mode/target banner,
all RpkTrace lines, MeshSearchTarget summaries/candidate lines, any position-prefix
copy and referenced-prefix logs, positive-control result, last counters, commit
and executable hash, and preparation/scan JSON. No RPK, game asset or memory dump
upload is needed.

## Interpret the results before changing geometry

* `RpkTrace ... position-prefix=verified`: the expected donor bytes were present
  at a file-read destination. This does not demonstrate the native copy destination.
* `MeshSearch ... position-prefix-copy`: fresh donor-prefix bytes exist in an actual
  recorded draw's supported vertex binding. Correlate addresses, path and shader IDs.
* `referenced-prefix-indices > 0`: sampled direct-draw indices reference that prefix.
  This is the immediate desired connection to a specific rendering command; shared
  or duplicate geometry can still be ambiguous, and visible pixels are unverified.
* `shape-candidate path=indirect-unchecked`: a likely-sized binding was recorded on
  the indirect path. Indirect arguments are not decoded or treated as executed.
* File prefix verified but no draw prefix: inspect anchor/shape/coverage counters.
  Wrong layout, changed order or coordinates, different-sized copies, GPU-generated
  buffers, shader-side vertex fetching and unobserved native selection remain open.
  A count filter is a coverage restriction, not proof that other buffers are unrelated.

Negative descriptors remain cached for a run; later writes to the same address may
escape a recheck. No resource-lifetime, native deserializer, CPU/GPU copy-chain,
scene/LOD selection, shader-pulling or indirect-argument producer has been decoded.
`src/runtime.c` exposes HLE memcpy/memmove wrappers, but instrumenting those alone
would miss native inline copies. No copy-path change is justified by these logs.
No LOD, culling, rebasing, rendering fix or visible custom geometry is claimed.

## Verification for this change

* Full importer suite: **63 Python tests passed**, native mesh/track/resource tests
  passed, and Blender 4.3.2 smoke/hierarchy/instance checks passed.
* Modkit self-tests and ATRAC9 checks passed.
* Native broad/targeted search tests passed in diagnostics ON/OFF modes, also with
  AddressSanitizer and UndefinedBehaviorSanitizer. They cover independent anchor
  budgets, unrelated-layout filtering, changed stride/offset, direct/indirect paths,
  exhausted-budget cache rejection, partial final chunks and fresh reference checks.
* RPK trace tests cover v1/v2 parsing, source-span bounds, invalid prefixes, split
  reads, exact prefix comparison, mismatches and failed backing reads.
* Preparation tests check actual source prefix bytes, reject altered signatures,
  preserve source/copy SHA256 identity and refuse existing output directories.
* Both updated standalone diagnostics cross-compiled for Windows x64 with LLVM-MinGW
  and `-Wall -Wextra -Werror`. Windows binaries were not executed here.

The full Vulkan runtime is not buildable here because the required CMake/GPU
dependencies are unavailable. The Windows rebuild and real gameplay test remain
required. No game assets, raw user logs or memory dumps are committed.
