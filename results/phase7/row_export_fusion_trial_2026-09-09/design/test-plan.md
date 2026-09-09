# Evaluate-and-safe row export fusion trial test plan

Companion to `design.md` in this directory. The trial macro is `TETRIS_ROW_FUSION_TRIAL`,
the trial profile binary is `tetris_profile_row_fusion`, the trial unit suite is
`row_fusion_trial_tests`, and the trial partition recorder is
`candidate_partition_row_fusion`.

## 0. Ground rules

1. Suite conventions follow `tests/direct_key_trial_tests.cpp`: a file-level
   `#ifndef TETRIS_ROW_FUSION_TRIAL` / `#error` guard, a `check(bool, std::string const &)`
   helper incrementing `checks` and `failures`, grouped `run_*_tests()` functions called
   from `main`, and a final
   `std::println("row_fusion_trial_tests: {} checks, {} failures", checks, failures)` with a
   nonzero exit on any failure.
2. Every check message is prefixed `row fusion` so a failure names its owner.
3. All corpora are deterministic. Randomization uses one fixed SplitMix64 stream per
   section with published seed constants, never wall time or addresses.
4. Nothing in this plan may add a field to `PROFILE_V3`. Instrumentation reaches tests only
   through trial-only accessors, because the count gate compares every non-timing field
   between binaries.
5. Minimum suite size is 30000 checks. Directed sections run to fixed iteration counts, not
   to an early break, so the total is reproducible across compilers.
6. Every section must pass under GCC debug, GCC self-release, Clang self-release, and the
   sanitizer build (`out/build/san`) before the count gate is frozen.

## 1. Trial-only export instrumentation (required proof)

Design section 7 does not specify how "exactly one export per computed board" is proven. This
section defines the instrumentation that proves it, and the identities the suite asserts.

Counters, all `#ifdef TETRIS_ROW_FUSION_TRIAL` and compiled out otherwise, held as inline
file-scope counters in `src/row_fusion_trial.h` with an accessor and an explicit reset so
tests can snapshot around a single `expand_source` call:

| Counter | Incremented |
|---|---|
| `rf_eval_exports` | once per 40 row stack export inside `Policy::evaluate` |
| `rf_legacy_exports` | once per export inside the `transition_known_lockout` safe block, counting both the 21 row and the 40 row sites |
| `rf_lockout_skips` | once per safe block that takes the `safe = -1` branch without exporting |
| `rf_row_reads` | once per `result.row(y)` call at either export site, so the total is rows, not calls |
| `rf_fused_children` | once per `transition_known_lockout` call that consumed a non-null supplied safe |
| `rf_legacy_children` | once per `transition_known_lockout` call that received a null supplied safe |

Required identities. Each is a separate `check`, and each is asserted both per directed
child and cumulatively after a full search:

1. `rf_eval_exports == eval_computed`. Exactly one export per computed board, never two,
   never zero.
2. `rf_fused_children + rf_legacy_children == policy_transitions`. Every transition is
   accounted for on exactly one path.
3. Per directed computed child with telemetry on and a single `expand_source` call, total
   export events `rf_eval_exports + rf_legacy_exports` is exactly 1. The unfused reference
   for the same child, obtained by calling `transition_known_lockout` with a null supplied
   safe, yields exactly 2 for a next-present child (`rf_row_reads == 40 + 21 == 61`), 2 for
   an empty-queue child (`rf_row_reads == 80`), and 1 for a lockout child (`rf_row_reads == 40`).
4. `rf_legacy_exports == 0` over a run in which every transition is a memo miss, and
   `rf_legacy_exports <= rf_legacy_children` always, with equality when no memo-hit child is
   a lockout.
5. `rf_row_reads == 40 * eval_computed + 21 * hits_with_next + 40 * hits_empty_queue`,
   where the hit splits are taken from directed single-child cases and from the conservation
   identity `eval_computed + eval_memo_hits == policy_transitions`.
6. `rf_lockout_skips` equals the number of lockout memo-hit transitions, and a lockout child
   never increments `rf_row_reads` on the safe side.
7. Counters are pure observers: a directed test asserts the returned `Evaluation` and `State`
   and the full engine work vectors are identical with counters reset, incremented, or read,
   so instrumentation cannot perturb arithmetic.

The suite also records `rf_legacy_children == eval_memo_hits + early_return_transitions`
after a fixed search, which pins the memo-hit and stopped-search legs to the null-pointer
path required by design section 8 risk 2.

## 2. Bit-identical evaluation results

`run_evaluation_identity_tests`.

