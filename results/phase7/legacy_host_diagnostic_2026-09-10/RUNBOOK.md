# Bounded legacy-host diagnostic runbook

Implements section 5 of `docs/phase7/decision_request_2026-09-09.md` at commit `b0dcc28`.
Diagnostic directory: `results/phase7/legacy_host_diagnostic_2026-09-10/`.

## 1. Scope and authority

This is a bounded, test-only diagnostic. It asks one question: can the fast reachability enumerator retain its advantage when consumed by legacy engine dataflow, and which costs prevent that advantage in the value engine?

It is not implementation of a production engine, not a qualification campaign, and not a gate change. Production sources keep their behavior. The legacy baseline, the normal value candidate, all frozen artifacts, all parameters, and all qualification thresholds stay unchanged. No cutover, legacy deletion, permanent second engine, parameter retuning, or gate relaxation follows from this runbook.

Legacy timing here is authorized by the owner directive to proceed with the diagnostic-first plan, and for this diagnostic only, under the existing holdout discipline. One legacy timing block is consumed: the Step 3 arm `L0` plus the frozen baseline anchor `B`. No other legacy block is authorized, and no conclusion here permits a new legacy block without separate owner approval.

Binding versus non-binding labels are mandatory throughout. Step 1 and Step 2 timings are diagnostic and non-binding. Only Step 3 selector outcomes feed the architecture decision, and even those are diagnostic screening, not qualification.

## 2. Arm labels

| Label | Meaning |
|---|---|
| `B` | Frozen legacy baseline binary, unmodified, absolute reference anchor only |
| `L0` | Diagnostic driver with the legacy `search_tspin::Search` enumerator and the legacy hash-only evaluation cache enabled |
| `L1` | Diagnostic driver with the fast-reachability adapter enumerator and the legacy hash-only cache enabled |
| `L2` | Diagnostic driver with the fast-reachability adapter enumerator and the legacy evaluation cache disabled, the exactness control |
| `V` | Value engine production enumeration and materialization pipeline, driven over the same frozen parents |

`L0`, `L1`, and `L2` are the same binary. Only flags differ, so the enumerator and the cache contract are the only variables. `B` stays untouched so its provenance remains the frozen hash.

## 3. Frozen inputs and provenance

Verify every hash before any execution. A mismatch is `BLOCKED`.

- Decision request: `docs/phase7/decision_request_2026-09-09.md` at commit `b0dcc28`.
- Legacy baseline: `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`, SHA-256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`, read-only.
- Legacy comparator (count parity only, not timed here): `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp`, SHA-256 `08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450`.
- Normal value candidate: `/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_value`, SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.
- Parameters: `/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`, SHA-256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`.
- Partition recorder (value side): `/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition`, SHA-256 `5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1`.

Frozen parent-input replay corpus, harvested from the real workload and already accepted:
`/home/icly/Documents/tetris_ai_runner_results/phase7/row_export_fusion_trial_2026-09-09/trace/trace_normal.inputs.bin`,
size 64365916 bytes, SHA-256 `c6b5ecf6c2b7e956b7770ea957820d77fefc4fcaad0a9e1be22a55d1d1473da5`, mode read-only, produced by the normal candidate's partition recorder on the workload seed 1, warmup 0, 20 moves, depth 6, 1000 iterations, which recorded `telemetry=on`, `timers=on`, `evals=6132302`, and `parents=145544`, and accepted as semantic evidence at `2c75c94`. It records, per first-observed expansion input in engine order: board occupancy words, piece, source, multiplicity, and the ordered candidate list with arrival, apply, spin, clear, lockout, survivor, and `result_hash40`. Format: `tests/partition_format.h`, magic `PARTV1`.

Matching sidecars, same directory and same accepted gate: `trace_normal.moves.tsv` (`723c2c83ef6de336ef8750d76fec7198cdea2b94545f8d10ad4badfc4d7c674b`), `trace_normal.run_totals.tsv` (`46934e5f13682b02e1df729c26c86d0f2c74f599f4e6136b3610ce5b3c374785`).

