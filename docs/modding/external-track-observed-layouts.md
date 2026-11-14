# Forward: scan observed layouts and retire stale copy sources

## Recovery and evidence

Recovered branch `feat/external-track-foundation` at
`dfca1f1c1120af7210599cb453cb71f9558cb0e7`, matching the fork; the worktree was
clean before this continuation. The existing layout/copy diagnostics were already
implemented and committed. This change extends them, preserving the importer.

Both complete supplied Forward logs (262 stderr lines, 2,122 stdout lines) and the
focused scan were inspected. Diagnostics are on **stderr**. stdout confirms the
stock override and normal termination. Private logs and assets are not committed.
Reverse requests another path and is outside this experiment.

* RpkTrace verified the position prefix at `0xf0026dc1bc` and `0xf0026db57f`,
  resource `0003a8908986f62b`. These are historical read addresses, not persistent
  ownership or GPU allocation identifiers.
* Index resource `0004f1b2dfbf0361` had a verified header. The focused scan places
  Road742's 195 indices at index element 462507. The header-containing read was
  only 26840 bytes: header verification does **not** verify that distant index window.
* Positive control captured 4263025 and replay-matched 4263714 (138 indices,
  float XYZ format 106, stride 12). It validates that sampled draw, not Road742.
* Last search snapshot: direct 9,870,784; indirect 292,752; cached views 1,418;
  full-scan bytes zero. All 1,418 supported views failed the native/slot count gate.
* Anchors did execute: 22,476 bytes, 1,872 attempted windows, 1,872 mismatches,
  964 too-short windows, no read/range failures. Neither sampled offset matched.
* Copy tracing seeded both addresses, then exhausted 64 source checks without
  any destination line. In the old code, every successfully verified source
  produces an end/destination line. Failures were silent and remained eligible
  indefinitely. Repeated stale sources can therefore exhaust the budget; the old
  log cannot distinguish byte mismatch from unreadable backing or count repeats.

Five supported rejected layouts appear in the complete log, all format 106:

| Records | Stride | Descriptor bytes | Example guest address |
|---:|---:|---:|---|
| 37334 | 44 | 1642696 | `0x50ff527cc0` |
| 37449 | 44 | 1647756 | `0x50ff2417a0` |
| 52376 | 28 | 1466528 | `0x50b592a824` |
| 51386 | 16 | 822176 | `0x509b876400` |
| 18057 | 44 | 794508 | `0x50ff920a70` |

These are observed supported views, **not** identified Road742 resources. Histogram
omissions/overflow mean they are not a census of all possible layouts. None is
asserted to contain the donor. The native scene-object dependency in the focused
scan does not decode scene selection, transform, shader pulling or allocation ownership.

## Implementation

`BB_MESH_SEARCH_ALLOW=l1:37334/44,37449/44,52376/28,51386/16,18057/44` adds exactly
these record-count/stride pairs to targeted full-scan eligibility. It requires a
valid target, format 106/12-byte XYZ, per-vertex rate, complete records and an
attribute contained within each record. At most eight distinct pairs are accepted;
invalid syntax or oversized entries disable search rather than enabling broad mode.
The default behavior is unchanged when this option is absent.

The same 64 MiB per-lane and 2 MiB per-binding budgets apply, including chunk overlap.
All five observed views fit with overlap. The search examines exact XYZ bytes at
every permitted record start, independent of the offline offset and index count.
It supports different strides and attribute offsets, not transformed, reordered or
quantized positions. Existing direct and async/indirect recording callbacks are
unchanged. Indirect arguments remain unchecked; no forced GPU synchronization or
guest writes are introduced. Negative cache entries still cannot detect later
rewrites at the same descriptor/address.

`MeshSearchAllow` separately reports admitted scans, completed searches and bytes.
`complete` means a readable search ended **or found a prefix**, not that every
vertex was scanned after a match. Bounded candidate lines include
`admission=observed-layout`, `scan-bytes`, `scan-fail` and `scan-truncated`.
Other existing cache/budget/unsupported counters retain their meanings.

The copy catalog now tracks a generation and live/busy state for each address.
A failed source check reports `prefix-mismatch`, `backing-unreadable` or `bounds`,
the failed sample, copy interval and retirement outcome. That generation is retired
and excluded from further checks and address queries. An independently verified
RPK read or copy destination can re-seed the address with a new generation; an old
in-flight failure cannot retire that newer generation. One verifier per generation
can be active at a time. Metadata locking never covers backing reads or game copies.
When all entries retire, observation pauses until an independently verified seed.

