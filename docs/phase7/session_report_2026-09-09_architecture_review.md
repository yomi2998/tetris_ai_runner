# Session report, 2026-09-09: architecture review and exact evaluation-reuse trace

## Scope and authorization

The owner provided an architecture-review verdict correcting stale performance premises and selected: archive the verdict and raw evidence, then run a diagnostic-only exact evaluation-reuse trace. No production cache, optimization, cutover, gate change, or legacy deletion was authorized.

The implementation and execution were delegated to subagents. The parent retained validation and commit authority.

## Archived architecture review

The pre-verdict request is preserved at `docs/phase7/architecture_review_request.md`. The corrected verdict is preserved at `docs/phase7/architecture_review_verdict_2026-09-09.md`. Nine raw count, timer, row, and perf artifacts are stored byte-for-byte under `results/phase7/architecture_review_2026-09-09/raw/`; every entry passes its recorded SHA-256 check.

The review rejects near parity as a demonstrated architectural ceiling. Its leading hypothesis is limited evaluation reuse, followed by bulky Child-to-Node staging, excess candidate volume before nearly equal policy-transition volume, and repeated packed-board row extraction. Dynamic reachability and final pathfinding are not leading costs.

## Diagnostic implementation

A dedicated `eval_reuse_trace` executable is compiled with `TETRIS_EVAL_REUSE_TRACE`. All trace records, EngineConfig hooks, engine branches, option fields, and CLI parsing are conditionally compiled. Normal `tetris_profile_value` contains no trace path and rebuilds byte-identically to the frozen candidate (`1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`).

The trace records exact eight-word Board occupancy images for evaluation requests and live materialized nodes. Exact board images determine identity. The existing occupancy fingerprint is diagnostic only and never establishes equality. Arena reset epochs disambiguate NodeId reuse across moves.

`tests/eval_reuse_analyze.py` performs measured-only analysis, excludes warmup without priming any state, simulates fully associative LRU and direct/2/4/8-way caches, partitions misses into cold/capacity/conflict classes, reports source/depth and request/parent distance distributions, audits fingerprint collisions, and measures reuse attainable from currently live nodes. Its resource use is bounded by a hard address-space limit. The 5.16 GB raw trace remains in `/tmp` and is excluded from git; the deterministic summary, manifest, commands, hashes, and result rows are stored under `results/phase7/eval_reuse_trace_2026-09-09/`.

## Rejected diagnostic attempts

Two intermediate results were rejected rather than repaired in place or silently reused:

1. The first Python analyzer included 20 warmup moves and grew to about 16.6 GB RSS. It was stopped. Its summary and findings report were deleted; `rejected_attempt_2026-09-09.md` records the failure.
2. The first bounded analyzer classified parents by NodeId alone. Arena indices are reused across move resets, producing 684,196 apparent same-parent repeats instead of the exact memo-hit count 684,163. Its summary and findings report were deleted; `rejected_attempt_parent_identity_2026-09-09.md` records the failure.

The parent found the second defect after the first fresh reviewer had reported no issue. The corrected analyzer identifies a parent by reset epoch plus NodeId and adds the invariant that same-parent repeats equal parent-local exact memo hits. A regression fixture reuses the same numeric NodeId in two measured moves and requires cross-parent classification.

## Accepted measured-only result

Workload: seed 1, warmup 20, measured moves 80, max depth 6, 1,000 iterations, frozen parameter file, CPU 7, telemetry on, component timers off.

- Evaluation requests: 26,632,382.
- Distinct boards: 9,031,593 (33.912 percent).
- Exact repeated boards: 17,600,789 (66.088 percent).
- Same-parent repeats: 684,163, exactly equal to `eval_memo_hits`.
- Cross-parent repeats: 16,916,626 (96.113 percent of repeats).
- Requesting parent expansions: 599,166, exactly equal to PROFILE parents.
- Cross-parent repeats matching a currently live node: 13,106,967 (77.480 percent of cross-parent repeats and 49.214 percent of all requests).
- Fingerprint groups containing more than one exact board: 164,123.

A fully associative 1,048,576-entry LRU captures 17,598,837 of 17,600,789 repeats. This shows that practical capacity is sufficient for nearly all repeated boards under an appropriate representation. The current FNV set mapping performs poorly under bounded associativity: 8-way at the same total capacity captures 5,993,345 hits and incurs 11,605,492 conflict misses. Exact verification and a collision-tolerant index are mandatory; copying the old hash-only table would violate correctness.

The diagnostic therefore confirms the review's leading explanation. Parent-local scope, not evaluation arithmetic or reachability itself, causes the largest verified cross-engine work amplification. A node-backed exact index could potentially avoid a large fraction of current computations without storing another 64-byte Board per cache slot. This is opportunity evidence, not a performance claim; lookup, collision handling, invalidation, and cache effects remain unmeasured.

## Validation

- Trace-on and trace-off rows contain identical values for every non-timing field and all work/result counters.
- Same-parent repeats equal exact memo hits: 684,163.
- Requesting parents equal expanded parents: 599,166.
- Repeat, distance, source/depth, record, NodeLive, warmup, spool-size, calibration, and all 30 cache-partition identities reconcile.
- Raw trace is byte-identical across three collections: SHA-256 `5956cfc5015360cd159ce023f7f05f8ca81119bb872c68dd9a7b675c16bb684f`.
- Full analyzer: exit 0 under a hard 4 GiB virtual-memory limit, peak RSS 3,011,796 KiB (2.87 GiB), wall 30:44.61, no surviving process.
- CPU 15 restored online after collection; governor/EPP and boost recorded in the manifest.
- Parent-focused GCC self-release build completed. Fourteen parent-run CTests passed, including analyzer selftest, bounds, warmup exclusion, trace isolation, trace noninterference, telemetry-off rejection, engine, profile, partition, and path tests.
- `tests/audit_phase7_campaign.py` exits 0 and continues to report gate 1 and gate 2 FAIL from the current qualification campaign.
- Final diff check, archive SHA-256 verification, source/artifact hashes, and no-staged-files checks passed.

## Commits

- `f6796ce`: architecture request, corrected verdict, and nine raw evidence artifacts.
- `95beda2`: dedicated exact trace target, bounded analyzer/tests, accepted measured-only results, and rejected-attempt records.

## State and next decision

The authorized diagnostic is complete. The migration remains paused at performance qualification; no production behavior changed. The strongest next bounded experiment would be a compact node-backed exact evaluation index with fingerprint collision chains and equality verified against the arena node Board. It must preserve the complete evaluation-request and result sequence, use count ABBA first, and proceed to telemetry-off performance pairs only after exact invariants pass. That optimization is not authorized by this diagnostic and requires a new owner decision.
