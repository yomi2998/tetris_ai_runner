# Supplemental legacy comparator record (PROFILE_CMP)

Diagnostic evidence only. Emitted by `tetris_profile_legacy_cmp`, a
separately built instrumented legacy binary. Never a replacement historical
baseline; the frozen `tetris_profile` binary and its V1/V2 output are
unchanged. Comparator rows are labeled by their own artifact hash and never
merged into the baseline series.

## Invocation

```bash
tetris_profile_legacy_cmp ... --quiet
```

`maxdepth` is bounded to 0-255, matching the value profile. Three modes:
`--telemetry off` detaches everything; `--telemetry on --timers off` counts
without timer reads and skips normalization (unique, unmatched, and all
timer fields report unavailable while counts stay numeric); full timers is
the default. Timed rows always use counters-only mode.

## Record format

A single line beginning with the token `PROFILE_CMP`, followed by
space-separated `key=value` fields in exactly the order listed below. Values
contain no spaces. `na` marks a measurement disabled by `--telemetry off`;
fields 16, 17, 28-32, 40, and 41 additionally report `na` under
`--timers off`. Absent columns never occur.

| Order | Key | Unit | Source and scope |
|---:|---|---|---|
| 1 | `moves` | count | Measured moves |
| 2 | `total_s` | s | Measured-loop wall (same scope as frozen `total_s`) |
| 3-7 | `min_ms` `median_ms` `p95_ms` `p99_ms` `max_ms` | ms | Per-move `run_hold` wall distribution (same scope as frozen per-move samples) |
| 8 | `eval_requests` | count | `Core::eval` entries per move |
| 9 | `eval_hits` | count | Depth-table hit branch per move |
| 10 | `eval_calls` | count | Actual policy evaluator calls per move; reproduces frozen `evals` |
| 11 | `transitions` | count | Completed `Core::get` calls per move; reproduces the frozen `transitions` column (counted by the wrapper named `gets` in the frozen source) |
| 12 | `searches` | count | `Search::search` invocations per move; reproduces frozen `searches` |
| 13 | `widening_iters` | count | `run_hold`/`run` outer-loop iterations per move |
| 14 | `parents` | count | `build_children` executions past the version check per move (accepted expansion transitions happen here; the span intentionally nests evaluation and transition work and is reported as an overlapping aggregate diagnostic) |
| 15 | `raw_landings` | count | Returned land points summed over search invocations |
| 16 | `unique_candidates` | count | Distinct normalized semantic candidates per invocation, summed |
| 17 | `unmatched_candidates` | count | Distinct unconvertible identities per invocation, summed; classified, never silently discarded |
| 18 | `materialized_nodes` | count | Search-child node initializations (`fresh + recycled`); equals `eval_requests` |
| 19 | `recycled_nodes` | count | Child nodes recycled from the free list |
| 20 | `search_roots` | count | Root allocations (at most one per `run_hold`) |
| 21 | `reused_nodes` | count | Children linked through status-identity reuse without re-evaluation |
| 22 | `dedup_survivors` | count | Linked children (`fresh + recycled + reused`); equals `raw_landings` when every land point links exactly one child |
| 23 | `path_states` | count | Valid discovered BFS states in final-path materialization (start and accepted early-exit goal included; successful marks that fail legality are excluded) |
| 24 | `path_ms` | ms | `make_path` production call total (timing evidence only; the profile applies through `attach` exactly as the frozen loop does) |
| 25 | `dead_moves` | count | Same contaminated definition as the frozen loop (warmup included) for parity |
| 26 | `games` | count | Same contaminated definition as the frozen loop for parity |
| 27 | `node_pool_bytes` | bytes | `memory_usage` byte deltas, same definition as frozen |
| 28 | `eval_hit_ns` | ns | Depth-table hit-path timer |
| 29 | `eval_miss_ns` | ns | Policy-evaluator call timer |
| 30 | `parent_ns` | ns | Parent-expansion span (overlapping aggregate, see field 14) |
| 31 | `search_ns` | ns | Search-invocation span (base call only; normalization sits outside it in field 41) |
| 32 | `transition_ns` | ns | Policy-transition span |
| 33-38 | `warmup_moves` `seed` `iters` `maxdepth` `budget_ms` `mode` | mixed | Same meanings as frozen V2 |
| 39 | `telemetry` | token | `on`, or `off` meaning the observer is detached and wrapper counting is skipped; boundary wall-time fields stay numeric |
| 40 | `alloc_ns` | ns | Search-child node allocation span (fresh plus recycled; roots excluded) |
| 41 | `norm_ns` | ns | Normalization and dedup phase span, outside every binding span by construction |
| 42 | `timers` | token | `on`, `off`, or `na` (when telemetry is off) |

Binding component rates derive as timer divided by count: per enumeration
call (31/12), per unique candidate (legacy numerator is the eval plus
transition spans (28 + 29 + 32) over field 16, covering attach, evaluation,
and transition per land point; the value side reports its rule span instead,
so the comparison carries both scopes explicitly), per eval request split by
hit/miss (28, 29 against 8), per policy transition (32/11), per materialized
node (40/18, allocation timed per search child with absolute overhead
evidence per the tiny-component rule), per selected-path state (24/23 with
differing early-exit behavior stated).

## Normalization identity

Land points normalize within one search invocation (current and hold
branches are never merged across invocations). The key is the sorted
occupied-cell set of the legacy status mapped through the existing
`ExternalPoseTransform` plus, for T pieces only, the spin class and
last-rotation channel preserved by the migration oracles. O rotation
collapses. Statuses that do not convert keep an opaque status-bits identity
inside the unique count and are reported under field 17. Dedup runs in a
reused linear-probe table over the full identity with exact comparison. The
algorithm and its directed fixtures (empty-board landing counts per piece)
are reviewed with the comparator implementation before binding use.

## Parity basis and known limits

- Fixed-iteration semantic and work parity: comparator `eval_calls`,
  `transitions`, and `searches` reproduce the frozen V2 totals exactly, and
  `dead_moves`, `games`, and `node_pool_bytes` match; verified by CTest on
  fixed workloads across seeds.
- Trajectory parity is observed at process level (separate deterministic
  runs), never by comparing sequentially constructed fresh engines inside
  one process. Sequential fresh legacy engines in a single process can
  disagree on selections and retained storage (first engine
  `memory_usage=8460840` selecting `T(6,2,1)` versus second engine
  `memory_usage=8491928` selecting `T(7,1,0)` on the empty map with piece T,
  lookahead `TOJ`, and 4 fixed iterations), reproduced with unmodified
  pre-7.1C sources built flag-off at `-O0` (`g++ -std=c++23 -O0 -I src`
  over `tetris_core.cpp`, `rule_toj.cpp`, `search_tspin.cpp`, `ai_zzz.cpp`,
  `random.cpp`; observed at both `-O0` and `-O1`). The effect is
  consistent with heap-reuse-dependent state in the legacy tree storage and
  predates this slice; both profile binaries construct one engine per
  process and advance it across moves exactly like the frozen loop, where
  runs are bit-identical.
- Timed comparisons never require identical trajectories.
- Overhead evidence is reported per component where measurable; tiny
  components carry absolute bounds. Per-invocation dedup uses a reused
  linear-probe table over the full key with exact comparison (no silent
  collision merging, no per-invocation allocation), so allocator churn
  between spans is structurally zero; any residual is measured and bounded
  with the absolute evidence. Measured rates and overhead evidence are
  reported separately; no global percentage is subtracted from any timing.