Legacy-compatible subcorpus: 33 boards, 231 cases, piece outer and board inner, from `tests/reach_corpus.h` (`legacy_subcorpus_boards = 33`). Report its coverage limit explicitly: it is a 40-row legacy-domain corpus and cannot establish full 48-row target compatibility.

Recorded diagnostic rows used to reconcile stage scopes, all read-only evidence:
`results/phase7/architecture_review_2026-09-09/raw/architecture-review-2026-09-09-candidate-timers.txt`
and `...-legacy-cmp-timers.txt` (candidate enumeration about 1087 ns per call versus legacy about 1751 ns, ratio 0.621, full measured enumeration scopes, instrumentation perturbed);
`results/phase7/gate89/summary.md` (corpus ratios 0.0762 raw T and 0.2399 worst non-T per parent, wrapper not measured);
`docs/phase7/hotspot_attribution_f.md` (wrapper 2912 ns per search on 2855528 searches).
Build flags for every rebuilt target: preset `build-linux-gcc-self-release`, `-O3 -march=native -flto=2`, compiler g++ 16.2.1. Any target rebuilt from the diagnostic HEAD must be hashed and recorded; recorded gate89 binary hashes are stale and must not be reused as current provenance.

### 3.1 Seam verification record

`results/phase7/legacy_host_diagnostic_2026-09-10/seam-verification.md` is an existing reviewed input beside this runbook; keep it current if implementation contradicts it. It records the seams with file and line evidence. Summary of the confirmed seams:

- `TetrisEngine` is a template over rule, AI, and search type: `src/tetris_core.h:2148-2149`.
- Legacy profile already injects a search wrapper: `ProfiledSearch : search_tspin::Search` at `src/tetris_profile.cpp:60`, proving the substitution seam.
- Pose-node resolution exists: `TetrisContext::get(TetrisBlockStatus const &)` at `src/tetris_core.h:345` and `:2242`.
- Coordinate inverse exists: `ExternalPoseTransform::to_legacy` at `src/toj_rule.h:91`.
- Arrival and classification flags are separate from the pose pointer: `search_tspin::Search::TetrisNodeWithTSpinType` at `src/search_tspin.h:29-35`, consumed by `ai_zzz.h:17`.
- Production enumeration entry hardcodes the spawn pose and takes no start argument: `detail::enumerate_into_for_block(Board const &, Piece, MovementConfig, std::span<Candidate>)` at `src/toj_rule.h:334-335`, called at `src/tetris_engine.cpp:1067` and `:1325`.
- Kernel accepts an explicit start and rotation: `binary_bfs(board_t, search_config const &, coord start, unsigned init_rot, ...)` at `src/fast-reachability/search.hpp:618` and the workspace overload at `:637`; seeding clamps the start row at `:429` and height-cut handling appears at `:99-115`.
- Canonicalization uses a thread-local scratch vector: `src/toj_rule.h:343-348`.
- Legacy path generation signature takes a pose node plus land point: `src/search_tspin.h:75-79`.
- Legacy evaluation table and result retention contract: `TranspositionTable` at `src/tetris_core.h:649`, used through `src/tetris_core.h:982`, `:1085`, `:1102`, and `TetrisCore::eval` at `:828` with the store at `:857`. The slot stores the result; consumers must not hold pointers across further calls. Verify and record whether any consumer retains a table pointer; if it does, the `L1` arm must copy and must not rely on slot stability.
- Legacy `memory_usage()` is an internal counter only: `src/tetris_core.h:2268`. It must not be the sole memory evidence.

If any recorded seam proves false during implementation, stop Step 2 and report the failing seam instead of adapting silently.

## 4. Prerequisite gates

All must pass and be captured in `validation/prerequisites_<date>.txt` before Step 1, and be re-verified unchanged immediately before Step 3, with hashes of every binary involved.

