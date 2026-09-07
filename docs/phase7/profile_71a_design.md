# Slice 7.1A design note: profile comparison contract (revision 3)

Design only. No source implementation and no default-target switch are
authorized in this slice. All section references point at
`docs/fast_reachability_port_plan.md` unless stated otherwise. This revision
supersedes revision 2 following review; known revision 2 errors are called
out explicitly so they are not reintroduced.

## 0. Standing defects in the current harness

These behaviors of `src/tetris_profile.cpp` are frozen, not silently fixed.

| # | Behavior | Treatment |
|---|----------|-----------|
| 1 | `--no-hold` is parsed and printed, but both search calls pass `true` for `run_hold` | Legacy artifact keeps the no-op. Value honors the flag with the approved root-only meaning in Section 3: no executed hold at each root, hold state stays empty in the live trajectory, deeper hypothetical hold branches unchanged. Stated in help text and schema docs. Hold-disabled runs are excluded from legacy comparisons. No whole-tree suppression is authorized. |
| 2 | The per-move timer surrounds the whole `run_hold` call (root update plus search); no path is materialized | Value defines `T_ROOTSEARCH` (root update plus search) as the legacy-scope measure and keeps run-only timing as a separate diagnostic (Section 4). Differently scoped timers are never compared under one label. |
| 3 | Warmup moves are excluded from time and work vectors, but `dead_moves`, `games`, clears, and attack still include warmup | Legacy artifact keeps this contamination frozen; the affected legacy columns are marked contaminated diagnostics. Value excludes warmup from every aggregate. Binding quality comes from the tuner comparison mode (Section 14.4), not from profile columns. |
| 4 | Root policy is rebuilt each move, including explicit T-value seeding | Mapped onto existing value-policy facilities (Section 3). The full root `evaluate` cost is charged to setup and reported separately from search evaluations. |
| 5 | Death uses the legacy target-anchor test (`row >= 20`) | Legacy artifact keeps it. Value uses lowest-mino lockout plus a separate spawn-obstruction check (Section 14.2). Disagreements are classified with counts. |
| 6 | `--telemetry off` disables wrapper counters but does not zero every field the V2 schema says it zeroes | Legacy artifact keeps exact current behavior. V3 defines `off` as real disabling: boundary wall-time fields stay numeric, component counters and timers report unavailable, never zero (Sections 4 and 5). |

`PROFILE_V2` and the byte-growth label from Phase 0 are reused unchanged.

## 1. Build artifacts and source separation

### Proposed targets

| Target | Role | Status |
|---|---|---|
| `tetris_profile` | Legacy profile, current sources, current behavior | Default; unchanged until its gate passes and the switch is independently approved |
| `tetris_profile_value` | Value-engine profile implementing this contract | New comparison artifact; never the default in this phase |
| `tetris_profile_legacy_cmp` | Supplemental instrumented legacy comparator (Section 7) | Proposed new build from legacy sources plus scoped timers and request/hit/miss counters; explicitly not the historical baseline |

No runtime engine selector. No release compatibility adapter.

### Proposed sources

- `src/tetris_profile.cpp` (frozen): byte-unchanged, including its scenario
  loop and attack switch. It does not call any new helper.
- `src/profile_value_support.h` (new, engine-free, value side only): argument
  parsing, scenario generation, attack and combo accounting, and percentile
  reporting for the value harness and for tests. It is not shared with the
  legacy source; equivalence with the legacy algorithm is proven by test-only
  differentials (Section 6), not by shared naming.
- `src/tetris_profile_value.cpp` (new): value move loop, timing boundaries,
  V3 reporting. Links `src/tetris_engine.cpp`, `src/toj_policy.cpp`, and the
  `fast_reachability` interface target only. It must not include
  `tetris_core.h`, `rule_toj.h`, `ai_zzz.h`, or `search_tspin.h`.
- Test support only: `tests/profile_scenario_tests.cpp` (generator
  determinism under scripted event schedules, attack-function differential
  over the full outcome space, schema field order, counter lifecycle
  identities, evaluation aggregation identities) plus a test-only legacy
  move driver (Section 7) that exposes per-move selections for trajectory
  parity evidence. Tests may reference legacy headers for differential
  checks; that does not modify the frozen source. No production library
  composition changes; final CMake composition stays deferred.

### Frozen comparison inputs (Section 17.2)

