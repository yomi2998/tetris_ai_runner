# Evaluate-and-safe row export fusion trial design

Status: proposal for one trial-only, search-count-safe experiment. This is architecture
verdict experiment 5, "Evaluation and safe-row export fusion with bit-identical
results". It changes no observable behavior, no work volume, and no production
binary. It is the second of the two remaining ranked mechanisms, and it must be run
only after the direct-view transposition key trial is closed, which it now is at
`0b30dd8`.

## 1. The duplicated work, located exactly

Two Policy functions export rows from the same `Board` for the same child, one after
the other, inside `Engine::expand_source_for_block`.

1. `Policy::evaluate(Board const &result)` at `src/toj_policy.cpp:349`.
   It writes a clean 40 row stack array:

   ```text
   std::uint32_t rows[policy_height] = {};
   for (int y = 0; y < policy_height; ++y) { rows[y] = result.row(y); }
   ```

   then `int roof = local_roof(rows)` at line 356, then
   `init_t_value(rows, roof, out.t2_value, out.t3_value)` at line 363.

2. `Policy::transition_known_lockout(...)` at `src/toj_policy.cpp:491`, safe block at
   lines 513 to 535, which for the same board does one of:
   - lockout: `safe = -1`, no export.
   - `!context.next.empty()`: export `spawn_frame_height - 1` = 21 clean rows, then
     `safe = scan_safe_rows(rows, context.next[0])`.
   - empty queue: export 40 clean rows, then
     `safe = spawn_frame_height - local_roof(rows)`.

The hot engine seam is `src/tetris_engine.cpp:1161` to `1166`, where
`evaluate_once(applied->board)` is immediately followed by
`policy_.transition_known_lockout(played, candidate, outcome, applied->board, ...)`
with the same board object. The board is not modified between the two calls.

### The mutation hazard

`init_t_value` does not treat `rows` as read only. It calls `apply_overlay`
(`src/toj_policy.cpp:225`), which ORs virtual T minos into the array in place at
lines 237, 243 to 246, 250 to 253, and 306. So after line 363 the array behind
`Policy::evaluate` no longer represents the real board. Any fused consumer of clean
rows must run strictly before line 363. That is why the safe block cannot simply be
moved after the T value computation, and why "reuse evaluate's rows array" as a naive
edit is wrong.

### The two exports read the same words

`Board::row(y)` (`src/tetris_board.h:59`) is
`occupancy_.logical_word(y / 6) >> ((y % 6) * 10) & 0x3ff`. `scan_safe_rows`
(`src/toj_policy.cpp:429`) only indexes `rows[height - 4 .. height - 1]` with
`height = spawn_frame_height - up` and `up` in `[1, danger_limit)`, so it touches only
`rows[0 .. 20]`. Those 21 rows live in occupancy words 0 to 3, exactly the words
`evaluate` read microseconds earlier and which are already resident. This bounds the
recoverable cost low, which matters for the honesty section 6.

## 2. Measured volume

From the accepted direct-key count gate, 80 moves, seed 1, depth 6, 1000 iterations,
telemetry on, timers off (`results/phase7/direct_key_trial_2026-09-09/count/`):

| Quantity | Value |
|---|---:|
| `policy_transitions` | 26,632,382 |
| `eval_memo_hits` | 684,163 |
| `eval_computed` | 25,948,219 |
| evaluate side `Board::row()` calls | 25,948,219 x 40 = 1,037,928,760 |
| safe side `Board::row()` calls, upper bound | 25,948,219 x 21 = 544,912,599 |

`eval_computed + eval_memo_hits` equals `policy_transitions` exactly, which is the
identity that bounds the fusion's reach. The fusion can only serve the
25,948,219 memo misses. On the 684,163 memo hits no evaluation runs, so no clean rows
exist and the transition keeps its own 21 row export, which is 14,367,423 row() calls
that this trial cannot remove.

The 544,912,599 figure is an upper bound, not a promise. It excludes lockout children,
which export nothing today, and counts the empty queue branch at 40 rows where it
actually occurs.

## 3. Mechanism

One fused pass per memo-miss child. No new buffer, no new loop form, no new storage.

### 3.1 Policy side

Inside `Policy::evaluate`, guarded by the trial macro, accept an optional safe
output and the two per-child inputs the transition would otherwise need:

