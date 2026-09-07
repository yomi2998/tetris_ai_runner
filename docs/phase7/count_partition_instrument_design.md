# Gate-3 count-partition instrument: accepted design (2026-09-07)

Status: design accepted by the coordinator; implementation authorized as
test-only code (no production behavior change). Note: the protocol's
reference to a "pre-authorized cross-invocation distinct-state
instrument" (recampaign_verdicts.md gate 3) had no written authorization
behind it; this document is the authorization record. All line
references anchor to commit `96c1366`.

## 0. What the two counters actually count (established fact)

### Value side (`src/tetris_engine.cpp`, `Engine::expand_source`)

Per `(parent, played piece, BranchSource)`:

1. `can_spawn` gate: if spawn blocked, zero counts.
2. `enumerate_candidates_into` -> `batch`. Counter `unique_candidates +=
   batch->count` — post-kernel-canonicalization (sorted-cell dedup +
   T-arrival collapse inside `src/toj_rule.h`, key = occupied cells +
   arrival, non-T arrival forced `Normal`), pre-rule-apply.
   `raw_landings` counted separately.
3. Per candidate: `++rule_applications`, then `toj::apply` (`is_landing`
   + mask + clear + `classify_spin` + `lockout = lowest >= 20`). Apply
   failures count in `rule_applications` and produce nothing further.
4. Intra-source result dedup: skip if an existing child in `out` has
   same source, same resulting board, same outcome (spin, clear,
   lockout).
5. `evaluate_once` + policy transition -> push `Child`,
   `++policy_transitions`. `child.expandable = !lockout`.

`expand_parent` clears `eval_memo_`/`child_buffer_` per parent and calls
`expand_source` up to twice (Current + Hold). Each materialized node is
expanded at most once per move; transposition merge/fail-stop lives in
`search_materialize_inner`/`transposition_probe`.

### Legacy comparator side (`src/tetris_profile_legacy_cmp.cpp`)

- `CmpSearch::search`: `calls++`, `raw_landings += result->size()` (raw
  land points, duplicates included). Normalization per call
  (`dedup.begin_call()`) — Current and Hold branches never merge,
  matching the value per-`expand_source` scope. Counters-only mode skips
  normalization, so gate-3 comparator rows must be full-timer twins.
- Normalization (`src/legacy_cmp_normalize.h`): matched key = (sorted
  occupied cells, channel) where channel = `spin_class*2 + last_rotate`
  for T (6 values), 0 for non-T; O rotation collapsed. Unconvertible
  land points -> `matched=false`, `opaque=status_bits`, counted in
  `unmatched_candidates` and included in `unique_candidates` under
  salted-FNV opaque identity.
- `CmpTOJ::get`: `transitions++` per policy-state computation. Legacy
  children are built per land point; there is no result-board/outcome
  dedup: two land points yielding the same board are two children, two
  evals, two transitions. Cross-node structural merging does not exist
  (the per-depth `tt` table caches evaluations only). `build_children`
  is version-gated (once per node per move), so re-walk means every
  transposition-equivalent board in a distinct tree node is fully
  re-expanded.
- Legacy searches start from the spawn-hook suggestion
  `Core::spawn_node`, which may differ from `generate(t)`. Value always
  starts from canonical spawn `(4,20,0)`. This is a real,
  fixture-recordable semantic difference (class (c2) below).
- Legacy death/lockout uses the bounding-row rule (`target->row >= 20`);
  value death uses `lowest_occupied_row >= 20`.

Recorded 7.2E seed-1 numbers: uniques 62,283,081 vs 55,838,330 (delta
+6,444,751, +11.542%); transitions 53,598,323 vs 64,334,998 (delta
-10,736,675, -16.689%). Residual truncation 174-190/200 moves fail-stop;
timed demand peaks 22,084 states (~12x headroom); fixed-work true demand
peaks 448,536.

## 1. Partition identity

A raw per-run count delta confounds four terms: (i) different parent
state sets visited (trajectories diverge; value's set is
truncation-cut), (ii) different per-parent candidate sets (the actual
semantics), (iii) different dedup/merging, (iv) different counting
normalizations. The instrument separates all four.