Before any timing claim: produce immutable `tetris_profile.baseline` and
`tetris_profile_value.candidate` copies plus one absolute frozen 29-double
parameter file, record SHA-256 hashes for all three, and make the copies
read-only. Never overwrite frozen artifacts. All artifacts use the same
absolute parameter file and the same combo-table bytes; the combo-table
content and its hash are recorded alongside the binary hashes. The
supplemental comparator gets its own hashed copy and never replaces the
historical baseline.

## 2. Scenario contract

The value-side generator replicates the legacy algorithm exactly:

- RNG: `std::mt19937` seeded once from `--seed`. No reseeding on restart.
- Bag base order: `I J L O S T Z` (ASCII sort of the legacy `get_generate`
  map keys, which is what `TetrisContext::prepare` iterates when building
  `index_to_type_`). Spelled as a compile-time constant per Section 14.2,
  never derived from container iteration at run time.
- Refill: at each move, pop the front piece first, then while `next.size() <=
  maxdepth` append one full base-order bag and shuffle only the newly
  appended bag with the shared RNG.
- Death: clear the queue entirely, so the next move deals a fresh shuffled
  bag from the advanced RNG state.
- Piece mapping: legacy chars map to `tetris::Piece` by name; `' '` means
  empty hold.

Identity holds only under identical consumption and restart events. Once the
engines choose different holds or deaths, their game streams legitimately
diverge and are not expected to resynchronize. Generator tests therefore use
controlled event schedules (scripted holds, deaths, and consumption counts)
and assert identical streams schedule by schedule, rather than asserting
cross-engine stream equality on live trajectories.

## 3. State contract per move

### maxdepth and queue conversion

`maxdepth N` builds a concrete queue of exactly N+1 pieces (current first),
zero boundary markers, `marker_count 0`, cursor 0. Supported range is N in
`[0, 255]`, since the queue must be nonempty and hold at most
`max_queue_length = 256` pieces; parsing rejects out-of-range values before
conversion with overflow-safe checks. Revision 2 wrongly claimed search
always explores N lookahead plies: the actual search horizon follows the
existing hold-dependent rule (`new_max` equals lookahead count, plus one when
a hold piece is present and the raw queue extends beyond one move or hold is
unlocked). The queue content contract above is exact; horizon behavior is the
frozen engine rule, not a profile invention.

### Hold

- Legacy char `' '` maps to empty `HoldState`; any other char maps to the
  named piece.
- At each ordinary profile root, hold is unlocked. This reproduces the legacy
  loop, which passes `hold_free = true` on every move; it is normal
  between-piece behavior, not a departure from real-game locking (in a real
  game the lock lasts from the hold down to its placement, then releases).
- Approved `--no-hold` meaning for this slice: no executed hold at each
  root, using the existing root hold lock. Hold stays empty in the live
  profile trajectory; deeper hypothetical hold branches remain unchanged.
  This meaning appears in help text and schema documentation. No whole-tree
  hold-suppression option is authorized.

### Root policy initialization (caller-owned)

The caller owns a single `toj_policy::Config` (combo table pointer, table
max, parameters) for the whole run and a single `toj_policy::Policy`
initialized once via `Policy::init`; config lifetime covers the engine that
borrows it. Per move, the root `toj_policy::State` is seeded field by field
from the legacy assignments using existing value facilities only:

| Legacy assignment | Value source |
|---|---|
| `death = 0`, `under_attack = 0`, `map_rise = 0`, `acc_value = like = value = 0` | Literal zeros |
| `combo`, `b2b` from harness counters | Harness combo/b2b counters (same integers the accounting function maintains) |
| `safe = get_safe(map, current)` | `config.safe = policy.safe_margin(board, current)` before `set_root`; `Policy::init` once provides the danger table |
| `init_t_value(map, t2, t3)` without overlay map | `policy.evaluate(pre-move board)`, which runs the same scan and returns `t2_value`/`t3_value`; only those two fields are copied into the root state |

The root `evaluate` call is charged to `T_SETUP` and reported in
`setup_eval_ms`, never folded into search evaluation counts. No post-move
policy object is substituted wholesale for the next root. If a differential
shows the `evaluate`-sourced T seed diverging from the legacy scan on any
profile board, that returns as a reviewer question; no new seeding API is
invented in the implementation slice.

### Combo, B2B, attack, perfect-clear accounting

The value-side accounting function implements the exact legacy switch over
`(clear_count, spin class, board-empty)` with the profile combo table
`{0,0,0,1,1,2,2,3,3,4}`, table max 10, and the `+6` perfect-clear bonus. The
legacy source keeps its own copy untouched; equivalence is proven by a
differential test enumerating the full outcome space (clear 0-4, all spin
classes, empty and nonempty results). Engine-internal `State` fields drive
search only; reported aggregates come from the accounting function.