```text
#ifdef TETRIS_ROW_FUSION_TRIAL
    struct SafeInputs
    {
        bool lockout = false;
        bool has_next = false;
        Piece next = Piece::I;
    };
    Evaluation evaluate(Board const &result, SafeInputs const *safe_in,
        int *safe_out) const;
#endif
```

The single definition of `evaluate` gains the two guarded trailing parameters. In the
body, between the existing `side_roof` loop (line 357 to 361) and the
`init_t_value` call (line 363), insert the guarded block:

```text
if (safe_out != nullptr)
{
    if (safe_in->lockout) { *safe_out = -1; }
    else if (safe_in->has_next) { *safe_out = scan_safe_rows(rows, safe_in->next); }
    else { *safe_out = spawn_frame_height - roof; }
}
```

Ordering requirements, all satisfied by construction:

- It reads only `rows[0 .. 20]` or `roof`, both from the clean export.
- It runs before `init_t_value`, so it never sees an overlaid array.
- `roof` is the value `evaluate` already computed at line 356, so the empty queue
  branch reproduces `spawn_frame_height - local_roof(rows)` bit for bit.
- It changes no statement, no operand order, and no accumulator in the existing
  evaluation arithmetic. The `out.value` expression and every preceding scan stay
  exactly as written.

`Policy::transition_known_lockout` gains one guarded trailing parameter
`int const *supplied_safe`. Its safe block becomes:

```text
int safe = 0;
if (supplied_safe != nullptr) { safe = *supplied_safe; }
else if (lockout) { safe = -1; }
else if (!context.next.empty()) { existing 21 row export and scan }
else { existing 40 row export and local_roof }
```

Everything downstream of `safe` is untouched, including `safe -= next.map_rise`, the
clamp to zero, `config_safe`, the `map_rise > safe` death test, and `t2_safe_margin`
and `t3_safe_margin` use. Only the source of the integer changes.

### 3.2 Engine side

In `expand_source_for_block`, hoist the per-source part of the inputs next to the
existing `context` and `t_expect` setup, around `src/tetris_engine.cpp:1022`:

```text
bool const has_next = !policy_next.empty();
Piece const next_piece = has_next ? policy_next[0] : Piece::I;
```

Both are already invariant for the whole call because `context.next = policy_next` is
assigned once per source. `outcome.lockout` is known before `evaluate_once` runs, so
the guarded call becomes:

```text
int fused_safe = 0;
int *fused_safe_ptr = nullptr;
Evaluation evaluation = evaluate_once(applied->board, outcome.lockout, has_next,
    next_piece, &fused_safe, &fused_safe_ptr);
PolicyState state = policy_.transition_known_lockout(played, candidate, outcome,
    applied->board, parent.policy, context, evaluation, outcome.lockout, t_expect,
    fused_safe_ptr);
```

`evaluate_once` gains guarded trailing parameters. It sets the output pointer only on
the branch that reaches `policy_.evaluate(board)` at line 983. On the memo hit return
at line 897, on the disabled-cache hit return at line 972, and on any early
`search_stopped_` return, the pointer stays null and the transition recomputes safe
exactly as it does today. The cached `Evaluation` and every memo push stay unchanged.

### 3.3 Cost model of the fusion itself

The fused path carries one `int` and a small struct pointer through two calls, and it
shortens the child loop by one 21 word stack array plus its write loop. It adds no
allocation, no container, no board metadata, no `top` field, and no persistent array.
This respects the verdict's constraint that the trial "would not add top or persistent
board metadata".

## 4. Explicit non-goals

This trial does not change any of the following, and any change is a gate failure:

- Evaluation arithmetic, operator order, `normalize_zero` treatment, or `Evaluation`
  values.
- `PolicyState` fields, attack, combo, b2b, like, dislike, acc value, map rise, death.
- Policy transition counts, candidate enumeration, canonical ordering, source-local
  board and outcome dedup, first survivor retention.
- Transposition key construction, fingerprints, probe walk, merge decisions, rehash,
  epoch reset.
- Child staging, `Node` layout, `Child` layout, arena capacity, heap representation,
  pop order, root child selection, final path, replay.
- Every `PROFILE_V3` counter and every rate denominator.

## 5. Build isolation and exclusivity

- Macro name `TETRIS_ROW_FUSION_TRIAL`, defined only on dedicated trial targets.
- Normal `tetris_profile_value` compiles zero trial code and must remain byte identical
  to SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`. The
  guarded parameters exist only inside `#ifdef` blocks, so the normal target's
  generated code is unchanged, and `nm` on the normal binary must show zero
  `row_fusion` symbols, mirroring `direct_key_absent_from_normal_build`.