For each board in a corpus, call the one-argument `evaluate(board)` and the fused
`evaluate(board, &safe_in, &safe_out)`, then compare `Evaluation` field by field using
`std::bit_cast<std::uint64_t>` on `value`, and integer compare on `t2_value` and `t3_value`.
Bit compare, not `==`, because equality hides signed zeros and would pass a `NaN` mutation.

Corpora:

1. Directed structural boards: empty; single mino at rows 0, 1, 20, 21, 39, 40, 47; full
   rows 0 through 5; full rows 0 through 20; a board with rows 0 through 20 all `0x3ff` and
   rows 21 through 39 zero, which is the `spawn_frame_height` boundary; one occupied bit at
   each of rows 17 through 24 to bracket the `danger_limit` edge; boards with only the side
   columns 0 and 9 set, which drive `side_roof` below `roof`; a staircase from row 0 to row
   39; a perfect-clear board whose 40 rows clear exactly.
2. Seeded random boards at three densities, sparse, half, and packed, with 8000 boards per
   density from independent streams, each generated as three `uint64_t` occupancy words
   masked to the logical row space.
3. Engine-real boards: every `applied->board` produced by expanding 20 seeded queue schedules
   to depth 6, harvested through `expand()`, expected in the tens of thousands.
4. Signed-zero probes. The value expression begins `0. - side_roof * p.roof`, so zero-valued
   evaluations are reachable. Include boards that drive `value` to exactly zero with
   `side_roof == 0`, with `side_roof > 0` and all other terms zero, with negative
   accumulators canceling to zero, and with `parent.acc_value` or `parent.like` holding
   `-0.0`. Assert the fused and unfused bit patterns match, so the `-0.0` versus `0.0`
   sign cannot flip.
5. `NaN` sentinel. Copy the existing `assert(acc_value == acc_value ...)` idea into a check:
   after each fused call assert `out.acc_value == out.acc_value` and
   `out.like == out.like` and `out.value == out.value`, which fails loudly if the fused read
   ever consumes an uninitialized or overlaid row slot.
6. Parameter sweep: repeat a 200 board subcorpus over the published parameter sets used by
   `toj_policy_tests` including `config.safe` values 0, 5, and 16, so the fused branch is
   exercised against every arithmetic weighting.

## 3. Identical safe margins

`run_safe_margin_tests`.

Reference implementation of the unfused value, written locally in the test as a function that
exports its own clean rows exactly as `transition_known_lockout` does today, so the
comparison is not circular.

For all 7 pieces, both `has_next` states, both lockout states, and 2000 boards per
combination:

1. `fused_safe == reference_safe`, and `rf_row_reads` shows the fused path read only
   evaluate's already exported prefix.
2. Lockout: `fused_safe == -1` and the fused path performed no safe-side `row()` read, which
   proves the `-1` branch never touches rows.
3. Next present: `fused_safe` equals `scan_safe_rows` over the clean 21 row prefix for that
   next piece, over the full next-piece set, including pieces whose `danger_bits` reach rows
   20 and 21.
4. Empty queue: `fused_safe == spawn_frame_height - local_roof(rows)` and equals evaluate's
   own `roof`, asserted over boards with `roof` at 0, 1, 21, and 22 so the margin clamps at
   both ends.
5. Then feed each fused value into `transition_known_lockout` alongside a null-supplied call
   on the same inputs and assert the returned `State` matches field by field: `death`,
   `combo`, `under_attack`, `map_rise`, `b2b`, `t2_value`, `t3_value`, `acc_value` and
   `like` and `value` compared as bit patterns.

Downstream consumers are pinned by name so no clamp or offset is silently bypassed:
`safe -= next.map_rise`, the clamp to zero, `config_safe`, the `map_rise > safe` death test,
and `t2_safe_margin` and `t3_safe_margin`.

## 4. Queue boundary and hold variants

`run_boundary_and_hold_tests`.

1. `context.next` lengths 0, 1, 2, and full schedule, each crossed with next-first-piece I,
   O, T, S, Z, J, L.
2. Hold variants: empty hold, hold T, hold I, hold other piece, and hold locked, each
   crossed with `has_next` true and false, because the hoisted `next_piece` is per-source
   while hold state is per-child.
3. Cursor and boundary states: `ready` true and false, spawn obstruction, landings at
   lockout rows 20 and 21, and occupancy confined to rows 0 through 2 then 3 through 21,
   which brackets the `spawn_frame_height` minus `danger_limit` lower edge at row 3 and the
   `rows[0 .. 20]` scan ceiling, so any export indexing error shows up as a changed margin
   rather than a crash.