### Evaluation model (corrected; revision 2 falsely assumed legacy has no evaluation reuse)

The legacy engine reuses evaluation results. `Core::eval`
(`src/tetris_core.h`) attaches the candidate, hashes the resulting map,
looks the hash up in a depth-specific transposition table, and calls the
policy evaluator only on a miss. The frozen `evals` wrapper therefore counts
actual evaluator calls, not evaluation requests. Consequences:

- Frozen `evals` must not be equated with value `eval_requests`. It is a
  legacy evaluator-call count usable as a diagnostic; binding eval
  comparison needs the comparator-measured legacy request/hit/call split.
- The legacy depth-table key semantics must not be treated as equivalent to
  the value exact-occupancy verification. Key-semantics differences belong in
  the count classification with evidence, and the legacy table behavior is
  never altered.

Value aggregation, defined explicitly over one `evaluate_once` call: every
call increments `eval_requests`. The per-expansion memo list is scanned
first; a hit increments `eval_memo_hits` and returns. Otherwise, with the
cache enabled, the `EvalCache` layer is engaged exactly once: the reported
`cache_requests`, `cache_hits`, `cache_misses` equal the live `EvalCache`
counters at snapshot read time (the identically named increments inside
`evaluate_once` are overwritten by `search_stats()`, so the live values win
and there is no double count). A cache hit returns the stored evaluation; a
miss runs the policy evaluator once, inserts the result, and increments
`eval_computed`. Testable identities: with cache enabled,
`eval_requests == eval_memo_hits + cache_requests`,
`cache_requests == cache_hits + cache_misses`, and
`cache_misses == eval_computed`; with cache disabled,
`eval_requests == eval_memo_hits + eval_computed`. The separate counts are
retained in the schema; no layer is folded into another.

### Death, restart, application, and failure handling

Value death rule, evaluated in order per move:

1. Canonical spawn `(4, 20, 0)` does not fit the active piece on the pre-move
   board (`tetris::toj::fits` fails): ordinary death before search, matching
   legacy `node->check(map)` failing.
2. `Policy::is_lockout(played, placement)` on the finalized selection is
   true: ordinary death without applying. This is the canonical correction
   corresponding to legacy `row >= 20`.
3. Otherwise the verified path is applied through `tetris::toj::apply(board,
   piece, candidate)`, which yields the resulting board, clear count, spin
   classification, and perfect-clear flag through the existing rule API (the
   same function family engine expansion uses). Clears and attack are scored
   with the accounting function; the resulting board becomes the next root
   board.

There is no second pathfinder replay in `T_APPLY`: `finalize` already ran the
locked production verification. `T_APPLY` covers the rule `apply` plus
harness accounting only.

On ordinary death (steps 1-2): reset board to empty, clear queue and hold,
zero combo and b2b, increment `dead_moves` and `games`. Spawn obstruction
stays a separate check from lockout. Cases where the old bounding-row anchor
and the lowest-mino rule disagree are logged as classified divergences.

Rejected roots are not ordinary deaths. A `set_root` rejection from an
invalid queue, a full input row, or unusable engine capacity is a
harness/configuration failure: the run is marked invalid and its numbers are
discarded, including during warmup, never silently restarted and continued.
A blocked active spawn (step 1) remains an ordinary death.

Finalization failures (no selection on a fittable board, missing candidate
content, or unverified path) are likewise never ordinary deaths: any such
failure, in warmup or measured moves, invalidates the whole qualification
run.

### Warmup

Warmup moves execute the full pipeline including search and finalization, but
contribute to nothing: no time samples, no counter reads, no
`dead_moves`/`games`/clear/attack aggregates. A warmup finalization failure
or rejected root invalidates the run exactly like a measured one.

## 4. Move execution and timing boundaries

The value move performs all five steps in order:

1. Prepare root inputs (Section 3).
2. Update the engine root (`set_root` / reroot reuse).
3. Run the chosen budget.
4. Finalize exactly once from canonical spawn.
5. Apply the move only on verified output.

No scalar test interpreter runs inside release timing; verification replay is
the locked production replay owned by `finalize`.

### Timer definitions (all on `steady_clock`)