- Trial targets, mirroring the accepted direct-key layout in `CMakeLists.txt:159` to
  `196` and `413`:
  - `tetris_profile_row_fusion`
  - `row_fusion_trial_tests`
  - `candidate_partition_row_fusion`
  - CTests `row_fusion_absent_from_normal_build`, `row_fusion_profile_equivalence`,
    and the four macro exclusion compile probes.
- Fail closed on unsupported combinations. A new `src/row_fusion_trial.h` declares
  `SafeInputs`, carries the trial static assertions, and begins with the same
  `#error` pairs the direct-key header uses at `src/direct_key_trial.h:2` to `11`,
  against all four of `TETRIS_DIRECT_KEY_TRIAL`, `TETRIS_CHILD_SOA_TRIAL`,
  `TETRIS_EVAL_INDEX_TRIAL`, and `TETRIS_EVAL_REUSE_TRACE`, plus one combined probe.
  `TETRIS_EVAL_REUSE_TRACE` in particular replaces the very `evaluate_once` call this
  trial edits at `src/tetris_engine.cpp:1156` to `1160`, so the two cannot coexist.

## 6. Honesty about the size of the prize

Two independent estimates, both derived before any trial timing.

Estimate A, microbenchmark. A standalone `-O3 -march=native` harness against the real
`tetris::Board` and a replica of `scan_safe_rows` measured the transition safe block at
7.850 ns, the same scan reading an already exported array at 4.431 ns, so the removable
export part is 3.418 ns per memo-miss child. Over 25,948,219 misses that is 91.0 ms
against a `run_ms` of 7798.954, or 1.17 percent of measured run time.

Estimate B, existing attribution. The timers-on architecture row
(`results/phase7/architecture_review_2026-09-09/raw/architecture-review-2026-09-09-candidate-timers.txt`)
gives `policy_ns` 1,242,238,647 of `run_ms` 11,970,779, so the whole transition scope is
10.4 percent. The safe export is 3.418 of the 46.6 ns average transition, about 7.3
percent of that scope, so roughly 0.76 percent of run time. Timers-on inflates both
sides, so treat 0.76 percent as the conservative bound and 1.17 percent as the optimistic
bound.

Conclusion stated plainly: the removable work is about 0.8 to 1.2 percent of candidate run
time. The verdict's own ceiling threshold says the total ratio needs a 2.069 percent
reduction to move 1.04155 to 1.02, and the p95 ratio needs 0.533 percent. Even the top of
this trial's range is below the total gate requirement, though it is above the p95
requirement. This trial is therefore not expected to qualify the migration on its own, and
it should not be presented as a candidate for the 0.90 primary target. Its real value is
that it is cheap, count-safe, and stackable evidence: a clean negative at about 1 percent
is exactly the kind of measurement the verdict's ceiling criteria 3 and 5 ask for, and a
clean positive buys roughly half the distance to the 1.02 bar.

Why prior row variants failed and this one differs. The verdict records that "simple
row-reuse, bulk-row, and unrolled-row variants have already performed poorly". Those
approaches rewrote how rows are produced, and they paid for it in ways the fused design
does not:

- Bulk or cached row export moves 40 or 48 rows through shared storage, adding a write
  plus a read of a buffer that `Board::row()` already serves from three L1 words.
- Unrolled and hand-vectorized export loops fight the compiler's own scheduling of a
  21 or 40 iteration loop over a hot cache line, and risk register pressure in the
  hottest function in the policy.
- Row reuse across siblings is invalid here anyway, because each child has a different
  resulting board.

This trial rewrites no loop and adds no buffer. It deletes one redundant pass by
reordering an existing read of data the evaluation already holds live, and it pays only
for passing one integer. If that still does not show up as wall time, the honest reading
is that the candidate's remaining disadvantage is not row export, which directly serves
verdict ceiling criterion 7.

## 7. Bit identity argument and tests

Argument, in the order a reviewer should check it.

1. `Board::row` is a pure function of the board object. The same `applied->board` is
   passed to both Policy calls and nothing mutates it in between, so evaluate's
   `rows[0 .. 20]` and the transition's own 21 row array hold identical values.
2. The fused safe block executes strictly before the first `apply_overlay` call, so it
   reads the clean array, matching today's separate clean export.
