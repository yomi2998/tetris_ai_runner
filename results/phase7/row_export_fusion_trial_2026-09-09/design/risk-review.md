# Evaluate-and-safe row export fusion trial, risk review

Scope: the proposal in `design/design.md` for this trial, checked against the actual
source. Read-only review. No edits, no builds, no benchmark execution.

Method: every load bearing claim in the design was checked against
`src/toj_policy.cpp`, `src/toj_policy.h`, `src/tetris_engine.cpp`, `src/tetris_board.h`,
`src/direct_key_trial.h`, `CMakeLists.txt`, and `CMakePresets.json`. Line citations were
followed rather than trusted.

## Verdict

**Proceed, with the expectation the design already states in section 6 and with two test
design corrections in findings F2 and F4 below.**

The mechanism is safe to implement and cheap to test. It is not expected to qualify the
migration, and on the campaign's own noise history it is unlikely to be classifiable even
if it is a real win. That makes it ceiling evidence, which is its stated purpose. It should
not be sold as a route to the 0.90 primary target.

## What the review confirmed

1. The duplicated work is real and located correctly. `Policy::evaluate` exports all 40
   rows into a local `std::uint32_t rows[policy_height]` at `src/toj_policy.cpp:349` to
   `354`. `Policy::transition_known_lockout` re-exports the same board at
   `src/toj_policy.cpp:513` to `535`, 21 rows when the next queue is present and 40 rows
   when it is empty. `policy_height` is 40 and `spawn_frame_height` is 22, so the 21 figure
   is `spawn_frame_height - 1`, as claimed (`src/toj_policy.h:21-22`).

2. The seam is exactly as described. `evaluate_once(applied->board)` at
   `src/tetris_engine.cpp:1161` is followed immediately by
   `policy_.transition_known_lockout(played, candidate, outcome, applied->board, ...)` at
   `1165`. `applied->board` is passed to both by const reference and nothing between the
   calls writes it. `evaluate_once` takes `Board const &` and stores memo copies by value.

3. The mutation hazard is genuine and correctly bounded. `init_t_value` at
   `src/toj_policy.cpp:251` calls `apply_overlay`, which ORs T minos into the array in
   place at `src/toj_policy.cpp:231`, `237`, `240`, and `245`. Therefore any consumer of
   clean rows must run strictly before the `init_t_value` call at
   `src/toj_policy.cpp:363`. The design's insertion point satisfies this.

4. `scan_safe_rows` truly touches only `rows[0 .. 20]`, so reusing evaluate's array is
   value-identical to a fresh 21 element array. With `danger_limit` 19
   (`src/toj_policy.cpp:100`) the loop breaks at `up >= 19` without reading, so the live
   range is `up` in `[1, 18]`, `height = 22 - up` in `[4, 21]`, and the read window
   `rows[height - 4 .. height - 1]` is contained in `rows[0 .. 20]`. The standalone array
   is also `{}` zero initialized, but since no read escapes `[0, 20]` there is no
   out-of-range or padding difference. Confirmed safe.

5. The empty-queue branch reproduces exactly. `local_roof` (`src/toj_policy.cpp:118`)
   scans the full 40 element array from the top, and evaluate's `roof` is that same
   `local_roof` over the same clean 40 values, so `spawn_frame_height - roof` is identical
   to today's `spawn_frame_height - local_roof(rows)`.

6. The per-source hoisting assumption holds today. `context.next = policy_next` is assigned
   once per `expand_source_for_block` at `src/tetris_engine.cpp:1101`, `context` is a local
   passed by const reference to the transition, and nothing in the child loop writes it.
   `t_expect` is already hoisted by the same argument, so the pattern is established.

7. Single seam coverage is complete for this trial, but only because of macro exclusion.
   There is a second identical seam at `src/tetris_engine.cpp:1396` and `1399`, inside the
   `TETRIS_CHILD_SOA_TRIAL` variant of `expand_source_for_block` that begins at line 1280.
   It is not compiled when the SoA macro is absent, and the fusion macro must exclude the
   SoA macro. This is worth stating in the design's section 5 so a later reader does not
   assume the 1161 seam is the only one in the file.

8. Count identity arithmetic checks out. `eval_computed` 25,948,219 plus `eval_memo_hits`
   684,163 equals `policy_transitions` 26,632,382 exactly, which is the identity the design
   uses to bound the fusion's reach.