| Timer | Scope | Reported as |
|---|---|---|
| `T_INIT` | Process start through engine init, policy init, parameter load, first allocation | `init_ms`, separate field, excluded from every total |
| `T_SETUP(m)` | Scenario pop/refill through root seeding (including the root `evaluate`) and root update | `setup_ms` total with `setup_eval_ms` subtotal; diagnostic split |
| `T_ROOTSEARCH(m)` | `set_root`/reroot plus `Engine::run(budget)` wall time | Per-move distribution in fields 3-7; the legacy-scope measure |
| `T_RUN(m)` | `Engine::run(budget)` wall time only | `run_ms` total; run-only diagnostic |
| `T_PATH(m)` | `Engine::finalize` wall time (finder construction, extraction, production replay) | `path_ms` total; per-result elapsed already instrumented |
| `T_MOVE(m)` | `T_SETUP` start through `T_PATH` end, contiguous, no gaps | `emove_*` distribution (end-to-end diagnostic) |
| `T_APPLY(m)` | Rule `apply` plus harness accounting | `apply_ms` total; inside loop wall, outside `T_MOVE` |
| Loop wall | First measured-move refill through last measured-move accounting | `total_s`; same scope as legacy `total_s` |

Revision 1 wrongly called run-only timing legacy-equivalent. The legacy
timer surrounds `run_hold`, which performs the root update inside the timed
region, so only `T_ROOTSEARCH` is comparable with legacy per-move samples.
One-time initialization is inside neither total (legacy `prepare` also runs
before its epoch, which resets at the warmup boundary).

Full production move time, used consistently as the path-overhead
denominator, is `T_MOVE + T_APPLY`: scenario refill, root preparation, root
update, search, finalization, rule application, and accounting. Only loop
framing outside these boundaries is excluded. This is a profile full-move
measurement; its path-cost share must not later be described as a DLL-only
production measurement without remeasuring the DLL boundary.

### Counter lifecycle

`SearchStats` resets on every successful root installation, clean or reroot,
and `EvalCache` counters reset with it, so the completed current-root
snapshot is the whole per-move record. `PathTelemetry` accumulates until
`init`, with per-result fields on each `FinalResult`.

| Measurement | Reset point | Collection rule |
|---|---|---|
| Search counters (`search_stats()`, including cache fields read live from `EvalCache` at read time) | Successful `set_root` only: clean path and reroot alike | Read once after the run completes; attribute the whole snapshot to the current root. Never subtract a previous root snapshot. |
| New scoped component timers | Same reset points as search counters (specified with their implementation) | Same read rule as search counters |
| Path counters | `init` only (cumulative) | Per-result fields (`states_expanded`, `elapsed_nanos`) where available, otherwise before/after `finalize` delta for `calls`/`failures` |
| Memory reservation | Never reset (capacities are monotonic within a run) | Absolute reads; signed 64-bit deltas only for the live-footprint field with decreases allowed |
| Rejected roots and pre-search spawn death | No search ran; a rejected `set_root` may preserve the prior tree and counters | Report search counters as zero; never read the preserved snapshot as current work |

Reroot discards the retained subtree prior counters by design; reuse benefit
is measured in nodes and time, not carried counters.

### Telemetry disabling

Approved semantics:

- Instrumentation counters (`SearchStats` accumulators, `EvalCache`
  request/hit/miss/replacement counters, component timers, `PathTelemetry`
  accumulators) are disabled by the switch.
- Budget and deadline clock reads stay active; timed mode keeps enforcing
  its deadline. The outer wall-time measurement is identical in both modes,
  so the overhead gate keeps its denominator.
- Cache stamps, replacement choice, memo storage, and every search
  data-structure update are preserved exactly; only the counting increments
  are skipped.
- Fixed-iteration runs preserve semantic results (selections, boards,
  queue/hold state) with the switch on or off. Identical timed trajectories
  are not promised: different elapsed cost may legitimately change completed
  work under a deadline.
- Boundary wall-time fields (`total_s`, fields 3-7, fields 24-34) stay
  numeric in both modes. Component counters and scoped-timer fields report
  `na` when off, never zero and never claimed equal to enabled counts.
  Absolute memory fields stay numeric (they are reservation reads, not
  instrumentation).
- Finalization verification and essential correctness checks stay active in
  both modes. A verification failure still invalidates the run even when its
  counter reads `na`.

The switch covers `EvalCache` own telemetry together with `SearchStats`:
`search_stats()` replaces its cache fields from the live cache counters, so
both counter sets gate together while cache policy stays untouched. No
search-state or cache-policy change is authorized.

### Scoped component timers (Section 17.2)

