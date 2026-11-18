# Half-float consumed-resource pair: offline validation, no launch

**The real offline pair validation passed.** Continue with the
[one controlled two-signature runtime experiment](external-track-pair-runtime.md).
Do not repeat the offline scan unless its input hashes changed.

Recovered a clean branch at `25648877057c0e3651daecb06e908340edc537ec`, matching
the fork, with no stashes. Preserved all existing experiments. The retained
uploaded mesh reports contain only Road742, not the full report or this pair's
`selection.json`. The following real-slot metadata comes from the user's latest
results; exact index ranges, record offsets and signature uniqueness still need
the local offline check below. No original RPK or save is changed.

## Findings and correction of the previous gate

| Slot | Vertex span length | Triangles / index count |
|---|---:|---:|
| `00087c1471eba534_0_polySurfaceShape1222` | 28 | 32 / **96** |
| `0008877f16d366b9_0_pPlaneShape147` | 64 | 62 / **186** |

Both reference position/normal `0003d12ccbd11945`, UV `000380084a65ecc7` and
index `00042bcd5555ffa6`. The position resource has 92 records × 12 bytes =
1104 data bytes, or 1132 bytes including its 28-byte header. The 28/64 values are
`vertex_last - vertex_first + 1`, not necessarily counts of uniquely referenced
vertices. Their sum being 92 **does not prove disjoint ranges or a partition**.
The slots have different mesh resource IDs; sharing buffers does not merge their
draw records or establish a common scene instance, material, LOD or visibility.

The earlier `position_half_float=false` condition was unnecessarily restrictive
for **signature matching**. It was a conservative experiment-selection gate,
not a limitation of MeshProbe or proof of incompatibility. The replacement check
does not simply remove it or choose the first row. It validates both exact slots,
retains all importer safety checks, and makes no target selection.

Existing code already supports this representation:

- `tools/modkit/src/cmd_mesh.cpp::PositionKind` recognizes the established half
  position formats. Its signature uses **six original bytes** for XYZ, sampled
  at the resource stride; no conversion or rounding is involved.
- `gpu/shim/bbport_mesh_probe.h::Parse/Positions/ReadVertex` accepts width 6 and
  checks stride, binding ranges, attribute offset and signed base vertex. It
  compares the original bytes. It also requires a paired index-prefix match,
  permitting only constant index rebasing.
- `vk_rasterizer.cpp::ResolveVertexBuffers` reports width 6 only for Vulkan
  `R16G16B16Sfloat` or `R16G16B16A16Sfloat` inputs. Other formats are not treated
  as half positions. If native processing converts/reorders the data, this probe
  can fail to match; it does not guess a conversion or reinterpret integer data.
- `known_prefix_decodable` historically reports `Fields.partial`, meaning **only
  a known prefix before an unsupported suffix**. False can mean either complete
  decoding or unavailable decoding. It is not a general success flag. New
  additive `layout_decode_status` explicitly reports `complete`,
  `known_prefix_only` or `unavailable`. The old field is preserved for consumers.

The two candidate signatures are distinguishable **from each other** because
MeshProbe first requires an exact index count, 96 versus 186. Neither resource
name nor half precision is used as an identity shortcut. Uniqueness against all
recognized slots and actual raw-byte validity have not yet been established for
the user's report. A signature match would still mean sampled byte-content
association at a recorded direct draw, not unique ownership or visible pixels.

## Minimal implementation

The existing `prepare_resource_trace.py` now has `--inspect-consumed-pair`.
It reads the existing full report and invokes the existing scanner for the two
exact target names only. This is not another broad scan or parser profile.

It checks fresh signatures/ranges/record offsets against the report, reuses
existing source-header/ID/prefix validation, verifies all 92 half XYZ records are
finite, checks the complete 96/186 index ranges and their vertex bounds, and
compares the reported native submesh and draw-record bytes with the original RPK.
No unknown field widths are inferred. It hashes the source, dependencies and
input report before/after, refuses stale data and existing output directories,
and writes only `donor-pair.json`. No RPK copy, import, launch or selection occurs.

The report gives actual vertex/index ranges, resource-relative draw-record
positions and absolute file offsets. A draw record at offset `d` is relative to
its mesh payload, not an offset into a guest allocation or GPU command buffer.
Index entry `i` is at index payload offset `28 + 2*i`; position record `v` is at
position payload offset `28 + 12*v`. These are different coordinate systems.

The full-report alias comparison includes constant index rebasing, differing
sample starts and unequal prefix lengths. Potential aliases are retained and
block the `signatures_distinct_in_report` result; they are never silently ranked
away. The comparison concerns the same binding in recognized report entries.
It cannot exclude external resources, unrecognized slots, multiple matching
attributes, batching or transformed geometry. This is not proof of draw identity.

## Exact next Windows action: offline pair validation only

Do not use the previous consumed-donor launch block. No game rebuild, debugger,
save preparation or gameplay is needed. First update/build **dcmod only** in
MSYS2 CLANG64; preserve local changes if the guard stops.

