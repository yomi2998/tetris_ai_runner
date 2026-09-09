# Session report, 2026-09-09: exact live-node evaluation-index trial

## Authorization and boundaries

The owner authorized a bounded trial of two exact live-node evaluation-index variants after the offline screen, followed by count identity, a non-binding selector, and at most one fresh three-pair comparison of the selected variant against the frozen legacy baseline.

The primary engineering target was candidate/legacy total and p95 medians at most 0.90. Results with both medians at most 0.95 required explicit fallback review. Existing migration qualification bars remained unchanged at 1.02. No production cutover, full qualification campaign, gate revision, quality campaign, or legacy deletion was authorized.

## Trial implementation

The implementation is compiled only for dedicated trial targets. Normal `tetris_profile_value` contains no index code and remains byte-identical to the frozen normal candidate at SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.

Both variants finalize the already-computed occupancy fingerprint with fmix and require exact 64-byte `Board` equality against a currently live arena node before returning an evaluation. Neither fingerprints nor tags establish equality.

- A: 262,144 direct 4-byte NodeId slots, 1,048,576 bytes.
- B: 65,536 direct 8-byte tag-plus-NodeId slots, 524,288 bytes.

Insertion occurs exactly once after non-merged materialization, with explicit root and reset-survivor repopulation. Merged or popped nodes cannot leave live-looking slots. Each lookup performs at most one exact Board comparison.

Fresh implementation review found and repaired three defects before evidence collection: insertion from generic `Engine::materialize` duplicated entries, the depth-zero pop-back merge path could leave a dead slot, and `eval_index_find` could compare an exact Board twice. Each variant then passed a 228-check suite covering reset, collisions, merges, telemetry, retained memory, exact mismatch, and tag short-circuit behavior.

## Count gate

The durable ABBA order was normal, A, A, normal, normal, B, B, normal. All rows used telemetry on, component timers off, 80 measured moves, seed 1, depth 6, 1,000 iterations, and 20 warmup moves.

Common semantic and work-vector counts across all eight rows were:

- Evaluation requests: 26,632,382.
- Parent-memo hits: 684,163.
- Index requests after memo: 25,948,219.
- Expanded parents: 599,166.
- Policy transitions: 26,632,382.
- Searches: 1,142,252.
- Materialized nodes: 25,757,157.

A recorded 12,220,062 hits, 13,728,157 misses/computations, 4,508,590 replacements, and 267,386,852 retained bytes. B recorded 10,695,122 hits, 15,253,097 misses/computations, 10,612,425 replacements, and 266,862,564 retained bytes. All 28 summary checks passed. Every unexpected non-timing difference count was zero, repeats were deterministic, and both designs exactly reproduced the offline screen.

## Frozen selector

The selector ran exactly four telemetry-off pairs in order A/B, B/A, B/A, A/B, with no appended pair or subset selection. B divided by A produced:

- Total ratios: 0.95927, 0.97866, 0.96373, 0.96848.
- p95 ratios: 0.92749, 0.95159, 0.93133, 0.94311.
- Total median 0.96611, spread 0.01939.
- p95 median 0.93722, spread 0.02410.

B met the frozen selector threshold of at least one percent total advantage, no more than two percent p95 regression, and spreads at most 0.04. The sole selector verdict was `SELECT-B`.

Two disclosed selector deviations had no observed validity effect. CPU 15 was online for 14 seconds between separate prewarm and timed offline windows, although it was offline for every prewarm and timed run. Timed commands spelled the parameter path relative to the fixed repository working directory rather than with the frozen absolute spelling; it resolved the same hash-verified file. The later legacy comparison corrected both deviations.

## Three-pair legacy comparison

The accepted comparison used one continuous CPU 15 offline window from before prewarm through all six timed runs. Commands used absolute artifact paths, telemetry off, CPU 7 pinned, governor and EPP `performance`, boost unchanged at 1, seed 1, depth 6, 200 measured moves, 20 warmup moves, and 1,000 iterations. Load gates passed, all exits were zero, CPU 15 was restored online, and no process survived.

Frozen artifacts were:

- Legacy baseline SHA-256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`.
- Selected B SHA-256 `e5c55131f9313ee9f7d5a1f3a508fc2efbbddfe225be56076f84b6ec095c41ce`.
- Parameters SHA-256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`.