Required binding-level timers, nested and non-overlapping, on the same
monotonic clock family: budget and deadline reads use the engine
`clock_nanos`, while component and boundary timer spans read the engine
`timer_nanos` (same steady-clock default; separable so injected test clocks
stay deterministic): enumeration body per search invocation; rule
application
plus dedup per unique semantic candidate; board-evaluation hit path and miss
path separately; policy transition; tree-node materialization;
selected-path find; production replay. Parent expansion and widening
iterations are aggregate diagnostics. The parent-expansion span intentionally
nests the binding component spans (it wraps expansion plus materialization);
it is reported as an overlapping aggregate diagnostic, never as a
non-overlapping binding rate. Every timer/count pair ships with a
defined comparable scope (decision 2 prerequisite); scopes are documented
with the comparator, not assumed. Timer overhead is gated by the on/off
protocol, which for timers compares cost, not identical trajectories.

## 5. Telemetry schema: PROFILE_V3

V1 and V2 records are byte-preserved; the legacy binary emits V1/V2 only and
never V3. This versioning amendment is explicit: value-only fields do not
retroactively supply baseline measurements (Section 7). V3 begins with the
token `PROFILE_V3`, uses space-separated `key=value` fields with no embedded
spaces, and allows the literal `na` for unavailable measurements (telemetry
disabled).

### Core fields

| # | Key | Unit | Source and scope |
|---|---|---|---|
| 1 | `moves` | count | Measured moves |
| 2 | `total_s` | s | Measured-loop wall; numeric with telemetry on or off |
| 3-7 | `min_ms` `median_ms` `p95_ms` `p99_ms` `max_ms` | ms | `T_ROOTSEARCH` per-move distribution (legacy-scope comparison basis); numeric in both modes |
| 8 | `evals` | count | Current-root `eval_requests` snapshots summed; `na` when off |
| 9 | `transitions` | count | Current-root `policy_transitions` snapshots summed; `na` when off |
| 10 | `searches` | count | Current-root `enumeration_calls` snapshots summed; `na` when off |
| 11 | `dead_moves` | count | Measured-only ordinary deaths; invalidating failures excluded, never scored here |
| 12 | `games` | count | Measured-only ordinary restarts |
| 13 | `node_live_delta_bytes` | bytes, signed | Net live-node change: signed sum of per-move changes in `arena_size() * sizeof(Node)` via the public API. Not allocation, not materialization work, not pool growth; reroot compaction makes it negative by design. |
| 14-16 | `evals_per_s` `transitions_per_s` `searches_per_s` | 1/s | Field 8/9/10 divided by field 2; `na` when off |
| 17-22 | `warmup_moves` `seed` `iters` `maxdepth` `budget_ms` `mode` | mixed | Same meanings as V2 |
| 23 | `telemetry` | token | `on`, or `off` meaning instrumentation disabled with boundary timers still numeric |

### Appended component fields (fixed order after `telemetry`)

