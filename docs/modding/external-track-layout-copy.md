# Explain the rejected layouts and follow verified prefix copies

## Actual Windows evidence at 4ac977a

The repository was clean on `feat/external-track-foundation` at
`4ac977adf9bcbb5ca61e39e393b0de82014dafc5`. Both supplied logs were examined:
166 stderr lines and 2,941 stdout lines. Diagnostics are on stderr; stdout confirms
the stock override and normal process termination. Raw user logs are not committed.

RpkTrace verified the Road742 position bytes at **prefix addresses**
`0xf0026d30dc` and `0xf0026d249f`. The containing resource-read destinations were
`0xf0026cd5c0` and `0xf0026cc983`; those are not the prefix addresses. The index
header was verified at `0xf0026d5778`. The positive control passed at serials
19814063 / 19816275, using 204 indices and format 97/stride 8.

The final targeted counters establish why full-scan bytes were zero:

| Counter | Last value | What it establishes |
|---|---:|---|
| Cached supported layouts | 1,700 | Descriptor views admitted after format/rate/stride checks |
| Shape-skipped | 1,700 | All admitted views failed the exact native/slot count filter |
| Shape-candidates | 0 | No arbitrary-offset full scan was selected |
| Anchor-byte accounting | 29,880 | Independent anchor checks did execute |
| Anchor hits | 0 | Neither checked offset produced a complete matching prefix |
| Budget-skipped / cache-full | 0 / 0 | Capacity exhaustion did not cause this run's zero scans |
| Unsupported observations | 29,372,468 | Repeated attribute observations, not unique resources |
| Recorded draws without supported bindings | 4,608,485 | A coverage gap, not proof of vertex pulling |

The exact-count filter was too restrictive to admit any of the observed supported
views. This does not establish the size or encoding of the actual Road742 rendering
representation: the rejected sizes were not logged. The old anchor accounting also
could not distinguish a short descriptor, mismatching bytes or a bounds rejection.
The new counters separate those cases. No LOD, culling or index-rebasing cause is
established. No geometry replacement is part of the next experiment.

## New diagnostics

### Rejected descriptor table

Set `BB_MESH_LAYOUTS=1` alongside the existing armed MeshSearch. A thread-local,
fixed-size table groups descriptor observations by byte size, stride, Vulkan format,
XYZ width, attribute offset, direct/indirect path and rejection reason:

* `instance-rate`: not a per-vertex input.
* `unsupported-format`: renderer did not identify a supported float/half XYZ layout.
* `xyz-width`: supported layout has a different width from the donor (for example half versus float).
* `stride` or `bounds`: invalid for the supported byte reader.
* `record-count`: valid supported view with neither the native nor slot record count.
* `eligible`: passes these gates; this is not resource identity.

Each five-second reporting opportunity emits the eight most frequent **tracked**
tuples. There are at most 12 reports per lane, 1,024 slots and eight lookup probes
per observation. Collisions/full-table omissions increment `overflow`; the table
is not a guarantee of the global top eight when that value is nonzero. Counts are
cumulative observations (including repeats before command recording), not unique
buffers or confirmed submitted draws. `records` is `bytes/stride`, or zero for
stride zero. An example guest address accompanies each tuple.

New anchor counters report attempted windows, too-short windows, byte mismatches
and bounds/layout failures. Backing-read failures retain their separate counter.
The exact-count filter remains available as a cautious full-scan restriction;
this change does not merely increase the broad search budget.

### Trace actual HLE copy calls, then use their destinations

Set `BB_RPK_COPY_TRACE=1` from launch, with the existing **v2** trace manifest.
When RpkTrace verifies the actual position bytes, it seeds a small address catalog.
The existing guest libc `memcpy` and `memmove` wrappers in `src/runtime.c` then
observe copies covering the complete sampled prefix window:

1. Validate the source prefix immediately before the existing copy.
2. Execute the existing CRT or GPU-aware memory-copy path unchanged.
3. Validate the computed destination prefix after that copy returns.
4. Log verified source/destination addresses and admit the destination as a new
   historical candidate, allowing a short copy chain to be followed.

The tracer itself never performs a guest write, changes protection or requests GPU
synchronization. The original copy result and errno are preserved. A diagnostic
metadata mutex protects the small address catalog; it is never held during backing
reads or the actual copy. Diagnostic reads can still affect timing.

Limits per process after the first verified seed: **16 prefix addresses, 262,144
HLE helper calls, 64 candidate-copy verifications and 4,096 binding queries**.
A candidate check reads at most eight XYZ samples at the source and, on success,
eight at the destination. Expensive checks remain bounded even when stale addresses
are copied repeatedly. Exhausted call/verification budgets cannot be rearmed by a
later read. Read-source and copy-destination address seeds do not own allocations.