1. Accepted suites pass on the current HEAD under GCC debug, GCC self-release, Clang self-release, and the sanitizer build: `tetris_engine_tests`, `tetris_board_tests`, `rule_differential`, `path_differential`, `toj_policy_tests`, `fast_reachability_tests`, `fast_reachability_perft`, `profile_value_tests`, `legacy_cmp_tests`, `candidate_partition_selftest`, and the four accepted trial suites `child_soa_trial_tests` (267 checks), `direct_key_trial_tests` (75722 checks), `row_fusion_trial_tests` (396598 checks), plus `eval_index_trial_a_tests` and `eval_index_trial_b_tests`.
2. The normal value candidate remains byte-identical to `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`, and `nm -C` on it reports zero diagnostic adapter symbols.
3. `tests/audit_phase7_campaign.py` still exits zero and still reports gates 1 and 2 `FAIL`; this diagnostic may not alter that record.
4. The read-only replay corpus files of section 3 verify by hash and size before and after every use.
5. CPU 7 governor `performance`, CPU 7 energy performance preference `performance`, global boost `1` unchanged, CPU 15 initially online, noninteractive write authority for the CPU 15 control proven before evidence creation.

## 5. Temporary architecture exceptions

Each is confined to diagnostic targets compiled with `TETRIS_LEGACY_HOST_DIAG`. Production behavior, production targets, and every frozen artifact stay unchanged. Anything outside this list is out of scope and is a stop condition.

- E1. A legacy-side translation unit may include value-side reachability and rule headers (`src/toj_rule.h`, `src/fast-reachability/*`), temporarily relaxing the plan section 5 dependency direction.
- E2. A test-only supplied-start enumeration helper is permitted. Preferred form: implement it inside the diagnostic target by calling `binary_bfs` directly and reusing the canonical key and comparator, then prove parity by ordered-digest equality against production `enumerate_into_for_block` on the frozen corpus. Only if the needed symbols are unreachable may a narrow `TETRIS_LEGACY_HOST_DIAG`-guarded helper be added to `src/toj_rule.h`, and the normal binary must stay byte-identical.
- E3. The adapter arm may replace legacy `make_path` with the value pathfinder (`src/toj_pathfinder.h`). It may not fabricate predecessor pointers to satisfy the legacy interface.
- E4. The adapter arm may report legacy-domain-unmappable poses as explicit counted categories. Silent truncation to the 40-row domain is prohibited.
- E5. The diagnostic driver may link both engine sources into one binary for arm selection by flag. This does not create a dual-engine production design.
- E6. The legacy hash-only evaluation cache may be enabled in `L1` as an explicitly labeled diagnostic control only. It is not a target cache contract and must not be reported as compliant.
- E7. The diagnostic driver may emit additional non-binding rows with its own version token and may report process memory from `/proc/self/status`. The frozen `B` binary is never modified; its memory is measured externally with `/usr/bin/time -v` at most three anchor runs, labeled as external measurement.
- E8. Test-only counters may count work in any arm. No new field may enter `PROFILE_V3` of any accepted artifact, and no accepted artifact may be rebuilt under a changed hash.

## 6. Step 1: stage-cost table (non-binding)

New test-only target: `legacy_stage_bench`. Build it with the diagnostic macro, run it under the sanitizer build for the digest checks and under the self-release build for timings. It is a microbenchmark: labels must read `DIAGNOSTIC NON-BINDING` on every row it prints.

### 6.1 Corpora

- C1: the 33-board legacy-compatible subcorpus, 231 cases.
- C2: the frozen parent-input replay corpus of section 3, deduplicated to distinct boards with recorded multiplicities, plus its per-piece and per-source partition. All arms consume identical boards, pieces, start poses, rotation and drop flags, and the same frozen parameters where a policy is involved.

### 6.2 Stages

Report each stage separately, per arm, per corpus, per piece, and per board class:

- S1 prepacked raw BFS. Boards prepared outside the timed span. Every `binary_bfs` result must be observable through an accumulator that depends on all landing bits, using the existing per-rep bit-shift sink pattern in `tests/raw_bench_main.cpp:142-163`, and the extraction or checksum pass reported as its own line rather than folded into S1. Also report S1x, the preparation pass alone, outside the span, so no preparation cost is hidden and none is double counted.
- S2 workspace preparation plus arrival-aware search plus landing extraction, matching `src/arrival_candidates.cpp` scope, reported without candidate normalization.
- S3 complete production candidate enumeration including ordered canonicalization and deduplication, which is the scope behind the recorded 2912 ns per search wrapper figure.
- S4 legacy search with the `TetrisMap` import and metadata rebuild outside the timed span, plus S4i, the import-inclusive variant, reported as a separate line so neither absorbs the other. The recorded gate89 legacy timings correspond to S4i, not S4.
- S5 adapter conversion: S3 output plus coordinate conversion through `ExternalPoseTransform::to_legacy` plus pose-node resolution through `TetrisContext::get`, and, in a separately reported subline, the unmappable-pose count.

### 6.3 Method

- Batch timing only. Never wrap a single sub-50-ns call in a clock read. Batch sizes: 200000 drives for S1 and S1x, 50000 for S2, S3, S4, S4i, and S5. Fixed batch counts; no extension.
- Repetitions: 3 discarded warmup batches, then 7 timed batches per cell, reported as median, minimum, and maximum. Fixed counts, no additional batches even on visible drift; drift is reported instead.
- Per-call cost equals batch nanoseconds divided by drives. Report drives, aggregate stage nanoseconds, and per-call nanoseconds.
- Hoisting control: the S1 sink digest must be nonzero, must equal the untimed digest pass, and must be identical across arms that run the same enumerator. A deliberately poisoned board must change the digest. Any digest failure is `FAIL`, not a retry.
- Output digests: ordered candidate digest, occupancy digest per candidate, arrival-class digest, and selected-best digest, printed per cell and hashed into `step1/digests.sha256`.
- Distributions: per piece `I J L O S T Z`; per board class, defined as (a) fully legacy-domain mappable, (b) unmappable pose or upper-row occupancy, (c) T with terminal-rotation arrival present, (d) blocked spawn, (e) density decile by occupied cell count. Report counts and medians per class, never aggregate means alone.

### 6.4 Step 1 deliverable

`step1/stage_table.tsv`, `step1/digests.sha256`, `step1/MACHINE.md`, `step1/summary.json`, and `step1/REPORT.md` containing the cost table from input preparation through canonical candidates, the reconciled count partition against the accepted trace totals, and an explicit statement that tree performance is not inferred from any raw BFS ratio.

Step 1 has no pass or fail threshold. It produces the budget used to interpret Step 3. Any attempt to cite Step 1 numbers as engine speed is out of scope.

## 7. Step 2: one adapter target and feasibility gates

New test-only target: `legacy_fast_adapter_bench`, one binary providing arms `L0`, `L1`, `L2`, and `V` by flag, with the frozen `B` binary kept separate and unmodified. It reuses the legacy profile move loop structure rather than inventing a new engine: same parameter load, same tree, policy, allocation, widening, and hold handling in all arms, with only the enumerator and cache flags changing. The driver implements two input modes, both explicit flags: a matched-parent replay mode consuming the frozen corpus for the binding block, and a self-play profile mode for the `L1` absolute anchor runs using the standard protocol workload. It builds the non-SoA engine variant (`#ifndef TETRIS_CHILD_SOA_TRIAL`), which is what the normal candidate uses.

### 7.1 Adapter requirements

- Supplied-start enumeration entry per exception E2, with the original pose validated. Full-height search is used initially. A cropped or height-cut search is permitted only after equivalence is proven for that exact start and rotation in the parity test, and never by silently replacing a live pose with a different seed.
- Reuse the current canonicalization keys, comparator, and first-survivor behavior. No second scalar reachability pass. No per-candidate pathfinding.
- Convert each parent board once per source (current and hold) and cache the conversion with explicit invalidation; the invalidation and any conversion count must be reported.
- Map each in-scope canonical candidate to a stable existing pose node through `TetrisContext::get`, not a newly built graph and not a per-landing geometry allocation.
- Replace legacy path generation with the value pathfinder per exception E3.
- Report unmappable poses, spawn obstruction, hold and queue boundaries, both 180 settings, and every hard stop as counted categories.