| # | Key | Unit | Source |
|---|---|---|---|
| 24-28 | `emove_min_ms` `emove_med_ms` `emove_p95_ms` `emove_p99_ms` `emove_max_ms` | ms | `T_MOVE` end-to-end per-move distribution, where the profile end-to-end move span is `T_SETUP + T_ROOTSEARCH + T_PATH + T_APPLY`, including scenario refill and hold/queue bookkeeping (diagnostic; scope differs from fields 3-7 by construction); numeric in both modes |
| 29 | `setup_ms` | ms | `T_SETUP` total; numeric in both modes |
| 30 | `setup_eval_ms` | ms | Root `evaluate` subtotal inside setup; numeric in both modes |
| 31 | `run_ms` | ms | `T_RUN` total (run-only diagnostic); numeric in both modes |
| 32 | `path_ms` | ms | `T_PATH` total; numeric in both modes |
| 33 | `apply_ms` | ms | `T_APPLY` total; numeric in both modes |
| 34 | `init_ms` | ms | `T_INIT` (excluded from totals); numeric in both modes |
| 35 | `parents` | count | `expanded_parents` snapshots summed; `na` when off |
| 36 | `parent_ns` | ns | Parent-expansion timer (aggregate diagnostic); `na` when off |
| 37 | `widening_iters` | count | `widening_passes` snapshots summed; `na` when off |
| 38 | `enum_ns` | ns | Enumeration body timer; `na` when off |
| 39 | `raw_landings` | count | `raw_kernel_landings` snapshots summed; `na` when off |
| 40 | `unique_candidates` | count | `unique_candidates` snapshots summed; `na` when off |
| 41 | `rule_transitions` | count | `rule_applications` snapshots summed; `na` when off |
| 42 | `rule_ns` | ns | Rule application plus dedup timer; `na` when off |
| 43 | `eval_hit_ns` | ns | Evaluation hit-path timer; `na` when off |
| 44 | `eval_miss_ns` | ns | Evaluation miss-path timer; `na` when off |
| 45 | `eval_memo_hits` | count | `eval_memo_hits` snapshots summed; `na` when off |
| 46 | `eval_computed` | count | `eval_computed` snapshots summed; `na` when off |
| 47 | `cache_requests` | count | Live `EvalCache` requests at snapshot read, summed; `na` when off |
| 48 | `cache_hits` | count | Live `EvalCache` hits at snapshot read, summed; `na` when off |
| 49 | `cache_misses` | count | Live `EvalCache` misses at snapshot read, summed; `na` when off |
| 50 | `cache_replacements` | count | Live `EvalCache` replacements at snapshot read, summed; `na` when off |
| 51 | `materialized_nodes` | count | `materialized_nodes` snapshots summed; `na` when off |
| 52 | `materialize_ns` | ns | Materialization timer; `na` when off |
| 53 | `policy_ns` | ns | Policy-transition timer; `na` when off |
| 54 | `transposition_merges` | count | `transposition_merges` snapshots summed; `na` when off |
| 55 | `promotions_refused` | count | `promotions_refused` snapshots summed; `na` when off |
| 56 | `pending_end_max` | count | Maximum across end-of-search `pending_occupancy` snapshots over measured moves. Limited sampling scope by construction: not the peak pending occupancy reached mid-search. Never summed; `na` when off. |
| 57 | `texhaust_moves` | count | Measured moves with `transposition_exhausted` set. Flag count, not a work metric; `na` when off. |
| 58 | `path_calls` | count | Path telemetry `calls` measured delta; `na` when off |
| 59 | `path_states` | count | Path telemetry `states_expanded` measured delta; `na` when off |
| 60 | `path_find_ns` | ns | Finder construction plus extraction timer; `na` when off |
| 61 | `path_replay_ns` | ns | Production verification replay timer; `na` when off |
| 62 | `replay_failures` | count | Path telemetry `failures` measured delta; any nonzero invalidates the run; `na` when off (verification still invalidates) |
| 63 | `mem_retained_bytes` | bytes | Absolute `retained_bytes()` at end of run, checked directly against the 256 MiB budget; numeric in both modes |
| 64 | `arena_reserved_bytes` | bytes | Absolute `Engine::arena_reserved_bytes()` at end of run (arena storage only); numeric in both modes |
| 65 | `idmap_reserved_bytes` | bytes | Absolute `Engine::idmap_reserved_bytes()` at end of run, split out so field 64 matches the engine API name; numeric in both modes |
| 66 | `raw_unique_ratio_x1000` | 1/1000 | `1000 * raw_landings / max(1, unique_candidates)`; `na` when off |

Binding component rates are derived as timer field divided by count field
(Section 17.2: per enumeration call, per unique candidate, per eval request
split by hit/miss, per policy transition, per materialized node, per
selected-path state). Gauges and flags (fields 56-57) are never summed as
work. `retained_bytes()` already includes the accounted buffers, queue
allowance, stack allowance, and frontier metadata, so gate 8 checks field 63
against the budget directly with only genuinely additional storage (none
currently) accounted separately.

## 6. Implementation gates (next slice, testable)

1. Scenario identity: scripted event schedules (holds, deaths, consumption
   counts) produce identical piece streams between the value generator and
   the legacy algorithm; hold on/off, occupied and empty hold, queue
   exhaustion after death, and the warmup boundary are covered.
2. Deterministic value repeats in fixed-work mode: identical semantic
   selections, resulting boards, queue/hold state across two runs with the
   same seed.
3. Hold-disabled (approved root-only meaning), empty-hold swap,
   occupied-hold swap, restart-after-death, and first-measured-move cases
   each produce legal deterministic trajectories with hold staying empty in
   the disabled case and zero `replay_failures` everywhere.
4. Any finalization failure or rejected root in any move including warmup
   invalidates the run; accepted runs show zero `replay_failures`.
5. V1 byte preservation on the legacy binary; V2 still parses; V3 field
   order, token, and `na` handling asserted by a parser test, including an
   off-mode row with numeric boundary fields and `na` component fields.