3. All three branches reproduce today's values exactly: lockout gives -1, next present
   calls the same `scan_safe_rows` with the same piece and the same row prefix, and
   empty queue reduces to `spawn_frame_height - roof` where `roof` is evaluate's own
   `local_roof` result on the same clean array.
4. Only the source of `int safe` changes; every downstream use, clamp, and comparison
   stays in place, so `PolicyState` is produced by identical arithmetic.
5. `Evaluation` is returned by an unmodified sequence of statements.
6. On memo hits and early returns the supplied pointer is null, so the untouched legacy
   path runs. The reachable set of outcomes is therefore the same set with one producer
   relocated.

Required tests, in `row_fusion_trial_tests` plus the guarded unit cases:

- Branch coverage: for all seven pieces, both `has_next` states, and both lockout
  states, assert the fused safe equals the value from the standalone path. Lockout must
  yield -1 and must not read rows.
- Overlay hazard witness: a board engineered so `init_t_value` calls `apply_overlay`
  inside `rows[0 .. 20]`. Assert the fused safe equals the clean rows reference and
  differs from the value obtained by reading the post `init_t_value` array. Without a
  witness that can actually diverge, this test is vacuous.
- Evaluation identity: call `evaluate` with and without the safe output on the same
  board and compare `value`, `t2_value`, `t3_value` bitwise, including sign of zero,
  across a directed corpus and a seeded random corpus.
- Transition identity: with a supplied safe and with a null safe, assert the full
  returned `State` compares equal field by field.
- Engine integration: reuse the existing full search determinism and telemetry suites,
  and the `toj_policy_tests` tallies, which already pin next present, next single T,
  next absent, and next empty coverage at `tests/toj_policy_tests.cpp:1224`.
- Profile equivalence CTest: compare every non-timing `PROFILE_V3` field between the
  normal and trial binaries on a short workload, with no mapped field exception in this
  trial. `mem_retained_bytes` must be equal, expected `266338276` on both sides, since
  the fusion adds no storage. Any nonzero memory delta is a defect, and the value must
  stay under `268435456` with at least `65536` residual margin, which it does at
  `2097180`.
- Normal-build absence and the four macro exclusion probes, as build failures rather
  than runtime checks.

Gate sequence, unchanged from the accepted discipline: implementation, independent
read-only review, commit, prerequisite transcript captured and hashed, one
fail-closed collector run of 80-move telemetry-on and timers-off count ABBA with two
byte-identical partition traces and eight rows, then only on an accepted `PASS` a frozen
telemetry-off four-pair selector with the existing 0.90 primary, 0.95 owner fallback, and
0.04 spread boundaries. Count evidence never authorizes a legacy run. If the selector
fails to clear normal, the trial stops and no legacy block is consumed.

## 8. Review risks

Highest first.

1. Silent post-overlay read. If a future edit moves the fused block after line 363,
   results change without changing any counter. Mitigation: the overlay hazard witness
   test, plus placing the block immediately before the `init_t_value` call with no
   intermediate statement.
2. Memo hit drift. If the engine wrongly reports a fused safe as valid after a cached
   return, the transition consumes a stale integer from a different source context.
   Mitigation: the output pointer is written only at the compute site, tests for both
   hit and miss, and the count gate compares `eval_computed` and `eval_memo_hits` for
   identity, which pins the reachable mix.
3. Per-source assumption drift. `next_piece` is hoisted on the fact that
   `context.next` is assigned once per `expand_source` call. If hold handling ever makes
   the next piece child dependent, hoisting is wrong. Mitigation: assert
   `context.next.data() == policy_next.data()` where context is built, and keep
   `has_next` and `next_piece` adjacent to the context construction so the coupling is
   visible.
4. Zero effect lost in noise. A 0.8 to 1.2 percent change is comparable to observed
   selector spread, so a pair balanced four pair design and the unchanged spread limits
   are required, and no appended pair may be used to chase significance.
5. Scope creep toward reuse. It is tempting to also cache `Evaluation` or safe values
   across sources. That is the rejected index and cache line and must not be combined
   with this patch under any circumstances.
6. Normal binary drift from guarded parameter changes. Mitigation: hash equality against
   `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765` and the symbol
   absence test, both before the count gate and again in `MANIFEST.txt`.

## 9. Decision requested

Proceed only on owner go ahead, with the explicit expectation set in section 6: this
mechanism is about 1 percent of candidate run time and cannot by itself close the
2.069 percent total gate. Treat it as the last ranked removable cost experiment and as
ceiling evidence either way.