### 7.2 Feasibility gates, all required before any Step 3 timing

1. Every in-scope canonical candidate maps to identical occupied cells and preserves its arrival class through child creation, node reuse, and same-piece hold deduplication.
2. Shared placements produce identical board, line-clear, spin, policy, and lockout outcomes across `L0`, `L1`, and `V`, with changed target semantics explicitly partitioned rather than presented as legacy parity.
3. Newly found placements and both T arrival classes survive end to end. Path generation and replay must succeed for each; a replay failure is a hard stop, never an implicit straight drop. An empty pre-lock command sequence is acceptable only when replay proves the selected placement and arrival.
4. Directed fixtures exercise arbitrary active poses, blocked spawn, hold and queue boundaries, both 180 settings, and upper-row and domain failures, through both the production dataflow and independent scalar replay.
5. Memory accounting is complete: `VmHWM` and `VmRSS` from `/proc/self/status`, legacy `memory_usage()`, adapter workspace bytes, thread-local scratch capacity, cache storage bytes, and both board representations held simultaneously. Total retained must be at most `268435456` bytes with at least `65536` bytes of margin for any arm proposed as a migration candidate; an arm exceeding this loses migration-candidate eligibility and is recorded as such, while the diagnostic may still proceed with that arm's data for attribution, provided process viability is not at risk.
6. The sanitizer build is clean for every arm.
7. Parity: the digest identity checks of section 6.3 pass on the replay corpus for `L1` versus `V` ordered candidates within the shared domain, and `L1` versus `L2` differ only in cache-work counters.

Any failure is terminal `FAIL` with the exact counterexample preserved. Do not time an implementation that fails a feasibility gate.

### 7.3 Stop-if-broad-rewrite rule

Source changes are limited to: the two new diagnostic targets and their test files, one supplied-start or canonicalization helper per exception E2, and narrowly scoped test-only identity, policy, and path integration. If preserving target behavior requires a broad tree rewrite, stop, report the failing seam with file and line, and return conclusion C4 of section 9. Do not widen the experiment. A common-domain-only adapter is a valid partial result, not a migration candidate.

## 8. Step 3: frozen three-configuration comparison (binding for the architecture decision)

One collector script, `step3/collect.sh`, executed exactly once, fail-closed, following the discipline established by the accepted count and selector runbooks.

### 8.1 Workload and commands

All commands use absolute paths, `env -u TETRIS_AI_PARAM_FILE`, `taskset --cpu-list 7`, seed 1, and the frozen parameter path.

- Matched-parent replay arms (`L0`, `L1`, `L2`, `V`): the driver consumes the frozen `trace_normal.inputs.bin` corpus of section 3 and executes the full per-parent pipeline: enumeration, conversion where applicable, candidate application, policy and evaluation with the arm's cache contract, child and node materialization, heap insert, and one selected-best extraction per parent. Each run prints one `DIAG_V1` row with total seconds, per-stage totals, work counters, memory fields, and the digest fields of section 6.3. Telemetry counters may be on for work accounting, but every timing number must come from a run where per-item component timers are off.
- Absolute anchors (`B`, plus `V` and `L1` full-profile references): the frozen baseline and the value candidate run the standard protocol at seed 1, warmup 20, 200 moves, depth 6, 1000 iterations, `--quiet --quiet-version 2` for `B` with `--telemetry off`, and `--quiet --quiet-version 3 --telemetry off --timers off` for `V`. These arms take different moves from each other, so they are not matched; they are reported as absolute reference only and partitioned per section 8.4.

### 8.2 Fixed schedule and budget

Prewarm: one discarded run per distinct binary, four total, printed with exit status only.

Binding matched-parent block, 6 rounds, each round contains `L0`, `L1`, `L2`, `V` in this frozen order:

| Round | Order |
|---|---|
| 1 | L0, L1, L2, V |
| 2 | L1, V, L0, L2 |
| 3 | V, L0, L2, L1 |
| 4 | L0, V, L1, L2 |
| 5 | L1, L2, V, L0 |
| 6 | V, L2, L0, L1 |

That is exactly 24 binding timed runs. Absolute anchors add exactly 9 runs: `B` 3, `V` 3, `L1` 3, at fixed positions after rounds 2, 4, and 6. Total timed runs: 33. Total commands including prewarm: 37.

Budget is fixed. No appended round, no replacement run, no additional batch, no retry after a discarded outlier, and no threshold change after seeing results. If the block cannot complete, it is `ABORTED` with partial evidence preserved.

### 8.3 Machine controls and gates

- CPU 7 pinned, governor `performance`, energy performance preference `performance`, boost `1` unchanged.
- Five-minute load average below 1.5 before prewarm, initial read plus at most ten one-minute rechecks. Failure is `BLOCKED` with no run.
- CPU 15 is taken offline once before prewarm and stays continuously offline through prewarm and all 33 timed runs, verified `0` at every command boundary, and restored to `1` on success and on every abort or error path.
- Load5 at least 2.0 immediately before a not-yet-started round permits at most five one-minute rechecks and continuation only below 1.5. Load5 at least 2.0 inside a started round, or any CPU 15 state failure, is `ABORTED`.
- Any nonzero exit, malformed row, nonempty stderr, digest mismatch inside a repeat, or feasibility-gate regression is `ABORTED`.

### 8.4 Identity and difference requirements

Expected identical, per round, across `L1` and `L2`: cache-independent quantities only, namely candidate count, ordered candidate digest, occupancy and arrival digests, and application and outcome counters on the shared domain. The driver must pin and report the legacy transposition table lifetime per depth (`tt[depth]`) and include a hash-collision audit; because the legacy table verifies hashes only and a hit can alias a different board sharing the hash, evaluation results and the selected-best digest are expected to diverge between `L1` and `L2` whenever a collision fires. That divergence is the measured E2 exactness signal: count and audit it against `L2`'s fresh evaluations, report it, and never abort on it.

Expected identical across repeat rounds within one arm: every non-timing field. Any non-timing difference is `ABORTED`. Note the rotating round order does not balance arm positions evenly across the six rounds; within-round pairing is the drift control, and the imbalance is acknowledged as a design limit.

`L0` versus `L1` is not an identity comparison. Report and partition the intended work difference: shared-domain candidates, `L1`-only candidates, arrival classes, clear, spin, policy, lockout, and outcome digests for the shared subset, using the accepted partition machinery in `tests/candidate_partition.cpp` modes `drive` and `classify` against the same corpus. Demanding equality with `L0` or treating the extra placements as free is prohibited.

`V` versus `L1` on the shared domain must be digest-identical; a difference there is a `FAIL` of the Step 2 gate 7 carried into Step 3, not a finding to be averaged.

For the absolute anchors, report each arm's own work counts, candidate volume, evaluation computations, cache work, materialization counts, and selected moves, and state explicitly that move divergence means the trajectories differ.

### 8.5 Screens, pre-declared before execution

Ratios are within-round, so order drift cancels. Display at least five decimals and keep full precision for comparisons; median of six sorted values is the mean of the third and fourth.

- E1, adapter versus legacy engine dataflow: `L1` divided by `L0` for total and for each stage total. Screen: total median at most `0.98` and every stage median at most `1.00`.
- E2, exactness and contract cost: `L1` divided by `L2`. Screen: total median at most `1.02`, computed on rounds without fired collisions, plus the collision-audit count and the divergent-evaluation audit against `L2` fresh results. Above the threshold, or with divergence fully explaining the E1 win, the labeled hash-only control is carrying the result.
- E3, value engine versus adapter: `L1` divided by `V`, reported as a difference measure with no threshold, since the arms differ in intended work.
- E4, absolute anchor: each of `B`, `V`, and `L1` against its own repeat median, with spreads, reported for context only.
- Spreads: report every screen's spread. A spread above `0.04` on E1 or E2 is recorded as unresolved and must be reported as such; it is not silently reinterpreted, and no extra round may be run to fix it.