6. Counter lifecycle and evaluation identities: current-root snapshots
   attributed whole, never differenced across roots; rejected roots and
   pre-search deaths report zero search counters; path deltas sum to
   reported totals; gauge/flag rules asserted; the Section 3 aggregation
   identities hold in both cache layouts.
7. Telemetry on/off in fixed-iteration mode: identical selections, boards,
   and queue/hold state; component fields read `na` when off while boundary
   timers stay numeric; timed mode asserts only continued deadline
   enforcement and active verification, not identical trajectories.
8. Memory: absolute `retained_bytes()` stays within the 256 MiB budget on
   the gate workload; arena/idmap reservation split reported; no unsigned
   underflow in any delta path.
9. Value artifact, supplemental comparator, and test-only legacy driver build
   under GCC and Clang self-release; debug and sanitizer builds run the
   scenario, determinism, schema, counter, and parity integration tests.

Move-sequence equality between legacy and value is never required: the
migration intentionally changes legal candidate sets. Divergences are
classified through the count partition (Section 17.3 item 3), not forced
through a compatibility path.

## 7. Baseline evidence mapping

### Which gates use the immutable baseline

- Total-time median ratio at most 1.02 (Section 17.3 item 1): value `total_s`
  against frozen baseline `total_s`, same command, paired protocol.
- Per-move p95 median ratio at most 1.02 (item 2): value field 5
  (`T_ROOTSEARCH` p95) against baseline V2 `p95_ms`, which shares the
  root-update-plus-search scope.
- Frozen V2 work totals (`evals`, `transitions`, `searches`) serve as
  diagnostics and as cross-checks on comparator parity. They supply no
  binding component denominator.
- Frozen V2 rates serve as timed-throughput diagnostics only.

### Which gates require the supplemental instrumented comparator

- Every binding component rate (item 4: per enumeration call, per unique
  candidate, per eval request split by hit/miss, per policy transition, per
  materialized node, per selected-path state).
- The unique-candidate count partition (item 3 denominator: V2 has no unique
  candidate count).
- Binding timed throughput (item 6: completed widening iterations, expanded
  parents, retained transitions per second; V2 reports none of the three).
- Legacy selected-path cost evidence (item 5 context and per-state
  comparison input).

### Measurements unavailable today

- All legacy component timings (no scoped timers exist in the frozen binary).
- Legacy evaluation request/hit split (the frozen binary counts evaluator
  calls only).
- Legacy widening-iteration, expanded-parent, and retained-transition counts.
- Legacy unique-candidate counts and selected-path state counts.

### Retained transitions, defined on both engines

Accepted expansion transitions before global merge and materialization, not
nodes retained in the arena. Value child-buffer entries may subsequently
merge or fail to materialize when storage is exhausted, and those outcomes
already have separate counters.

- Value: successful `Child` insertion after same-result deduplication,
  matching the existing `policy_transitions` increment site. This definition
  is approved as stated; redefining the metric as post-merge materialized
  nodes is rejected.
- Legacy: completed search policy transitions at the established `Core::get`
  site.

Executed policy calls and accepted child transitions stay distinct wherever
instrumentation exposes a path on which they differ, and exhaustion fixtures
verify that distinction rather than assuming equality. Remaining scope
differences between the two sites enter the item 3 classification;
definitional equality is not assumed.

### Legacy comparator instrumentation sites

- Evaluation: requests at `Core::eval` entry, hits at the depth-table hit
  branch, evaluator calls at the actual policy call (must reproduce the
  frozen `evals` total under parity). Legacy table key semantics are
  recorded as-is for classification; never treated as equivalent to value
  exact-occupancy verification and never altered.
- Transitions: `Core::get` completions (must reproduce the frozen `gets`
  total under parity).
- Enumeration: `Search::search` invocations (must reproduce frozen
  `searches`); unique semantic candidates by normalizing returned land
  points with the existing migration normalization and geometry facilities,
  never a new identity function. Raw returned land points, unique normalized
  semantic candidates, children surviving result-level dedup, and
  materialized nodes are reported as four distinct counts. Normalization
  compares within the same board, played piece, and movement configuration;
  normalizes equivalent geometry consistently; preserves the T
  arrival/spin distinctions supported by the existing migration oracles;
  never collapses current and hold branches across search invocations; and
  classifies candidates that cannot be matched instead of discarding them
  silently. The normalization algorithm and its directed fixtures are
  reviewed with the comparator implementation before its counts become
  binding denominators.