Comparison unit: the distinct enumeration input `e = (occupancy,
piece)`. Candidate enumeration on both sides is a pure function of board
occupancy + piece + movement flags. Source branch (Current/Hold) is kept
as a label, never merged, on both sides.

Shared semantic identity `K(c) = (sorted occupied cells, arrival class)`
with arrival = `TerminalRotation/Normal` for T, `Normal` collapsed for
non-T. Both sides' sets are projected onto `K` before comparison. The
legacy spin_channel and opaque keys are not semantic identity; their gap
to `K` is measured explicitly (normalization-gap sub-table).

Let `E_V`, `E_L` be the multisets of enumeration inputs expanded by each
engine on the same workload, `D_V`/`D_L` their distinct sets,
`S = D_V ∩ D_L` (occupancy compared on rows 0-39; inputs with rows 40-47
occupied are out-of-legacy-domain and counted separately, never silently
dropped).

Uniques leg. For `e ∈ S`: `V(e)`, `L_sem(e)` = legacy land points
projected onto `K`. Per-input delta `δ(e) = |V(e)| - |L_sem(e)|`. The
campaign uniques delta decomposes as:

```
Δ_unique = Σ_{e∈S} δ(e)  +  Σ_{e∈D_V∖S} |V(e)|  -  Σ_{e∈D_L∖S} |L_norm(e)|
           + [normalization gap on S: Σ|L_norm(e)| - Σ|L_sem(e)|]
```

The first sum partitions into classes (a)-(d). The second/third sums are
the volume term (reported with truncation split). The bracketed term is
the normalization gap (spin-split + opaque inflation, legacy-side
provable).

Transitions leg (separate): same shape, but per-input child sets are
`C_V(e)` (post-apply, post-intra-source-dedup, per source) vs `C_L(e)`
(per land point via attach+legacy classify, per source branch), joined
on `(result-rows-hash, clear, spin, lockout, source)`.

The tool asserts exact integer accounting (`a+b+c+d == δ` per input and
in total) and exits nonzero otherwise. That assertion is the gate-3
evidence artifact.

## 2. Classification predicates and fixture corpus

All predicates operate per shared input `e`, on sets already projected
onto `K`. Fixture-identified means each predicate has a named fixture
file plus a unit test that forces the class.

- (a) New-legal `P_new(c)`: `c ∈ V(e) \ L_sem(e)` AND all three hold:
  (1) `toj::apply` succeeds on `e`'s board; (2) scalar-oracle reachable
  (`scalar_arrival::ScalarOracle::landable_words` bit set for
  `(cells, channel)`); (3) legacy-command reachable through the legacy
  create/check/kick tables from `(3,21,0)`. Predicates (2)+(3) exist
  verbatim in the `run_candidate_tests` new-only block of
  `tests/rule_differential.cpp`; the instrument reuses that code. If (2)
  or (3) fails, the candidate is not class (a) — it falls to (d), never
  to (a)-by-assertion.
- (b) Removed semantic duplicates `P_dup`: groups of >= 2 legacy land
  points in one call with identical `K` but distinct raw identity. Two
  sub-counts: `b_counted` — groups split by spin_channel or opaque
  identity that inflate legacy uniques (enter the normalization gap);
  `b_collapsed` — groups already collapsed by `ProbeDedup` (explains the
  raw-landings delta only, informational, excluded from the uniques
  partition sum). Both provable from the legacy side alone. Fixtures to
  pin: I/S/Z same-cells multi-rotation landings; O non-zero rotations in
  the opaque class; any T same-(cells, arrival) dual land point.
  Labeled speculation: `b_counted` is believed small (same
  `(cells, arrival)` almost surely implies same spin classification
  since corners/clear are functions of cells+board), but that must be
  measured — the normalization-gap table settles it.