9. Build isolation is achievable as designed. `evaluate_once` is a single definition in the
   non-trace build (`src/tetris_engine.cpp:876`), so the guarded trailing parameters thread
   through `evaluate_once` directly to `policy_.evaluate` at line 983. The
   `evaluate_once_for_parent` split at lines 868 and 873 exists only under
   `TETRIS_EVAL_REUSE_TRACE`, which the new header must exclude. The four `#error` pairs
   already used at `src/direct_key_trial.h:2` to `11` are the correct template.

10. No fast-math exposure. The only optimization flags are `-O3 -march=native` plus LTO in
    the release presets (`CMakePresets.json:27` and `67`). There is no `-ffast-math`,
    `-Ofast`, or `-ffp-contract` change, so IEEE semantics are preserved and the design's
    bit-identity argument is not threatened by compiler options.

## Risk register

Severity is the review's own scale. Each row is closed by the named mitigation.

| Id | Risk | Severity | Mitigation | Status |
|---|---|---|---|---|
| R1 | Silent post-overlay read if the fused block is ever moved after `init_t_value` | High | Overlay witness test per F2, plus placing the block immediately before the `init_t_value` call with no statement between | Closed by test design |
| R2 | Memo hit consumed as a fresh computation, feeding a stale safe integer | High | Write the output pointer only at the compute site at line 983; directed tests for hit and miss; count gate pins `eval_computed` and `eval_memo_hits` | Closed by design plus test |
| R3 | `context.next` becomes child dependent, invalidating the hoisted `next_piece` | Medium | Assert `context.next.data() == policy_next.data()` where context is built; keep `has_next` and `next_piece` adjacent to that construction | Closed by assertion |
| R4 | Bit drift in `Evaluation` from the signature change interacting with LTO | Medium | Bitwise `evaluate` with and without the safe output over directed and seeded corpora, including sign of zero; profile equivalence CTest on every non-timing field | Closed by test |
| R5 | Component timer attribution moves between scopes, later misread as a speedup | Medium | Disclose in `MACHINE.md` that work moves from the `policy_ns` scope into the `evaluate_once` miss scope; verdicts use telemetry-on timers-off count rows and telemetry-off selector rows only | Closed by disclosure |
| R6 | Scope creep into caching `Evaluation` or safe values across sources | High | Explicit non-goal in design section 4 and section 8.5; the count gate's exact 37 field identity plus `transposition_merges` catches work-volume drift | Closed by gate |
| R7 | A real but small win buried under pair-to-pair spread | Medium to high, statistical not correctness | Four balanced pairs, unchanged 0.04 spread limit, no appended pairs, no favorable subset | Accepted, unmitigable |
| R8 | Normal binary drift from guarded parameter edits | Low | Hash equality against `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765` and zero `row_fusion` symbols via `nm`, checked before the gate and again in `MANIFEST.txt` | Closed by test |
| R9 | New storage smuggled in as a per-source buffer | Low | `SafeInputs` and one `int` on the stack only; `mem_retained_bytes` must equal `266338276` on every row of both engines with no mapped exception, so any heap growth fails the gate | Closed by gate |

## The six requested assessments

### The `init_t_value` mutation hazard and other aliasing or lifetime hazards

The hazard is real and the design's handling is correct, with one qualification worth
recording: `init_t_value` receives `roof` by value before any overlay happens, so the
overlay changes `rows` without changing `roof`. That asymmetry means the empty-queue branch
of the safe block is **insensitive** to the overlay ordering bug, while the next-present
branch is sensitive. A witness test that exercises only the empty-queue branch would pass
even with the fused block moved after `init_t_value`. The test must therefore run with
`has_next` true, and must assert inequality against the post-overlay read. See F2.

Other aliasing and lifetime checks:

- `fused_safe` lives in the child loop body and is consumed by the very next statement. No
  escape. If `SafeInputs` is materialized as a temporary inside the call expression it is
  alive for the full expression, so taking its address is still safe, but it must never be
  stored past the call. Review the final diff for this.
- `scan_safe_rows` takes `uint32_t const *`. Passing evaluate's local array is a
  same-function call, no lifetime question.