- Search control: outer anytime-loop iterations (widening), parent expansion
  at the tree-node expansion site, scoped timers with the same
  nesting/non-overlap rule as the value side.
- Path: duration around the existing `make_path` production call plus a
  path-state denominator counted as valid discovered states admitted to
  traversal or accepted as the goal, including the start and any accepted
  early-exit goal. Successful `node_mark_.set` insertions alone are not the
  denominator, because some call sites mark before `check(map)` and a
  successful mark can name a rejected position. The value per-state rate uses
  finder queue-tail states; the two denominators measure different algorithms
  with differing early-exit and full-traversal behavior, so the comparison is
  reported with both scopes stated, never as a same-scope claim. Legacy
  legality checks and marking order stay untouched: observational
  instrumentation only.

### Comparator qualification before it supplies denominators

1. Scope definitions: every timer/count pair ships with a defined comparable
   scope on both sides.
2. Fixed-iteration parity covers the selected move, resulting board,
   hold/queue progression, and the original work totals (`evals`,
   `transitions`, `searches` must reproduce the frozen values). Trajectory
   parity is observed through the test-only legacy move driver, which shares
   the frozen engine sources and establishes the relationship by source
   identity plus work-count identity, since immutable profile rows expose no
   selections.
3. Timed comparisons never require identical trajectories; agreement is
   characterized statistically.
4. Overhead evidence is reported per component where the component exists in
   both armed and disarmed modes; components too small for a stable ratio
   report absolute timing-overhead evidence with sufficient repetitions or
   batched observations to establish resolution, plus recorded counts and
   timer scope. A global overhead result never certifies a single component,
   and no binding pass claim survives measurement uncertainty that can flip
   the verdict at the 2 percent margin. The whole-run 0.5 percent
   telemetry-overhead gate remains required alongside per-component
   evidence; neither replaces the other. No global percentage is subtracted
   from any component timing: measured rates and overhead evidence are
   reported separately, and the word overhead-adjusted appears nowhere in
   the gate table.

### Numerator and denominator sourcing per required gate

| Gate | Numerator (candidate) | Denominator (baseline side) |
|---|---|---|
| Total time | Value `total_s` | Immutable baseline `total_s` |
| p95 | Value field 5 | Immutable baseline V2 `p95_ms` |
| Component rates | Value timer/count field pairs | Comparator timer/count field pairs with per-pair scopes; overhead evidence reported alongside, never subtracted |
| Path overhead cap | Value `path_ms` over value full move time (`T_MOVE + T_APPLY`) | None required: Section 17.3 item 5 caps the value implementation own cost share. Comparator `make_path` time and path states are component-comparison inputs, not substitutes for the cap. |
| Timed throughput | Value completed work per second (widening iterations, expanded parents, retained transitions) | Comparator completed work per second for the same three metrics; frozen V2 rates diagnostic only |
| Telemetry overhead | Value off vs on total time, same binary, same order | Same binary, same order (no baseline side); below 0.5 percent |

If any comparator parity check fails, or any required numerator or
denominator remains unavailable, that gate is blocked. Blocked gates are
reported as blockers, never bypassed by omitting the metric.

## 8. Review outcomes recorded at 7.1A close

This section records binding verdicts; it adds no new requirements.

1. Legacy land-point normalization: approved with the explicit identity
   contract in Section 7 (existing migration facilities, four distinct
   counts, branch separation, classification of unmatched candidates).
   Normalization algorithm and directed fixtures are reviewed with the
   comparator implementation before binding use.
2. Retained-transition sites: approved as accepted pre-merge transitions
   (value `Child` insertion site, legacy `Core::get` site); post-merge
   redefinition rejected; executed-vs-accepted distinction verified by
   exhaustion fixtures.
3. Tiny-component overhead: absolute evidence acceptable under the stated
   conditions; whole-run 0.5 percent gate still required; uncertainty that
   can flip a 2 percent verdict blocks any pass claim.
4. Refill timing: `T_MOVE + T_APPLY` includes scenario refill per the
   corrected denominator prose; profile full-move measurement, not a DLL
   boundary claim.
5. Legacy path states: valid discovered-state count (start and accepted
   early-exit goal included), not raw successful marks; legacy check/mark
   order untouched.

## 9. Items deferred to the comparator slice

No open contract choices remain from 7.1A. The following review checkpoints
arrive with the supplemental comparator implementation: the normalization
algorithm with directed fixtures before its counts become binding
denominators, per-pair timer/count scopes on both sides, and the overhead
evidence package (global 0.5 percent gate plus per-component absolute
evidence).
