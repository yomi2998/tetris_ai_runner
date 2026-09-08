# Performance gap decision request, 2026-09-08 evening

This document is a decision request, not a proposal. It records the full state of the fast-reachability migration at the end of the 2026-09-08 sessions, states the one unmet requirement, lays out the evidence, and asks the reader to decide what happens next. No option below is recommended over another. The questions at the end are deliberately open ended.

## The requirement still unmet

Phase 7 qualification (docs/fast_reachability_port_plan.md, gates at lines 1201 and 1202, restated in docs/phase7/qualification_protocol.md) requires, for seed 1 fixed-work mode on the pinned benchmark:

1. The median of five paired candidate-to-baseline total-time ratios is at most 1.02.
2. The median paired ratio for per-move p95 latency is at most 1.02.

The candidate is the migrated engine (PROFILE_V3 quiet output). The baseline is the frozen production binary `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`, sha256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`, PROFILE_V2, run with `--quiet-version 2`. The benchmark pins CPU 7, `artifacts/frozen_29d.bin` (sha256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`), seed 1, depth 6, 1000 iterations, warmup 20, telemetry off.

Everything else in the qualification program is either already green or downstream of this one bound. This is the only open blocker.

## Current measured standing

Measured tonight with the machine quiet and CPU 7 pinned to the performance governor (see "Measurement conditions" below), three interleaved pairs at 100 moves, telemetry off:

- Total-time ratio: 1.0324 (gate at most 1.02), remaining gap about 1.2 percent.
- Per-move p95 ratio: 1.0422 (gate at most 1.02), remaining gap about 2.2 percent.

In absolute seconds: production completes the 100-move run in about 8.99 s, the candidate in about 9.28 s. The p95 penalty exceeds the total penalty, so the heaviest moves are relatively slower than the average move.

The formal five-pair campaign has not been run yet; these three-pair interleaved measurements are the standing estimate that would feed it. Nothing has been staged, committed, frozen, or cut over.

## What is already proven green

Correctness and structural gates are fully satisfied on the current tree:

- `tetris_engine_tests`: 59,181 checks, 0 failures.
- `toj_policy_tests`: 71,906 checks, 0 failures, including 0-ULP frozen parity.
- `rule_differential`: 904,022 checks, 0 failures.
- `profile_value_tests`: 159 checks, 0 failures.
- All four of the above pass on all four configurations: GCC debug, GCC self-release, Clang debug, Clang self-release.
- Count ABBA (80 moves, telemetry on, timers off) against the predecessor binary produces bit-identical counters for every observable quantity, including `evals=26632382`, `materialized_nodes=25757157`, `parents=599166`, `promotions_refused`, and `pending_end_max=425068`. The migrated engine performs the identical sequence of searches, evaluations, materializations, promotions, and refusals as the retained predecessor.

So the gap is purely wall-clock speed. There is no correctness, determinism, or accounting question outstanding on the current tree.

## What the retained tree contains

The current tree (working directory, HEAD still `272026f`, nothing committed) carries the full retained optimization stack, each step gated on exact counters plus a binding off-mode ABBA win:

1. Rule-path geometry fusion with compile-time `detail::block_cells<B>` and once-per-source block dispatch.
2. Board compaction to 64 bytes.
3. O(1) child-link append for provably fresh children.
4. Default cache layout `Disabled`.
5. Side-height evaluate scan (0-ULP).
6. 192-byte Node and Child layouts.
7. 21-row transition extraction.
8. Known-lockout forwarding in transitions.
9. Prepared T distance.
10. Packed canonical sort key.
11. Direct Node construction from Child.
12. Recorder-hash gating.
13. Batch-8 transposition hashing.
14. Source-local dedup.
15. Eval-memo fingerprint prefilter.
16. Eval-memo SoA storage (90 logical bytes per slot).
17. 16-bucket MSD radix sort partition in `src/toj_rule.h`.
18. Eval-memo chain bucketing (64 buckets, Fibonacci hash, uint16 chains).
19. Evaluate micro-opts (direct row reads in `init_t_value`, `wide[10]`).
20. Pending-heap entry array: the pairing heap's links and cached values moved out of the 192-byte arena nodes into a fixed id-indexed 16-byte `PendingHeap::PendingEntry` array reserved to arena capacity; identical meld and pairing algorithm, so the pop sequence is unchanged; `arena_bytes_per_node` is now 192+4+16=212 and the derived capacity is 1,087,085.

Tonight's three retained steps measured, respectively: eval-memo chain bucketing 0.98363 off-mode; evaluate micro-opts 0.99139; pending-heap entry array 0.98771 off-mode under the powersave governor and 0.99427 under the pinned performance governor, with count ABBA 0.98374 and exact counters. Promote's profile share fell from about 7.1 percent to about 5.5 percent after the heap swap.