- The memo vectors can reallocate, but the fusion holds no reference into them; the child's
  board is owned by `applied` in the loop frame. No aliasing path exists.
- `PolicyState next = parent` copies rather than aliases, and `parent.policy` is a const
  reference to the arena node that nothing in either call writes.

### Bit-identity risk in floating point

Low, and the design's argument is sound. The fused block writes one integer and reads only
`roof` or `rows[0 .. 20]`. It adds no statement, operand, or accumulator to the evaluation
arithmetic, and `out.value` at `src/toj_policy.cpp:420` to `424` is left byte for byte as
written. There is no fast-math flag in any preset, so no reassociation is licensed, and the
only remaining vector of change is codegen shape under LTO, which cannot reorder IEEE
operations without a semantics change. `normalize_zero` applies to `acc_value`, `like`, and
`value` inside `init_t_value`'s inputs and the key path, none of which the fusion alters.

The residual is a measurement question rather than a correctness one: because the signature
change can itself alter inlining, part of any timing delta may be codegen rather than the
removed export. That is inherent to a one-mechanism patch and must be reported as a result
about the patch. The claim to avoid is that a measured delta isolates the export removal.

### Could the fused path change lockout or queue-state exports

Not by the design as written.

- Lockout children: today they export nothing and yield `safe = -1`. The fused block must
  short-circuit to -1 before any read. It runs after `local_roof` and the `side_roof` loop,
  which is fine because those belong to `evaluate` regardless of lockout; the transition's
  own export is what disappears. Behavior is identical.
- Next present: same function, same piece, same row prefix, same array contents. Identical.
- Empty queue: reduces to evaluate's own `roof`. Identical per section 5 above.
- Everything downstream of `safe` is untouched, so `safe -= next.map_rise`, the clamp,
  `config_safe`, the `map_rise > safe` death test, and the t2 and t3 safe margin uses all
  consume the same integer.

The one way this goes wrong is the R2 memo-hit case: a null pointer must force the legacy
path. The disabled-cache branch at `src/tetris_engine.cpp:951` to `976` is dead in
production because the layout is `Disabled`, which is why `cache_requests=0` in every
accepted row, but the pointer must still be left null on that path so the trial cannot
diverge if a test enables the cache.

### Interaction with telemetry counters

No new counters, which is what keeps this trial's gate as strict as the earlier ones. The
existing identity requirements still apply: `eval_requests`, `eval_memo_hits`,
`eval_computed`, `policy_transitions`, `materialized_nodes`, `transposition_merges`, and
`rule_applications` must all be exactly equal across engines, and the count gate requires
that on all 37 frozen values plus `mem_retained_bytes`.

Two interactions deserve explicit note:

1. `timers_.policy_ns` in the engine currently wraps only the transition call at
   `src/tetris_engine.cpp:1163` to `1168`, so the transition's row export is charged to
   `policy_ns`. After fusion that work moves inside `evaluate_once`, charged to the eval
   miss scope instead. Any timers-on diagnostic run will therefore show `policy_ns` falling
   and `eval_miss_ns` rising by a comparable amount without any wall change. Counters-only
   and telemetry-off rows are unaffected. Disclose it.
2. The partition recorder and the `record_push` path observe candidates, outcomes, and board
   hashes, not safe margins, so ordered expansion identity remains a valid check. Keep the
   byte-identical partition trace requirement unchanged.

### Is the removable cost plausibly above the roughly 2 percent threshold

No, and the design's own estimate is the right one. Both independent routes put it lower:

- Estimate A gives 3.418 ns removable per memo-miss child, so about 91 ms against 7799 ms
  of `run_ms`, or 1.17 percent.
- Estimate B gives about 0.76 percent from the timers-on `policy_ns` share.

The design's range of 0.8 to 1.2 percent sits below the 2.069 percent total reduction the
verdict's ceiling threshold needs, and only overlaps the 0.533 percent p95 requirement.

Add two things the design does not say, which make the practical outlook worse than the
arithmetic alone.

First, the campaign's own noise floor. Observed selector spreads were 0.033 for the SoA
total median and 0.066 for its p95, and 0.050 and 0.063 for the direct-key trial. A one
percent median effect is smaller than those spreads, so even a genuine win has a poor chance
of clearing the unchanged 0.04 spread limit on four pairs. Since appended pairs are forbidden,
the likely honest outcome of a real one percent improvement is still `NO-ADVANCE`.