The exact pair order was legacy/B, B/legacy, legacy/B. B divided by legacy produced:

- Total ratios: 1.15167, 1.13076, 1.14183.
- p95 ratios: 1.14210, 1.12348, 1.13635.
- Total median 1.14183, spread 0.02092.
- p95 median 1.13635, spread 0.01862.

Both spreads passed, but both medians exceeded 1.02. The terminal verdict is `NO-ADVANCE`. The tested index is about 14.2 percent slower than legacy in total time and 13.6 percent slower at p95. It misses the 0.90 target, the 0.95 fallback band, and the unchanged migration bars.

## Interpretation

The trace opportunity was real, but the tested representation converted fewer evaluations into worse wall time. Variant B avoided 10,695,122 of 25,948,219 post-memo computations, a 41.217 percent hit rate. Its modeled workload still performed 25,948,219 lookups, 25,757,237 live-node insertions, 10,612,425 replacements, and 10,847,908 exact Board comparisons. The added hash finalization, random slot and arena accesses, live checks, insertion traffic, and cache pressure outweighed the avoided arithmetic.

This also corrects an overly optimistic reading of the earlier 78 ns miss timer. That scope includes parent-memo fingerprinting and lookup as well as evaluation. An index hit does not remove the whole measured scope, and timing-derived savings from another instrumented run were never valid wall-speed proof.

The result rejects these two direct live-node layouts as the next migration mechanism. It does not contradict the exact reuse trace, and it does not prove every possible reuse architecture impossible. It does show that another standalone live-node index should not proceed without a materially different cost model.

## Recommended next bounded experiment

Stop the exact-index line and return to the architecture review's next ranked removable cost: duplicate Child-to-Node staging.

The smallest semantics-preserving trial is Child-only structure-of-arrays staging with aligned Boards separate from compact metadata. It should preserve child indices, ordering, transposition keys, parent-pop sequence, selected root child, and final path while avoiding repeated 192-byte aggregate writes and copies. The current 80-move evidence represents 5.113 GB of logical Child staging and 4.945 GB of logical Node records, while materialization-related scopes account for the largest remaining measured cluster.

Use the existing discipline:

1. Implement one isolated layout only, without combining key, frontier, policy, or work-volume changes.
2. Require count ABBA and exact candidate/order, parent-pop, selection, path, and result digests before timing.
3. Compare new versus the normal candidate with a frozen telemetry-off selector before consuming another legacy block.
4. Advance to legacy only for a clear current-candidate win large enough to plausibly reach the engineering target.
5. If staging fails, test direct-view transposition hashing/equality with once-per-source context next. Evaluate-and-safe row export fusion remains later because prior bulk and unrolled row variants regressed and the architecture review classified conversion as secondary.

This next experiment is not authorized by the completed index trial and requires an owner decision.

## Parent validation

The parent independently recomputed all selector and legacy ratios from unedited rows, including medians and spreads, and verified the terminal classifications. Row counts, profile versions, workload fields, telemetry mode, retained bytes, manifest order, absolute legacy commands, load values, and one-to-one row mapping passed.

External legacy, B, and parameter hashes were recomputed. Count binary and row hash manifests pass. Architecture-review raw hashes pass when checked from their recorded directory. The normal, A, and B build hashes remain exactly recorded. The campaign audit exits zero and continues to report gates 1 and 2 `FAIL`. CPU 15 is online, governor and EPP remain `performance`, boost remains 1, and no benchmark process survives.

The tracked diff after `a716223` contains only the frozen selector and legacy protocols and their evidence. Unrelated untracked research documents remain untouched.

## Commits and final state

- `a716223`: count-gated exact index trial implementation and evidence.
- `7f94be7`: frozen two-variant selector protocol.
- `d60c5bd`: accepted selector evidence with `SELECT-B`.
- `e8c92e7`: frozen three-pair legacy protocol.
- `8cd342d`: accepted legacy comparison evidence with `NO-ADVANCE`.

The exact-index trial is complete and stopped. Migration qualification remains blocked by performance. No production behavior changed, no full qualification follows, and no cutover or legacy deletion is authorized.