Evidence files: `step3/collect.sh`, `step3/collect.log`, 33 unedited rows, all stderr files, `step3/rows.sha256`, `step3/digests.sha256`, `step3/binaries.sha256`, `step3/commands.txt` with all 37 commands in full absolute spelling, `step3/MACHINE.md`, `step3/MANIFEST.txt`, `step3/summary.json`, and `step3/verdict.md`. Hash every artifact, make external outputs read-only, and record the label mapping if any template label survives.

## 9. Step 4: stop and report

Write `step4/REPORT.md` with the Step 1 cost table, the Step 2 feasibility and memory results, the Step 3 ratios and screens, the partitioned work differences, all controls and anomalies, and exactly one terminal conclusion:

- C1 adapter wins with target-compatible semantics and exactness: E1 passes, E2 passes, and every Step 2 gate passed. Route to option B (port the demonstrated dataflow improvement back into the value engine under separate authorization) or option C (explicit production architecture amendment). Neither is automatic and neither is authorized here.
- C2 only the restricted domain or the hash-only control wins: E1 passes but E2 fails, or the win exists only on the 40-row common domain. Report the contract cost and remaining compatibility work, and route to option C with measured numbers or to option D. Do not claim the final target is faster.
- C3 adapter loses: E1 fails. Identify which of conversion, canonicalization, extra work, or tree integration consumes the saving, using the Step 1 stage table as the budget. This rejects this adapter, not all possible reuse, and routes to option B with the stage budget or to option D.
- C4 feasibility gate failure: no timing was taken. Preserve the exact counterexample and stop; route to option C or option D.

Report the outcome to the owner. Stop in every branch. No cutover, no legacy deletion, no permanent second engine, no parameter retuning, no gate relaxation, and no second optimization program follows automatically.

## 10. Fail-closed preservation rules

- Each collector executes exactly once and refuses to start if its own `collect.log`, any retained row file, or any external output already exists. Presence of prior outputs is `BLOCKED`; never remove, truncate, rename over, or replace evidence.
- Every generated file is preserved on every terminal path, including `BLOCKED` and `ABORTED`. A `BLOCKED` terminal must run nothing.
- If an infrastructure error occurs after collection completed, resume metadata finalization only and execute no benchmark command again.
- The restoration trap must restore CPU 15 on all paths and must report its own result.
- No row may be dropped as an outlier. No pair or round may be appended, replaced, or selectively retained, and no favorable subset may be chosen.
- Parent must independently re-verify all hashes, all digests, all row fields, the arithmetic and screens at high precision, the CPU 15 state, the absence of surviving processes, and the clean tracked state, and must obtain a fresh read-only review before any conclusion is accepted.

## 11. Budget summary

| Item | Fixed count |
|---|---|
| New test-only targets | 2 |
| Step 1 timed batches | 7 per cell, 3 warmup discarded, cells enumerated in `step1/cells.tsv`, no extension |
| Step 3 prewarm runs | 4, discarded |
| Step 3 binding timed runs | 24 |
| Step 3 absolute anchor runs | 9 |
| Step 3 total timed runs | 33 |
| Step 3 total commands | 37 |
| Legacy timing blocks consumed | 1, arms `L0` and `B`, authorized for this diagnostic only |
| Collector invocations | 1 per step, never twice |

## 12. Claim discipline

Every Step 1 and Step 2 number must carry `DIAGNOSTIC NON-BINDING`. Permitted claims are stage cost attribution, feasibility, memory, and the Step 3 within-round ratios. Prohibited claims: qualification, migration acceptance, production readiness, legacy equivalence at engine level, superiority over legacy, any quality statement, and any inference of end-to-end speed from a raw BFS ratio. Only Step 3 outcomes feed the section 9 conclusion, and only owner action changes production.