Partial-window overlaps are counted, with at most four examples, without consuming
source-verification checks. Self-copies consume no verification check. The existing
16-address, 262144-helper-call, 64-verification and 4096-binding-query limits remain;
re-seeding cannot reset exhausted budgets. A verified source/destination remains
only evidence of an observed HLE copy, not allocation lifetime or rendering.

## Windows experiment

In **MSYS2 CLANG64**:

```bash
cd /h/DC-Track-Test
set -euo pipefail
git status --short
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
RunLog="work/observed-layout-build-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RunLog"
bash tools/track_import/test.sh 2>&1 | tee "$RunLog/importer.log"
bash tools/modkit/test.sh 2>&1 | tee "$RunLog/modkit.log"
BB_DIAGNOSTICS=ON BB_PGO=off BB_LTO=OFF bash build.sh 2>&1 | tee "$RunLog/build.log"
git rev-parse HEAD | tee "$RunLog/commit.txt"
```

Then use the complete [PowerShell preparation and launch block](external-track-resource-trace.md#prepare-and-run-powershell).
It now enables this allowlist and creates `road742-observed-layouts-<GUID>` with a
byte-identical stock `_n.rpk`, fresh user/config/log paths and a unique executable.
Keep `BB_DMEM_MB=8056`. Arm only after entering **Fraser Valley Road Circuit Forward**.
No visible geometry change is expected: this is a stock resource diagnostic.
Do not reuse an old trigger file. Do not substitute the Reverse RPK.

Expected evidence:

| Message | Interpretation |
|---|---|
| `explicit layout allowlist enabled tuples=5` | New configuration is active |
| `MeshSearchAllow: ... scans>0 ... bytes>0` | Larger observed layouts actually received full scans |
| `admission=observed-layout ... scan-truncated=0 scan-fail=0` | This candidate search completed or found its prefix |
| `source-rejected ... retired=1` | This historical source generation failed; it will not consume repeated checks |
| `partial-window ... no source verification charged` | Observed copy covers only part of the sampled window |
| `destination=verified` | A real HLE helper copied the verified bytes to this destination |
| `MeshCopyBinding ... fresh-matches>0` | Fresh bytes link a historical address to a supported binding |
| `position-prefix-copy ... path=direct ... referenced-prefix-indices>0` | A recorded direct draw samples indices referencing the matching prefix: desired next milestone |
| `position-prefix-copy ... path=indirect-unchecked` | Matching binding bytes; indirect execution/selection still unproven |

Return stdout and stderr, diagnostic-lines, commit/hash/arming metadata, preparation
report and focused scan. Do not upload game archives, binaries or memory dumps.
If scans remain zero, the allowlist banner and layout rejection table distinguish
configuration failure from changed observations. Exhausted budgets, cached negatives,
unsupported formats and zero matches remain inconclusive about Road742's presence.

## Validation

* Importer: 63 Python tests passed; native mesh/track probes and Blender 4.3.2
  smoke, hierarchy, instance and reflected-winding checks passed.
* Resource diagnostics: diagnostics ON/OFF, broad/targeted modes, malformed
  allowlist and missing-target gating passed, including ASan/UBSan. New coverage
  includes an interior prefix in the observed 37334/44 layout, an attribute offset,
  a complete negative scan, cache reuse, rejection of unsafe allowlist entries,
  one-check stale retirement, re-seeding, old-generation protection and busy claims.
* Linux and LLVM-MinGW Windows x64 runtime C syntax checks passed ON/OFF.
  Copy and search standalone tests cross-compiled with warnings as errors in both
  modes. Windows executables were **not executed** here.
* Modkit self-tests and ATRAC9 encoder checks passed. No remaining test failures.

The full Vulkan runtime cannot be built/run in this environment (CMake and GPU
build dependencies are unavailable). Windows integration and gameplay validation
remain required; the verified build above is the standalone diagnostic code.

Modified files: `gpu/shim/bbport_mesh_search.h`, `src/runtime_rpk_copy_impl.h`,
`tools/mesh_search_test.cpp`, `tools/rpk_copy_test.c`, `tools/test_resource_trace.sh`, this report and the updated
`external-track-resource-trace.md` launch guide.

## Remaining blocker

No verified connection yet exists from Road742's RPK position prefix to a recorded
draw. Native inline copies, transformed/repacked vertex order, storage-buffer
vertex pulling, native road generation and indirect command producers remain
possible uncovered paths, not established causes. This experiment is intended to
locate a concrete binding/copy address before instrumenting another path. Native
LOD/culling behavior is not blamed. Custom visible geometry, collision and driving
on a custom road remain unverified.