- (c) Lockout-semantic changes: (c1) `lowest_occupied_row >= 20` XOR
  legacy `node->row >= 20` (anchor/bounding-vs-mino disagreement);
  (c2) spawn asymmetry — `toj::can_spawn` false on value side while
  legacy generate/check admits the start, or recorded legacy start pose
  != canonical `(4,20,0)` (spawn-hook suggestion divergence), filed
  under (c) as a deliberate canonical-spawn replacement per plan 12.3,
  with separate CSV sub-columns so a strict reader can re-file it as
  defect without rerunning; (c3) out-of-legacy-domain inputs (rows 40-47
  occupied: value enumerates, legacy's 10x40 map cannot represent) —
  counted in the volume term, labeled, share reported. Fixtures: the
  directed lockout block in `tests/rule_differential.cpp` plus the
  plan-9.3 orientation corpus.
- (d) Defects: everything else, split into `d_V_only` (value candidate
  unexplained: failed replay, outcome mismatch on shared `K` such as
  clear-count disagreement vs legacy attach, spin mismatch vs legacy
  classify) and `d_L_only` (legacy land point with no value counterpart
  and no (b)/(c) explanation). Both must be zero. Any nonzero row
  invalidates the campaign and becomes a new fixture. Outcome comparison
  reuses the shared-case checks in `run_candidate_tests`.

## 3. Truncation handling (recommendation with evidence)

Primary partition corpus = timed 20 ms workload; secondary = fixed-work
restricted to pre-exhaustion widening passes.

Evidence: fixed-work value runs fail-stop on 174-190/200 moves, so the
fixed-work parent set is truncation-cut and confounds semantics with
table capacity; timed demand peaks at 22,084 distinct states against
262,144 entries (~12x headroom, zero exhaustion), so timed runs on both
engines are budget-complete and volume-comparable. Concretely: (i) run
the partition instrument on the timed seed-1 workload (both engines,
same scenario stream as the campaign) for the full 100-percent-accounted
class table — the mechanism proof; (ii) for the binding fixed-work
integers, emit per-widening-pass deltas `(pass, side, uniques_cumul,
transitions_cumul, exhausted_flag, pending_occupancy)` per move
(`pending_occupancy` refresh already runs on early-stop paths but is not
emitted per move by `src/profile_value_runner.h`), verify class shares
measured pre-exhaustion match the timed shares within a predeclared
tolerance, and attribute the post-exhaustion fixed-work residual to the
volume term explicitly. Pure pre-exhaustion restriction is rejected as
primary (discards ~90 percent of moves); per-pass deltas alone cannot
separate classes.

## 4. Transitions leg (required, separate)

The -16.7 percent transitions delta cannot be derived from the candidate
partition: transitions are counted after value's intra-source result
dedup and after transposition merging, while legacy counts one
transition per land-point child with no equivalent dedup and no
cross-node merging. Decompose:

```
Δ_trans = Σ_{e∈S} (|C_V(e)| - |C_L(e)|)   [per-parent child-set delta -> classes (a)-(d)]
        + Σ_{V-only parents} |C_V| - Σ_{L-only parents} |C_L|   [volume term]
```

with the value-side merge count (`transposition_merges`, ~7,400/move
recorded — the instrument must re-emit it) reconciling expansions: value
downstream transitions are suppressed exactly where merges absorbed
equivalent boards, while legacy re-expands each equivalent node. Leg 2
captures per-parent child sets on the value side (post-dedup child
contents: board hash, outcome, source, expandable flag) and legacy
counterparts (per land point: attach resulting-rows hash + clear +
classify spin + legacy lockout + branch). Class mapping: children from
(a)-candidates -> (a); legacy children sharing (board, outcome) within a
source that value dedups -> (b); expandable/dead disagreement -> (c);
remainder -> (d), must be zero. Cost is marginal once leg 1 exists.

## 5. Implementation plan

New test-only target `candidate_partition` (no production code path
changes; zero release impact):

1. Recorder (value side): explicit test-only record callback invoked in
   `Engine::expand_source` after a successful `enumerate_candidates_into`:
   dumps `(occupancy words, played piece, source, candidate list with
   packed placement+arrival, per-candidate apply outcome + intra-source
   dedup survivor flag + lockout)`. Default-null `std::function`; when
   null the compiled cost is one branch. Also emit per-move `(pass,
   exhausted, pending_occupancy, transposition_used)` — requires per-move
   emission in `src/profile_value_runner.h` (fields exist in
   `SearchStats`). Stream to disk; maintain a distinct-input hash set
   with deterministic cap and exact totals.
2. Legacy driver: long-lived flag-off legacy engine (frozen comparator
   config, same as `tests/legacy_corpus_bench.cpp`), one process. Per
   recorded distinct input with rows 40-47 empty: rebuild `TetrisMap` +
   `rebuild_metadata` (the `drive_case` pattern), record actual spawn
   status used (drive from `generate(piece)` and separately record the
   `spawn_node` suggestion to feed (c2)), `search(map, node, 1)`,
   normalize both ways, `attach`+`classify` per land point for leg 2.
   Never fresh-engines-per-input (the 7.1C sequential-fresh-engine
   divergence limit forbids in-process fresh-engine comparison).
   Out-of-domain inputs are counted, not driven.
3. Classifier (offline): join on `(occ40-hash, piece)`; apply predicates
   reusing the `tests/rule_differential.cpp` oracles (LegacyReplay,
   scalar oracle, clear/spin agreement); emit CSV rows `input_id,
   board_hash40, piece, nV, nL_norm, nL_sem, a, b_counted, b_collapsed,
   c1, c2, c3, dV, dL, outcome_mismatches...` plus the transitions-leg
   row; final line asserts the class integers sum to the delta per leg
   and exits nonzero on any remainder.
4. Campaign slot: gate-3 evidence bundle = timed-seed-1 full table
   (primary) + fixed-work pre-exhaustion share check + seeds 2/3
   diagnostic tables. Verdict rule unchanged: any unclassified remainder
   invalidates.
5. Cost estimate: distinct timed inputs order 10^5; one legacy
   `search(map,node,1)` per input at single-parent cost ->
   minutes-to-tens-of-minutes per seed, dominated by leg-2 attach calls.
   Value recording run = one extra profile run. No production impact.
6. Classifier unit tests (hand-built, no engine): I-r0/r2 same-cells dual
   landing -> `b_collapsed`; synthetic spin-split pair -> `b_counted`;
   O-r1 opaque land point -> `b_counted`; anchor-vs-mino disagreement ->
   `c1`; blocked-spawn asymmetric input -> `c2`; high-row input ->
   `c3`/out-of-domain; replay-failing synthetic value candidate -> `dV`
   nonzero -> tool exit 1; clear-count-mismatched shared-K pair -> `dV`;
   exact-accounting test on a mixed fixture.

## 6. Honest limits

1. Per-parent semantics only, not quality: nothing about move quality,
   policy correctness, or WR/APP/APL.
2. Coverage is value-visited-states only: legacy-only subtrees enter
   only as aggregate volume, never classified per-parent. The
   `D_L \ S` term is bounded, not explained.
3. Common-mode rule data: replay proofs share the same SRS/kick tables;
   mitigation is the independent scalar SRS interpreter plus
   hand-authored fixtures per plan 11.5, plus the gates 8/9 comparators.
4. Low-40-row restriction: high-board differences above row 39 are
   counted in (c3), not compared.
5. Arrival-channel mapping assumption: legacy `is_last_rotate ->
   TerminalRotation` equivalence rests on existing rule_differential
   class-parity checks, not on this instrument.
6. Merge correctness out of scope: the volume term attributes suppressed
   downstream transitions to transposition merging without proving
   merge-key correctness; that proof belongs to engine unit tests, and
   any merge bug surfaces here only as (d).
7. Authorization provenance: the "pre-authorized" language had no written
   basis; this document is the authorization record (coordinator-endorsed
   2026-09-07, implementation limited to test-only code).

Bottom line: build the three-pass test-only instrument; lead with the
timed-20 ms corpus; assert exact integer accounting per leg; file
spawn-suggestion divergence under (c) as an explicitly labeled
sub-column. If any (d) row is nonzero, the campaign stays invalid — and
the instrument names exactly which placement, on which board, and why.