4. The `context.next.data() == policy_next.data()` assertion from design section 8 risk 3,
   exposed through a trial-only test hook and checked on every source of a 20 move scenario,
   so the per-source hoisting premise stays witnessed rather than assumed.
5. Existing `toj_policy_tests` tallies must still pass unchanged, including the next-present,
   next-single-T, next-absent, next-empty, and next-short coverage and the empty, T, I, and
   other hold states.

## 5. Overlay hazard witness

`run_overlay_hazard_tests`. This is the highest value test in the suite and it must not be
vacuous.

1. Build a board where `init_t_value` really calls `apply_overlay` at some index in
   `rows[0 .. 20]`, with the T placement candidate and arrival class that reach the overlay
   at lines 237, 243 to 246, 250 to 253, or 306.
2. Assert three values are pairwise distinct: the clean-rows reference margin, the margin
   obtained by rescanning the array after `init_t_value` has overlaid it, and the fused
   margin. The test fails unless the post-overlay value differs from the clean value, which
   is what makes the witness meaningful.
3. Assert the fused margin equals the clean-rows reference, never the post-overlay value.
4. Assert `rf_row_reads` attributes every fused read to the pre-overlay pass.
5. Repeat over all 7 next pieces and both arrival classes so the witness is not confined to
   one geometry.

## 6. Memo, cache, and early-return legs

`run_early_path_tests`.

1. Telemetry on, one repeated board: the second evaluation is a memo hit, so
   `rf_fused_children` does not increase for it while `rf_legacy_children` does, and the
   transition still produces the identical `State`.
2. Telemetry off: `eval_computed`, `eval_memo_hits`, and every exported counter stay at zero
   while results and `arena_size` are unchanged, matching the existing telemetry-off
   quietness checks.
3. Disabled cache branch, where the design cites the return at `src/tetris_engine.cpp:972`:
   supplied pointer null and the legacy export runs.
4. `search_stopped_` mid-expansion: no child reports a fused value it did not compute, and
   the returned `State` and stats match the unfused reference.
5. Depth zero root children and rehash and epoch reset paths, which never use the fused
   value, are exercised so the null-pointer fallback is covered where it actually occurs.

## 7. Policy transition and candidate order identity

`run_search_identity_tests`, plus the CTest and gate artifacts.

1. Within the trial binary: for a 20 move, depth 6, seeded scenario, record an ordered digest
   of every `expand()` result as `(Board occupancy words, Outcome, candidate, source)` and a
   second digest with the trial instrumentation read, reset, or unreached. Digests must be
   equal, which pins candidate order and first-survivor dedup against observer changes.
2. Pop order and selection: replay the accepted pattern from the direct-key suite, asserting
   `select_best()` matches across repeats, `finalize(spawn).path_ok`, and identical
   `arena_size`, `transposition_merges`, `materialized_nodes`, `expanded_parents`,
   `probe_steps`, and `probe_rebuilds` across three repeats.
3. Cross-binary order identity is proven by the partition recorders: normal
   `candidate_partition` and `candidate_partition_row_fusion` on seed 1, warmup 0, 20 moves,
   depth 6, 1000 iterations must produce byte-identical `.inputs.bin`, `.moves.tsv`, and
   `.run_totals.tsv`, which is the ordered first-observed expansion input, exact
   candidate and application and outcome and survivor records, repeated-input
   multiplicities, and per-move work vectors.
4. Profile equivalence CTest `row_fusion_profile_equivalence`: short workload with telemetry
   on and timers off, comparing every non-timing `PROFILE_V3` field with no mapped field
   exception. Any timing field may differ, nothing else may.
5. Both partition selftests pass, including the new `candidate_partition_row_fusion_selftest`
   registration alongside the existing recorders.

## 8. Build isolation and macro exclusivity

1. CTest `row_fusion_absent_from_normal_build`: `nm` on the normal binary reports zero
   `row_fusion` symbols and `--help` exposes no trial flag. Normal must stay byte-identical to
   `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`, checked by the existing
   binary determinism and hash gates and re-recorded in the count `MANIFEST.txt`.
2. Four pairwise exclusion CTests, each a compile probe that must fail, proving
   `TETRIS_ROW_FUSION_TRIAL` cannot coexist with `TETRIS_DIRECT_KEY_TRIAL`,
   `TETRIS_CHILD_SOA_TRIAL`, `TETRIS_EVAL_INDEX_TRIAL`, or `TETRIS_EVAL_REUSE_TRACE`.