Current candidate binary: `/tmp/tetris_profile_value.heap-entries`, sha256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`. Predecessors this session: `memo-chain` (`39c378173e6d29ab042539574014cd73b8c73834e71a65ac664fc9f1c662cd78`), `eval-micro` (`24ad42399e28e47a02e6e78725401be06fac1f1ce400bd98e802d9f94f5872fb`).

## What was tried and rejected

Ten controlled experiments since the last retained win were rejected on binding measurements and reverted byte-identically (sha-verified after each revert):

| Experiment | Binding measurement | Outcome |
|---|---:|---|
| `[[gnu::always_inline]]` materialize plus size caching | 1.09573 off-mode | Forced inline bloated the hot path |
| Queue boundary `vector<bool>` to `vector<uint8_t>` | 1.00698 count | Bit extraction was already cheaper |
| `build_key` skeleton split with cursor-keyed cache | 1.00812 count | 130-byte skeleton copy beat the tiny loop |
| Dedup compare order swap, outcome first | 1.03785 count | Branchy compare added work before an early-exiting board compare |
| `[[gnu::always_inline]]` evaluate_once | 1.01002 off-mode | Count-mode gain did not transfer, same pattern as trusted-apply |
| Dedup fingerprint prefilter with shared fingerprint vector | 1.00181 off-mode | Neutral; board compare already early-exits |
| Node field reorder placing pending links in the value cache line | 1.02181 off-mode | Regression |
| Heap root-value cache | neutral | Rejected earlier |
| Trusted-enumerator apply | regressed | Rejected earlier |
| Row extraction exports, three variants | regressed | The compiler's vectorized fixed loops won in all three |

Three separate cases this session confirmed that count-mode (telemetry on) gains do not transfer to binding off-mode timing: trusted-apply (0.98 count, 1.00520 off), evaluate_once inline (0.979 count, 1.01002 off), dedup fingerprint (0.969 count, 1.00181 off). Count ABBA is therefore used only as the counter-equality gate, never as the performance gate.

## Why the remaining gap is hard

A fresh profile of the retained tree under the pinned governor (100 moves, off-mode, self-time percentages):

- `Policy::evaluate` 15.31
- `Engine::materialize` 12.40
- `expand_source_for_block` clones about 17.6 combined
- `Policy::transition_known_lockout` 6.75
- `Engine::promote` 5.46
- `Engine::build_key` 4.80
- `Engine::evaluate_once` 4.28
- `Engine::transposition_probe_prehashed` 4.06
- `Policy::scan_safe_rows` 3.68
- `enumerate_into_for_block` clones about 11.5 combined

Every one of these surfaces has been either optimized to a retained win or rejected in a controlled experiment. Evaluate is arithmetic the compiler already vectorizes; materialize is an irreducible Child to Node copy; the geometry and row-scan surfaces rejected every reformulation tried; build_key's skeleton split lost; the heap is now external-entry. No further exact, semantics-preserving source-level lever with meaningful expected value has been identified after ten consecutive controlled attempts.

## Measurement conditions, including tonight's machine fix

The machine is a desktop with an amd-pstate-epp driver. Until tonight the CPU 7 governor was `powersave`, letting the pinned core float between 3.02 and 5.39 GHz. That made interleaved blocks too noisy to trust: the frozen production baseline itself spread 9.04 to 9.54 s across blocks, and standing estimates swung between 1.033 and 1.066 for the same binaries.

Tonight CPU 7 was pinned to the `performance` governor (EPP `performance`). Back-to-back runs now agree within 0.7 percent, baseline blocks within about 1 percent, and the standing numbers above are reproducible. The powersave-era retain and revert decisions were made on tightly interleaved same-conditions pairs, so they remain sound; the pinned governor mainly stabilizes the candidate-versus-production standing measurements and any future formal campaign.

A fairness note for whoever decides: the frozen production baseline is a mature, plain implementation with large cache-hot nodes. The candidate's architecture (compact nodes, memo SoA, batched hashing, fused geometry) was chosen for the phase 8 world (smaller footprint, deterministic accounting, single header surface), and the retained stack has recovered most of the original gap. The last 1.2 percent of total time and 2.2 percent of p95 sit in code the compiler already handles well.

## What is downstream of this decision

Blocked entirely on the qualification bound:

- The formal five-pair qualification campaign and re-freeze of the candidate artifacts.
- Production cutover: profile harness, tuner, match, DLL and CMake switches.
- Phase 8 deletion of the legacy implementation.

Partially independent work that can proceed regardless, but is queued behind the current focus:

- Repairing `tests/audit_phase7_campaign.py` so it recognizes re-freeze, partition, and `results/phase7/gate89/` evidence without modifying frozen artifacts (it currently reports stale failures, including a campaign-artifact hash mismatch against the current working binary).
- Adding the explicit fused-rule boundary and public layout tests requested by the independent reviewer, accounting for the now-dead `pending_child` and `pending_sibling` node fields and the new 16-byte pending entry layout.
- Formal adjudication of the remaining findings from the original audit reports.

## Options on the table

These are the paths identified so far. They are presented neutrally; the list is not exhaustive and the reader is free to define a different one.

1. Profile-guided optimization on the candidate side. Likely clears the gap. Considerations: training on the gate workload itself would be methodologically circular, so a differently seeded or differently shaped training corpus would be needed; it changes the production build pipeline (two-stage builds, profile artifacts) and raises a Clang parity question; the plan pins the preset name rather than the flag string, so whether PGO is in scope for the phase 7 gate is itself a protocol question.
2. Revising the bound. Raising the acceptance ratio (for example to 1.05) or scoping it differently (total only, or workload-specific) would let qualification, cutover, and phase 8 proceed on the current tree. Considerations: it is a protocol change to a bound that exists to prove no player-visible regression; the measured 1.0324/1.0422 would need to be documented as the accepted standing; the p95 asymmetry means the heaviest moves carry the largest relative penalty, which is worth weighing if player experience under load matters.
3. Continued bounded source-level search. Considerations: nine of the last ten controlled attempts were rejected; expected value is low; each attempt costs an experiment cycle (build, count ABBA, off-mode ABBA, revert if rejected). Candidate ideas that remain untried are progressively more speculative: probe-chain prefetch variants, a vectorized scan_safe_rows reformulation (data-dependent early break makes this risky), further build_key variants.
4. Re-examining the comparison itself. Considerations: the baseline is a frozen binary built from the pre-migration tree; a reader could question whether the 1.02 bound was calibrated against this machine, this governor state, or a different workload mix, and whether re-measuring or re-freezing the baseline under today's pinned controls (without touching frozen artifacts, e.g. a rebuild-and-compare study) changes the standing. This path is about measurement policy, not code.
5. Pausing the campaign. Recording the standing, the exhausted-surface analysis, and the decision request, then resuming later, possibly on different hardware or after the phase 8 design priorities are re-weighed. Considerations: correctness is already green, so nothing decays except context; the working tree carries uncommitted optimization work that would need committing or shelving.
6. Any combination or alternative the reader sees. For example: PGO with a clean corpus plus a gate-protocol amendment documenting the build change; or accepting the current tree under a revised bound while separately funding a follow-up performance workstream post-cutover.

## Questions for the reader

These are the open questions. They are asked, not answered, anywhere in this document or in the session records.

1. Is the 1.02 total and p95 bound, as written, still the requirement you want fulfilled? If not, what bound or form of bound do you want, and should the p95 leg be treated differently from the total leg given the measured asymmetry?
2. Which path do you want taken toward fulfilling the requirement: PGO, a protocol revision, further bounded search, a re-examination of the comparison methodology, a pause, or something else entirely?
3. If PGO: what training corpus is acceptable, does the production pipeline adopt two-stage builds permanently, and is Clang parity required for the phase 7 gate or only for later phases?
4. If the bound is revised: what evidence must accompany the revision (the five-pair campaign under pinned controls, the exhausted-surface analysis, or something more), and should the revision be permanent or revisited after phase 8?
5. If search continues: how many more controlled experiment cycles are worth funding given the observed hit rate, and are speculative surfaces (probe prefetch, vectorized row scans) in scope?
6. Should the measurement policy itself be pinned in the protocol now that the governor effect is quantified (performance governor for all future qualification runs), so that all future ratios are comparable to tonight's numbers?
7. Is there any consideration outside this document, for example product timing, other consumers of the engine, or hardware you care about, that should reshape the decision?

## Reproducing the standing measurement

From the repository root, with CPU 7 pinned to the performance governor:

```bash
BASE=/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline
PARAM="$PWD/artifacts/frozen_29d.bin"
for spec in "prod 2" "cand 3"; do
  set -- $spec
  if [ "$1" = prod ]; then BIN=$BASE; else BIN=/tmp/tetris_profile_value.heap-entries; fi
  env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$BIN" \
    --warmup-moves 20 --moves 100 --iters 1000 --seed 1 --maxdepth 6 \
    --param-file "$PARAM" --telemetry off --quiet --quiet-version "$2"
done
```

Interleave at least three pairs and compare medians of `total_s` and `p95_ms`. The count twin (counter equality) uses `--moves 80 --iters 1000 --telemetry on --timers off` against the predecessor binary.

## State of the tree and records

- HEAD `272026f`, nothing staged or committed. Tracked modifications: `src/tetris_board.h`, `src/tetris_engine.cpp`, `src/tetris_engine.h`, `src/toj_policy.cpp`, `src/toj_policy.h`, `src/toj_rule.h`, `tests/tetris_engine_tests.cpp`.
- Untracked: this document, `docs/phase7/session_report_2026-09-08_afternoon.md`, and unrelated research documents left untouched.
- Session records: `docs/phase7/session_report_2026-09-08.md`, `docs/phase7/session_report_2026-09-08_afternoon.md`, `docs/phase7/handoff_astra_2026-09-07.md`, `docs/phase7/qualification_protocol.md`, `docs/phase7/hotspot_attribution_f.md`.
- Advisory reports from the independent review workflows remain under the subagent artifact directories and are advisory only.
