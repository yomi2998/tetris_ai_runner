# Session report, 2026-09-09: Child SoA staging trial

## Authorization and boundaries

After the exact-index line stopped at `NO-ADVANCE`, the owner authorized one bounded, trial-only Child structure-of-arrays staging experiment against the normal candidate. The scope was exactly the internal per-parent staging representation: two-array Child SoA staging with no change to public `Child`, `Node`, evaluation, policy, candidate enumeration, heap, transposition representation, work volume, or default arena capacity.

The sequence was: implement and review, commit, pass a full 80-move telemetry-on/timers-off count ABBA gate, and only then run a frozen telemetry-off normal-versus-SoA selector. Legacy evidence was not to be consumed unless the selector showed a clear current-candidate win. Direct-view transposition keys remained a separately gated fallback that must not be combined with SoA.

## Implementation

The trial-only layout stages each parent's children in two arrays: aligned 64-byte `Board` values separate from a compact 104-byte metadata record, 168 bytes per staging slot against the 192-byte `Child` aggregate. Materialization constructs each `Node` directly from staged fields exactly once, with no default-then-overwrite and no local aggregate copies. Source-local deduplication scans only the current `expand_source` window. Insertion order, NodeId assignment, sibling links, heap order, and transposition merges are unchanged.

Combined use with `TETRIS_EVAL_INDEX_TRIAL` or `TETRIS_EVAL_REUSE_TRACE` fails compilation. Normal `tetris_profile_value` compiles no trial code and remains byte-identical to SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.

Independent review repaired the initial materialization before any evidence collection and added 267-check suites covering exact-capacity overflow, depth-zero merge, macro exclusion, and profile equivalence. The SoA trial binary hash is `721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91`.

Committed as `e02f476` with the partition-trace target at `369d447`.

## Count gate attempts

The gate ran under fail-closed runbooks with a single collector script that verifies provenance, hashes, machine controls, load gates, and one continuous CPU 15 offline window before and during collection.

- Attempt 1 stopped `BLOCKED`: the five-minute load never fell below 1.5 within the ten-minute budget, so nothing ran. Preserved at `19ea6a1`.
- Attempt 2 was rejected `FAIL` by parent verdict and adversarial review: a first collection ran with CPU 15 online after a control permission failure, was deleted, and was replaced. The runbook forbids extra, replacement, or selectively retained rows and unauthorized deletion. The preserved replacement rows were internally semantically identical but could not ground a pass. Preserved at `bffa256`.
- Attempt 3 passed. One collector invocation produced two byte-identical partition traces and eight ABBA rows with zero reruns, one continuous CPU 15 offline window, and every exit zero.

Accepted count evidence at `b1ab3bc`, key results:

- All 37 frozen common values matched in every row, including `evals=26632382`, `searches=1142252`, `materialized_nodes=25757157`, and `transposition_merges=875225`.
- Normal repeats and SoA repeats were identical on all non-timing fields; cross-engine, only `mem_retained_bytes` differed.
- Retained memory: normal `266338276`, SoA `266153956`, exact reduction `184320`, SoA residual margin `2281500` against the `268435456` cap.

## Timing selector

The first selector attempt aborted before any benchmark logic: copied artifacts were frozen at mode `444` without execute bits, so the first prewarm exited 126. The collector stopped, restored CPU 15, and preserved the abort. Preserved at `eac8039`.

The corrected attempt used fresh mode-`555` artifacts with identical hashes, a new collector hash, and the same frozen order and thresholds. Frozen at `5c6740f`.

The collector ran exactly two prewarm and eight timed commands, telemetry off, timers off, CPU 7 pinned, one continuous CPU 15 offline window, all exits zero, zero reruns. Four balanced pairs: normal/SoA, SoA/normal, SoA/normal, normal/SoA, 200 moves, seed 1, depth 6, 1000 iterations.

SoA divided by normal ratios:

- Total: 1.11886, 1.12928, 1.09736, 1.09613.
- p95: 1.13118, 1.12364, 1.06498, 1.09171.
- Exact total median 1.10811, spread 0.03315.
- Exact p95 median 1.10768, spread 0.06620.

Both medians exceed the 0.90 primary target and the 0.95 fallback band, and the p95 spread exceeds 0.04. The sole terminal classification is `NO-ADVANCE`. The Child SoA trial is about 10.8 percent slower than the normal candidate on both metrics.

Accepted and committed at `52d6ee0`.

## Interpretation

The two-array staging layout preserved every semantic and work-vector count while reducing retained memory, but the 24-byte-per-slot saving did not convert into wall time. The two-vector indirection, per-child Board copies during staging, and direct construction costs outweighed the avoided aggregate writes at this workload. As with the index trial, count identity established semantics only; timing claims came solely from the frozen telemetry-off selector.

This closes the Child SoA line as tested. It does not prove every staging representation impossible, but another standalone staging variant should not proceed without a materially different cost model.

## State of the optimization stack

- Frozen pre-migration legacy baseline: `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`.
- Normal candidate: `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.
- Migration qualification remains blocked by gates 1 and 2 performance failure; the 1.02 bars are unchanged.
- The exact live-node index line and the Child SoA staging line are both stopped at `NO-ADVANCE`.
- No legacy evidence was consumed by the SoA trial. No qualification, cutover, quality campaign, gate revision, or deletion is authorized.

## Next step, requiring owner decision

The remaining ranked mechanism from the architecture review is direct-view transposition keys with once-per-source context: compare a stored 64-bit key instead of materializing keys through the current path. It must be pursued, if at all, under its own separate gate and never combined with staging or index changes. Evaluate-and-safe row export fusion remains later; prior bulk and unrolled row variants regressed.

This report does not authorize that work. It requires an explicit owner gate.

## Commits

- `e02f476`: trial-only Child SoA staging implementation.
- `369d447`: Child SoA partition trace target.
- `b6a66dd`: frozen count gate runbook.
- `19ea6a1`: preserved load-blocked attempt.
- `bffa256`: rejected tainted attempt with parent verdict and prerequisite transcript.
- `1884768`: frozen fail-closed count collector.
- `b1ab3bc`: accepted semantic count gate.
- `26384a5`: frozen timing selector.
- `eac8039`: preserved mode-444 selector abort.
- `5c6740f`: frozen corrected selector.
- `52d6ee0`: accepted `NO-ADVANCE` selector evidence.