For a newly observed supported vertex binding, MeshSearch can query this catalog.
If a historical prefix address falls inside the descriptor and aligns with its
stride, it **freshly validates all position samples**. This uses the existing
anchor budget (charged for the maximum sample size). A verified address-derived
match bypasses the native/slot-count filter, so it can identify an interior region
of a larger shared buffer. The usual recorded-callback check and sampled direct
index-reference check still apply. `MeshCopyBinding` reports candidates and fresh
matches; `MeshSearch ... position-prefix-copy` is the recorded-draw evidence.

This is narrower than tracing every native allocation or memory operation. Native
inline copies, per-row repacking, GPU copies, transforms, vertex pulling through
storage resources and indirect argument execution remain unobserved. A matching
CPU-backed copy is not a GPU completion or visibility claim. Cached negative
bindings can still miss later writes at the same address. A historical catalog
entry never counts as a match without fresh bytes.

## Next Windows run

Use **MSYS2 CLANG64**:

```bash
cd /h/DC-Track-Test
set -euo pipefail
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
mkdir -p work
bash tools/track_import/test.sh 2>&1 | tee work/layout-copy-tests.log
bash tools/modkit/test.sh 2>&1 | tee work/layout-copy-modkit.log
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh
git rev-parse HEAD
```

Then use the complete updated [PowerShell preparation and launch commands](external-track-resource-trace.md#prepare-and-run-powershell).
They create a fresh `road742-layout-copy-<GUID>` directory with a byte-identical
stock RPK copy and new user/config paths. They retain `BB_DMEM_MB=8056` and now set:

```powershell
$env:BB_RPK_COPY_TRACE='1'  # active during loading after a verified RPK prefix seed
$env:BB_MESH_LAYOUTS='1'   # summaries after the usual gameplay trigger
```

Do not insert these after `Start-Process`; the child must inherit them at launch.
Follow the full script's normal route/arming instructions. Existing saves, archives,
executables and experiments are not overwritten. No visual change is expected.

Return both stdout/stderr plus the generated diagnostic-lines, commit/hash metadata,
preparation report and focused scan. Do not upload RPKs, game assets or memory dumps.

## Evidence to look for

```
RpkTrace: ... position-prefix=verified
RpkCopy: seeded verified prefix=...
RpkCopy: HLE copy calls observed=...
RpkCopy: ... source-prefix=... destination-prefix=... destination=verified
MeshLayouts: ... bytes=... records=... stride=... format=... reason=...
MeshCopyBinding: ... candidates=... fresh-matches=...
MeshSearch: ... position-prefix-copy ... referenced-prefix-indices=...
```

A verified copy destination connects an actual helper call to the known prefix.
A fresh recorded binding match with positive referenced-prefix indices is the next
render-command milestone. An indirect binding hit still requires investigating its
argument producer; indirect bytes are not read as if they were executed commands.

If seeds appear but no helper calls do, this hook does not cover the observed path.
If calls exhaust their budget without a candidate, that is incomplete copy coverage.
If a copy destination is verified but no supported binding contains it, use that
address and the rejection table to choose the next observer (for example shader
buffer resources), rather than replacing another arbitrary road mesh. No copy,
selection or rendering outcome is claimed before the Windows logs establish it.

## Verified development results

The interrupted worktree was preserved and finished; it contained these diagnostics,
their tests and this guide. No additional geometry experiment was substituted.

* The importer suite passed **63 Python tests**, the native probes, and Blender 4.3.2
  smoke/hierarchy/instance checks. Modkit self-tests and ATRAC9 checks passed.
* Native diagnostic tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
  Cases include rejected-layout accounting/overflow, anchor coverage, a prefix inside
  a larger buffer using copy metadata, copy chaining, overlapping memmove, stale
  source bytes, an unchanged destination, overflow and budget exhaustion.
* The final copy test also checks ingestion from a verified RPK read and rejects a
  contradictory resource header before seeding the address catalog.
* Updated standalone layout and copy tests cross-compiled to Windows x64 using
  LLVM-MinGW with `-Wall -Wextra -Werror`; they were not executed on Windows here.
* Modified runtime C sources passed syntax checks with diagnostics ON and OFF.

The full Vulkan runtime cannot be built here because its CMake/GPU dependencies are
unavailable. New HLE-copy and draw-binding integration still needs the Windows
rebuild and gameplay test above. Synthetic tests do not demonstrate native Road742
selection, GPU completion, visible custom geometry or drivability.

Implementation files: `gpu/shim/bbport_mesh_search.h`, `src/runtime.c`,
`src/runtime_rpk_trace.h`, new `src/runtime_rpk_copy.h` and
`src/runtime_rpk_copy_impl.h`; tests: `tools/mesh_search_test.cpp`,
`tools/rpk_copy_test.c`, `tools/test_resource_trace.sh`; documentation: this report
and the updated resource-trace launch guide. Private runtime logs are retained
locally and are not committed.