Second, the prior row variants. The verdict records row-reuse, bulk-row, and unrolled-row
as already performing poorly. The design's explanation for why this differs is correct and
is the strongest part of the proposal: it adds no buffer and rewrites no loop, and it only
deletes a redundant pass over data the caller already holds live. But those failures also
show that this code region has repeatedly converted removed work into equal or worse wall
time, most likely because the second export is served from three resident L1 words. Estimate
A measures the removable part in isolation; it cannot measure that the export was already
nearly free in context. That is the specific reason to treat a null result as informative.

So: plausible upside is around one percent on paper and probably unmeasurable in practice.
This is ceiling evidence, exactly as design section 6 frames it.

### Memory accounting traps

The fusion removes stack arrays and adds one `int` plus a small struct pointer, so
`mem_retained_bytes` must be **equal**, not reduced: `266338276` on both engines, with
`arena_reserved_bytes` `208720320` and `idmap_reserved_bytes` `4348340` unchanged, and
`node_live_delta_bytes` `-2314176` unchanged. Residual margin against the `268435456` cap
stays at `2097180`, well above the `65536` floor.

Traps to guard:

- Any nonzero memory delta is a defect in this trial, not a mapped exception as it was for
  the SoA staging gate. The runbook must therefore drop the mapped-field allowance, which
  the design does correctly in sections 5 and 7. Reusing the SoA collector template without
  that edit is the single most likely way to ship a wrong gate.
- Do not let the trial add a per-source vector or arena of safe values, even sized to a
  fixed bound. That is R9, and it is also the boundary toward the rejected cross-parent
  reuse line.
- Stack shape changes do not move `mem_retained_bytes`, which measures retained heap, so
  the removed 84 byte or 160 byte arrays will not show up there. Do not expect a memory
  improvement as a signal that the fusion happened; the partition trace and count identity
  are the only structural evidence.

## Findings to fix before implementation

F2, test design, severity medium. The overlay witness test is only meaningful with
`has_next` true. The empty-queue branch reads just `roof`, which `init_t_value` does not
change, so it cannot detect a post-overlay bug. Specify the witness as a board that forces
`apply_overlay` into `rows[0 .. 20]`, evaluated with a nonempty next queue, asserting both
equality against the clean-rows reference and inequality against the post-overlay read.

F4, design accuracy, severity low. The design labels 544,912,599 safe-side row calls an
upper bound, but it is mixed: lockout children export nothing, so 21 per child overcounts
them, while empty-queue children export 40, so 21 undercounts them. The correct description
is an estimate that is tight for the common next-present branch. The conclusion is unaffected
because the next-present branch dominates, and the conclusion is already below the gate.

Two smaller notes, both documentation only.

N1. Design section 3.2 refers to adding trailing parameters to `evaluate_once`, while the
trace build defines `evaluate_once_for_parent` instead. Since that macro is excluded, state
the threading against the non-trace definition at `src/tetris_engine.cpp:876` so the
implementation does not hunt for a second signature.

N2. Design section 1 does not mention the second seam at `src/tetris_engine.cpp:1396` inside
the SoA variant. Add one sentence noting it is excluded by macro mutual exclusion, which is
what makes the single-seam scope complete.

## Recommendation

Proceed to implementation on the terms in the design, with F2 and F4 applied and N1 and N2
recorded, and with the gate sequence unchanged: implementation, independent read-only review,
commit, prerequisite transcript, one fail-closed 80-move telemetry-on count ABBA with two
byte-identical partition traces and eight rows and full non-timing identity including
`mem_retained_bytes` with no mapped exception, then only on an accepted `PASS` a frozen
four-pair telemetry-off selector at the 0.90, 0.95, and 0.04 boundaries.

Two conditions on how the result should be reported. Set the expectation before collection
that a one percent effect is below this machine's four-pair spread and will most plausibly
classify as `NO-ADVANCE` even if the mechanism is a real win, and do not append pairs to
chase it. If the selector fails, say plainly that the last ranked removable per-candidate
export cost has been measured and did not convert, which is verdict ceiling criterion 7
evidence, and stop the line rather than reopening the combined or cache variants.

No legacy block may be consumed by this trial, and no production behavior, gate, or artifact
changes at any outcome.