3. One combined probe must also fail with three or more trial macros defined.
4. The trial header alone compiles clean with only its own macro defined, and the
   `#error` text names the conflicting macro so a misconfigured target is diagnosable.
5. `TETRIS_EVAL_REUSE_TRACE` exclusivity is asserted with a dedicated runtime reason, since
   that trace replaces the same `evaluate_once` call site at
   `src/tetris_engine.cpp:1156` to `1160` that this trial edits.

## 9. Retained memory

1. `Engine::retained_bytes()` in a grown directed search must equal the normal binary's value
   for the same workload, with no exception allowance, because the fusion adds no storage.
2. Expected full-workload value `mem_retained_bytes=266338276` on both sides, below the
   `268435456` budget with residual margin `2097180`, at least the `65536` floor.
3. `arena_reserved_bytes=208720320` and `idmap_reserved_bytes=4348340` unchanged, since no
   capacity changes are permitted.
4. A static assertion in the trial header confirms `sizeof(Node)`, `sizeof(Child)`, and
   `sizeof(Evaluation)` are unchanged by the trial, and that the guarded `SafeInputs` is
   aggregate and no larger than 8 bytes, so no hidden storage is introduced.
5. Any nonzero memory delta in any test, CTest, or gate row is a defect that blocks the
   freeze, not a mapped difference to be explained.

## 10. Count ABBA compatibility

The count gate reuses the direct-key collector shape with the trial binary substituted, so
the suite must predict its outcome. Frozen workload: seed 1, warmup 20, 80 measured moves,
depth 6, 1000 iterations, telemetry on, timers off, quiet version 3, order normal, trial,
trial, normal, normal, trial, trial, normal.

1. All 37 frozen common values must appear unchanged on all eight rows: `moves=80`,
   `evals=26632382`, `transitions=26632382`, `searches=1142252`, `dead_moves=0`, `games=0`,
   `node_live_delta_bytes=-2314176`, `warmup_moves=20`, `seed=1`, `iters=1000`, `maxdepth=6`,
   `budget_ms=0.000`, `mode=iters`, `telemetry=on`, `parents=599166`, `widening_iters=80000`,
   `raw_landings=65566529`, `unique_candidates=32446394`, `rule_transitions=32446394`,
   `eval_memo_hits=684163`, `eval_computed=25948219`, `cache_requests=0`, `cache_hits=0`,
   `cache_misses=0`, `cache_replacements=0`, `materialized_nodes=25757157`,
   `transposition_merges=875225`, `promotions_refused=27680`, `pending_end_max=425068`,
   `path_calls=80`, `path_states=76513`, `replay_failures=0`, `arena_reserved_bytes=208720320`,
   `idmap_reserved_bytes=4348340`, `raw_unique_ratio_x1000=2020`, `timers=off`.
2. Plus `mem_retained_bytes=266338276` on all eight rows, giving no mapped exception, unlike
   the Child SoA gate.
3. A short pre-gate unit assertion mirrors this contract: a 20 move scenario must report
   `eval_computed + eval_memo_hits == policy_transitions` and
   `rf_eval_exports == eval_computed`, which is the same identity the gate checks at scale.
4. The prerequisite transcript records the suite across all four build variants, the
   exclusion probes, the symbol isolation result, the equivalence CTest, both partition
   selftests, the hash list, and CPU 15 online afterwards, and its hash is verified by the
   frozen collector before any command runs.

## 11. Failure classification

| Condition | Action |
|---|---|
| Any fused result differs from its reference, or any identity in section 1 fails | implementation defect, repair and re-review, no gate |
| Overlay witness cannot diverge | test defect, rebuild the witness until it diverges, no gate |
| Normal hash or symbol isolation fails | build isolation defect, no gate |
| Any nonzero memory delta | defect, no gate |
| Count gate any mismatch | terminal `FAIL`, trial stops |
| Selector clears neither median at 0.90 or 0.95 with both spreads at most 0.04 | `NO-ADVANCE`, trial stops, no legacy block consumed |

## 12. Reviewer focus

1. That the fused block sits strictly between the `side_roof` loop at
   `src/toj_policy.cpp:357` to `361` and `init_t_value` at line 363, with no intervening
   statement.
2. That `out.value` at line 412 still consumes `side_roof`, not the safe output or any new
   variable.
3. That every early return in `evaluate_once` leaves the output pointer null.
4. That instrumentation cannot be reached from the normal target, and adds no `PROFILE_V3`
   field.
5. That the suite proves one export per computed board by counter identity, not by inference
   from timing or totals.