```bash
cd /h/DC-Track-Test
set -euo pipefail
if [[ -n $(git status --porcelain) ]]; then echo 'Preserve local changes before updating'; exit 1; fi
git switch feat/external-track-foundation
git fetch https://github.com/orsarusi312/driveclub-recomp.git feat/external-track-foundation
git merge --ff-only FETCH_HEAD
bash tools/modkit/build.sh
bash tools/test_mesh_probe.sh
CheckDir=$(mktemp -d work/half-pair-check-XXXXXXXX)
clang++ -std=c++17 -O1 -Wall -Wextra -o "$CheckDir/make-fixture.exe" \
  tools/track_import/tests/make_fixture.cpp tools/modkit/src/rpk.cpp
"$CheckDir/make-fixture.exe" "$CheckDir/test.rpk"
DCMOD="$(cygpath -w "$PWD/out/dcmod.exe")" \
TRACK_FIXTURE="$(cygpath -w "$PWD/$CheckDir/test.rpk")" \
python3 -m unittest discover -s tools/track_import/tests -p test_consumed_pair.py -v
```

Then run in PowerShell (all outputs go to a new directory):

```powershell
$ErrorActionPreference='Stop'
$Repo='H:\DC-Track-Test'
$Run="$Repo\work\half-donor-pair-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory $Run | Out-Null
git -C $Repo rev-parse HEAD | Set-Content "$Run\commit.txt"
$Arguments=@(
  "$Repo\tools\track_import\prepare_resource_trace.py",
  '--dcmod',"$Repo\out\dcmod.exe",
  '--source',"$Repo\work\visual-recovery\worlds\canada_road_circuit_02_n.rpk",
  '--dependency',"$Repo\work\dependencies\global.rpk",
  '--dependency',"$Repo\work\dependencies\worlds\canada_common.rpk",
  '--inspect-consumed-pair',"$Repo\work\visual-recovery\mesh-scan.json",
  '--output',"$Run\result"
)
# Process redirection avoids PowerShell 5's NativeCommandError formatting.
$QuotedArgs=($Arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
$Python=(Get-Command python -CommandType Application | Select-Object -First 1).Source
$Process=Start-Process -FilePath $Python -ArgumentList $QuotedArgs -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput "$Run\stdout.log" -RedirectStandardError "$Run\stderr.log"
$Code=$Process.ExitCode
$Code | Set-Content "$Run\exit-code.txt"
Get-Content "$Run\stderr.log"
if($Code -ne 0) { throw "Offline validation stopped. Return $Run logs; do not launch or substitute a slot." }
$Result=Get-Content "$Run\result\donor-pair.json" -Raw | ConvertFrom-Json
$Result | Select-Object signatures_distinct_in_report,pair_distinguished_by_index_count,vertex_span_ranges_overlap,index_ranges_overlap,selected_target,launch_prepared
$Result.candidates | Select-Object target,index_count,vertex_range_inclusive,index_range_half_open,draw_record_offsets,possible_signature_aliases_in_report | Format-List
Write-Host "Return result\donor-pair.json, commit.txt, exit-code.txt, stdout.log and stderr.log from $Run. No game launch."
```

A successful report must have two byte-verified candidates, index counts 96/186,
`selected_target=null`, `launch_prepared=false` and `runtime_verified=false`.
Empty alias lists support distinguishable offline signatures; nonempty lists or
validation errors are explicit blockers. The next decision can then use real
ranges and signatures, rather than choosing a name or treating the shared
resource as one unique mesh. No game archives, saves or textures should be sent.

The exact remaining milestone is an instance-specific paired index/position
match at a recorded draw. This change validates the input for that test; it does
not establish native vertex transfer, a draw match, visible custom geometry or
collision. Tsukuba integration remains downstream of visible proof.

## Validation completed here

- dcmod rebuilt successfully on Linux.
- **117 Python tests passed, no skips**, using the synthetic RPK fixture and the
  existing Capstone-enabled test environment. Nine pair tests cover malformed
  ranges, corrupt headers/positions/indices/draw records, nonfinite half values,
  complete-versus-partial status, rebased signature aliases, stale/incomplete
  reports, changed inputs, existing-output refusal and a real dcmod end-to-end
  shared-half-buffer pair. A separate multistream test verifies half export and
  unchanged source bytes.
- Native MeshProbe tests passed in diagnostics ON/OFF builds, normally and with
  ASan/UBSan: exact half-byte matching, paired count discrimination, rebasing,
  attribute offsets, range/format failures and existing trigger/budget behavior.
- Modkit self-tests and ATRAC9 round trip passed.
- Standalone probe harness cross-compiled for Windows x64, diagnostics ON/OFF;
  not executed on Windows. No game executable was rebuilt or launched.
- `git diff --check` passed. The Windows commands and the user's original pair
  still require local execution; synthetic success is not native game evidence.
