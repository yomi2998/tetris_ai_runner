# Decision request: fast-reachability migration, performance gate unresolved

Date: 2026-09-09.
Status: decision required.
Prepared after the executed optimization program ended per the owner's sequential-attempts directive.
Review update: 2026-09-10. The owner requested a diagnostic-prototype-first plan and a documentation-only revision. Implementation, new benchmark campaigns, production reuse, and gate changes are not authorized by this revision.

## 1. What we are trying to achieve, and what happened

### The goal

Replace the production Tetris AI engine's pointer-network reachability stack with a clean engine built on the pinned fast-reachability kernel (`src/fast-reachability/`, commit `0c35e13`). The plan is `docs/fast_reachability_port_plan.md`. The new engine must preserve the exported ABI, path commands, and TOJ behavior, and must satisfy:

1. Performance: no more than 2 percent slower than the frozen legacy baseline under the frozen profile protocol (plan item 9).
2. Quality: non-inferiority with a 3 percentage point WR margin and 3 percent relative APP/APL margins over 1,000 seat-swapped pairs (plan item 10). These are tolerated regressions, not required improvements.

### The legacy side, anchored

- Frozen legacy baseline binary:
  `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`
  SHA-256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`.
- Legacy comparator binary used for count parity: SHA-256 `08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450`.
- The legacy engine stays in production and byte-frozen throughout; nothing in this program modified it.

### What happened

The new engine has substantial correctness evidence: a 16,236-check engine regression and green replay and corpus differentials. Gates 8 and 9 are supported by recorded corpus evidence. Gate 3 remains pending on the current candidate; the validated partition bundle concerns a superseded build. Gates 4 through 6 and the comparator-bound result remain historical, not current passes. Gameplay quality and full qualification remain incomplete. See `docs/phase7/session_report_2026-09-09.md` and `tests/audit_phase7_campaign.py`.

- Binding gate rerun (`5809409`, report `b3edf56`): candidate/legacy total median ratio 1.04155, p95 1.02547 against the 1.02 bar. Gates 1 and 2 FAIL. Total/p95 ratio spreads are 0.03706/0.06492; the p95 spread also exceeds the runbook's 0.04 advancement limit.
- The architecture review (`f6796ce` request, `7b9f8c5` verdict) identifies measured work asymmetries, not a proven architectural ceiling. On its 80-move workload, the candidate computes 25,948,219 evaluations versus legacy's 5,576,912, a 4.653x multiplier. Its parent-local exact memo hits 2.569 percent of requests; legacy's broader, hash-only table hits 75.508 percent.
- Canonical outputs are 32,446,394 versus 23,089,552, up 40.52 percent, but policy transitions are 26,632,382 versus 26,546,434, up only 0.32 percent. The candidate also makes 13.47 percent more enumeration calls. These stages have different filtering and reuse behavior. Extra legal placements are real, but the full output or evaluation multiplier must not be labeled unavoidable capability cost.

The bounded program then tested four ranked mechanisms, one at a time, under count gates and frozen telemetry-off selectors. All four returned `NO-ADVANCE` (section 2). The program ended with report `d46e682`; the performance gate remains failed. This establishes that the tested implementations did not meet their advancement criteria, not that every implementation or legacy-host design must fail.

### Why a much faster BFS does not imply a much faster engine

The owner's figures, less than 50 ns for `binary_bfs` and roughly 1,500 ns for legacy `Search::search`, motivate a controlled comparison. A packed reachability result is not the same output as a materialized, ordered candidate list. The exact inputs, build flags, and timed boundaries behind those two figures still need to be paired.

The candidate's engine path in `src/toj_rule.h`, `detail::enumerate_into_for_block`, prepares the workspace, runs arrival-aware search, visits landing bits, constructs geometry-derived keys, buckets and sorts them, and deduplicates them. Rule application, result-board deduplication, evaluation, and tree work follow in `src/tetris_engine.cpp`.

Existing diagnostic timer rows give about 1,087 ns per candidate enumeration call versus 1,751 ns per legacy search call, ratio 0.621. Including the different call counts, total enumeration time is 1.242 versus 1.763 seconds, ratio about 0.705: enumeration already saves time in this diagnostic. These are the full measured enumeration scopes, not raw `binary_bfs`. The source rows are `results/phase7/architecture_review_2026-09-09/raw/architecture-review-2026-09-09-{candidate,legacy-cmp}-timers.txt`. Instrumentation and differing scopes make these attribution evidence, not binding wall-time evidence.

Likewise, the much larger corpus wins in `results/phase7/gate89/summary.md` do not measure the production wrapper: `src/arrival_candidates.cpp` times arrival search and raw counting without candidate normalization, while `tests/legacy_corpus_bench.cpp` includes legacy map import and metadata rebuilding. Both normalize outside their timed passes.

The speedup budget must therefore account for search-call volume, wrapper cost, evaluation reuse, and tree work. A faster BFS can lose its savings elsewhere without the BFS itself being slow.

## 2. What has already been attempted

| # | Mechanism | Commit anchor | Selector result | Measured effect |
|---|---|---|---|---|
| 0 | Retained optimization stack (word-wise hash, cells caching, epoch transposition reset, compact 152/160-byte keys, landing validation) | `58e2739`, `e055cf5`, `510b356`, `d928a05` | gate campaign `5809409` | Brought the gap from about 6 percent down to 1.04155 |
| 1 | PGO build | `795b2f1` | rejected pre-selector | 1.05980, worse |
| 2 | Exact live-node evaluation index (two variants A/B, screen `abbdcb1`, trial `a716223`, selector `d60c5bd`, legacy comparison `8cd342d`, report `f7fa419`) | as listed | B beat A internally, then `NO-ADVANCE` versus legacy | About 14.2 percent slower than legacy despite a 41.2 percent post-memo hit rate |
| 3 | Child SoA staging (two-array layout) | `e02f476`, count gate `b1ab3bc`, selector `52d6ee0` | `NO-ADVANCE` | About 10.8 percent slower than normal candidate |
| 4 | Direct-view transposition keys | `e0da804`, count gate `368a332`, selector `0b30dd8` | `NO-ADVANCE` | About 8.1 percent slower than normal candidate |
| 5 | Evaluate-and-safe row export fusion | `abf870d`, count gate `2c75c94`, selector `1e71abf` | `NO-ADVANCE` | Total ratio 0.98915 versus normal, about 1.09 percent faster; insufficient alone |

The layout, key, and row-fusion trials preserved the frozen semantic/work counts and partition traces. The evaluation-index trial preserved the search work and evaluation results while intentionally changing cache hits, computations, and retained memory. Its measured post-memo hit rate was 41.217 percent. Fewer computations did not repay lookup, insertion, exact comparison, and memory-access costs in that implementation.

The SoA selector's p95 spread was 0.06620, and both direct-key spreads exceeded 0.04. Those trials failed their advancement rules; their effect sizes are not tight bounds on removable cost. Row fusion's spreads were 0.00754 total and 0.01207 p95.

The normal value-profile binary remained byte-identical to `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765` throughout. No legacy timing block beyond the single authorized index-legacy comparison was consumed by these four trials.

### Acceptance bar versus engineering target

The migration medians must both be candidate/legacy at most 1.02. The latest gate-1/2 runbook additionally requires both ratio spreads to be at most 0.04 (`results/phase7/gate12_2026-09-09/RUNBOOK.md`). Closing the median gap alone is insufficient; the p95 uncertainty must also satisfy that rule. The later trials also used more ambitious 0.90 primary and 0.95 fallback thresholds; each runbook defines its denominator. A selector's `NO-ADVANCE` must not be read as proof that it produced no useful improvement.

Moving from 1.04155 to 1.02 needs a candidate-time reduction of `1 - 1.02 / 1.04155 = 2.069%`. The row-fusion gain is roughly half that requirement, not an order of magnitude too small. If it transferred unchanged, `1.04155 * 0.98915` would be about 1.03025, leaving roughly another 0.995 percent candidate-time reduction. This multiplication is a planning estimate across campaigns, not a measured combined result or a qualification pass. Both total and p95 still require a fresh authorized gate.

The failed cache representations do not disprove reuse. The accepted trace contains 26,632,382 requests over 9,031,593 distinct boards, including 16,916,626 cross-parent repeats (`results/phase7/eval_reuse_trace_2026-09-09/report.md`). That proves repeated exact work exists, not that a practical cache can eliminate it profitably within the memory cap. The index trial report explicitly rejects a universal impossibility conclusion and notes that the 78 ns miss timer includes memo fingerprinting and lookup, not just removable evaluation arithmetic. See `docs/phase7/session_report_2026-09-09_eval_index_trial.md`.

Untested canonical ordering, frontier, and node-layout ideas have no established sub-1-percent upper bound. That does not justify another open-ended optimization program; it does mean the previous trials cannot prove a ceiling.

## 3. Legacy reuse is technically plausible, but unmeasured

The clean rewrite is an explicit architecture requirement in the port plan, not proof that the legacy engine cannot host fast reachability. The existing implementation provides concrete integration seams:

- `m_tetris::TetrisEngine<TetrisRule, TetrisAI, TetrisSearch>` accepts a replaceable search type (`src/tetris_core.h`). `src/tetris_profile.cpp` already instantiates it with a `ProfiledSearch` wrapper.
- `TetrisContext::get(TetrisBlockStatus)` resolves an existing pose node. `ExternalPoseTransform::to_legacy` in `src/toj_rule.h` supplies the inverse coordinate mapping. For representable poses, an adapter could map fast results to stable existing nodes; it need not rebuild the graph or allocate a geometry node per landing.
- `TetrisNodeWithTSpinType` carries arrival/classification flags separately from the pose pointer. The new arrival kernel and `src/toj_pathfinder.h` provide building blocks for replacing traversal and chosen-move path generation, rather than invoking legacy traversal again.

This is feasibility evidence, not a working adapter or a speed claim. No prototype or measurement was identified in the recorded trials, and the consulted migration agent confirmed none was recorded.

The real issues to test are:

1. **Representation:** legacy `TetrisMap` and its pose domain are 40 rows, while the target board is 48 rows. Parent-board packing and pose lookup cost time. A low-board/common-domain experiment cannot establish full target compatibility; upper-row cases and unmappable poses must be counted and reported, never silently truncated.
2. **Semantic identity:** legacy reuse and same-piece hold deduplication key on pose status in parts of `TetrisTreeNode::search`. The target distinguishes normal and terminal-rotation arrivals at the same T pose. Returning richer landings alone may not preserve both through those consumers.
3. **Policy and paths:** preserve target lowest-mino lockout, mini/full classification, supported movement modes, and replay-valid paths for newly found placements. Reusing legacy `make_path` cannot be assumed to reach placements legacy enumeration misses. Do not fabricate predecessor pointers to satisfy its interface.
4. **Evaluation reuse:** legacy's direct-mapped table verifies hashes, not boards (`TranspositionTable::find` in `src/tetris_core.h`). Its performance is a useful diagnostic control, but its cache contract is not an exact-cache implementation ready to inherit into the target.

Three choices must remain distinct: a temporary legacy-host diagnostic, reuse of proven legacy scheduling/allocation ideas with value storage, and permanent dual-engine deployment. The first does not commit us to the third. Keeping the legacy scheduler also does not require keeping the legacy reachability semantics.

Shipping a legacy-host solution would require owner amendments to the clean-port, graph-deletion, board, and cache contracts as applicable. A temporary experiment can instead identify which dataflow is worth porting back to the value engine.

## 4. Which phase we are in

The kernel, rule, policy, value engine, and migration-test foundations are implemented. Work is paused in Phase 7, "cut over production harnesses one at a time", with qualification blocked. Do not equate that progress with complete harness integration: the planned legacy-versus-value quality comparison is still missing (section 6A). Phase 8 and legacy deletion have not started. Production still runs the legacy engine.

## 5. Recommended next step: one bounded legacy-host diagnostic

**Question:** can the fast enumerator retain its advantage when consumed by legacy engine dataflow, and which costs prevent that advantage in the value engine?

This is a diagnostic plan, not a production migration or permission to repeat failed trials. Use one isolated experimental target. Keep production sources' behavior, frozen artifacts, parameters, and qualification thresholds unchanged. Execution requires an approved runbook that names the temporary architecture exceptions, exact scope, comparison budget, and advancement criteria.

### Step 1: measure equivalent work before changing an engine

Reuse the existing search corpus, raw benchmarks, `arrival_candidates`, `legacy_corpus_bench`, and candidate-partition oracles. Add only the missing diagnostic stages:

- Prepacked-board raw BFS, with every result made observable and extraction/checksum overhead reported separately. Check that optimization has not removed or hoisted the searches.
- Workspace preparation plus arrival-aware search and landing extraction.
- Complete production candidate enumeration, including ordered canonicalization.
- Legacy search with its input prepared outside the timed span, plus a separately reported import-inclusive measurement.

Use identical boards, pieces, starts, rotation/drop flags, compiler/ISA flags, and frozen parameters where applicable. Batch timing around many calls instead of putting a clock read around a sub-50-ns operation. Report per-call costs and aggregate stage totals, per-piece and board-class distributions, counts, and output digests. Keep diagnostics separate from telemetry-off wall-time measurements.

Start with the existing legacy-compatible subcorpus and report its coverage limits. For end-to-end attribution, use a frozen parent-input replay from the real workload as well: identical seeds alone do not keep the engines on identical boards after they choose different moves. Separate a shared ordered candidate-stream diagnostic from each implementation's full legal output. Any filtering for equal-work analysis is test-only, never a production optimization.

**Deliverable:** a cost table from input preparation through canonical candidates, and a reconciled count partition. Do not infer tree performance from the raw BFS ratio.

### Step 2: build one adapter, not another engine rewrite

Instantiate the legacy engine with a test-only fast search adapter. Reuse current arrival primitives, canonicalization, coordinate conversion, and existing pose lookup. A supplied-start entry point is required: `enumerate_into_for_block` hardcodes spawn, while kernel seeding clamps the start to the selected height cut. Validate the original pose and use a full-height search initially unless cropped-search equivalence is proven for that start and rotation. Do not silently replace a live pose with a different seed. Convert each parent board once for its current/hold sources where possible, with explicit invalidation. Do not run a second scalar reachability search or add per-candidate pathfinding.

Before timing, verify:

- Every in-scope canonical candidate maps to the same occupied cells and preserves its arrival class through child creation, reuse, and hold deduplication.
- Shared placements produce the expected board, clear, spin, policy, and lockout outcomes. Changed target semantics are explicitly partitioned rather than disguised as legacy parity.
- Newly found placements and both T arrivals survive; the adapter replaces legacy path generation with the value pathfinder. Path or replay failure is a hard stop, never an implicit straight drop. An empty pre-lock command sequence is acceptable only if replay proves the selected placement and arrival.
- Arbitrary active poses, blocked spawn, hold/queue boundaries, both 180 settings, and upper-row/domain failures are exercised through production and independent scalar replay.
- Actual retained memory includes adapter workspace, thread-local canonicalization scratch, cache storage, and both board representations. Do not rely on legacy `memory_usage()` alone. Respect the existing 256 MiB cap unless the owner separately approves a diagnostic exception.

Limit source changes to the adapter, its supplied-start/canonicalization helper, and narrowly scoped test-only identity, policy, and path integration. Stop if preserving the target behavior requires a broad tree rewrite. Report the failing seam instead of silently widening the experiment. A common-domain-only adapter is a valid partial result, not a full migration candidate.

### Step 3: distinguish dataflow benefit from weaker correctness

Compare three labeled configurations: legacy with legacy search, legacy with the fast adapter, and the current value engine. On matched-parent replay, report candidate application, actual evaluation computations, cache work, child/node materialization, and selection costs as well as enumeration.

The unchanged legacy hash-only cache may be retained only as an explicitly labeled diagnostic control. Before crediting a win to reusable architecture, audit it against exact-board/fresh-evaluation results and measure an exact or cache-disabled control. Include slot-replacement/result-lifetime checks: `TetrisCore::eval` retains a pointer to the table's result, not an owned evaluation copy. Collision-free observations do not prove an exact contract. If the benefit disappears when enforcing the target contract, report that rather than advertising a compliant speedup.

Require candidate/order/result identity within equal-work comparisons. For full-output engine comparisons, report and partition intended work differences instead of demanding identity to legacy or treating less work as a free optimization. Keep timings and correctness instrumentation separate.

Freeze the selector protocol before running it. Any new legacy timing block, including this diagnostic comparison, needs explicit authorization under the existing holdout discipline. Use the prescribed paired order, machine controls, hashes, memory accounting, and raw-row preservation. No appended pairs or post-result threshold changes. Microbenchmarks and this selector cannot replace the full qualification campaign.

### Step 4: stop and make the architecture decision

Return one report with stage timings, semantic coverage, memory, work counts, and these possible conclusions:

- **Adapter wins with target-compatible semantics and exactness:** propose either porting the demonstrated dataflow improvement into the value engine or an explicit production-architecture amendment. Neither is automatic.
- **Only the restricted or hash-only control wins:** identify the contract cost and remaining compatibility work. Do not claim the final target is faster.
- **Adapter loses:** identify whether conversion, canonicalization, extra work, or tree integration consumes the saving. This rejects this adapter, not all possible reuse.
- **Feasibility gate fails:** preserve the exact counterexample and stop without timing an incorrect implementation.

No cutover, legacy deletion, permanent second engine, parameter retuning, gate relaxation, or second optimization program follows automatically.

## 6. Alternatives after the diagnostic

**A: gather quality evidence.** Useful, but the binding comparison harness must first be implemented and verified: `src/tuner.cpp` has no `compare-engines` command, and `src/migration_compare.cpp` runs legacy against legacy. This is remaining integration work, not an already executable campaign.

The quality gate is non-inferiority: one-sided lower bounds must clear negative 3 percentage points WR and negative 3 percent relative APP/APL. Passing does not prove additional strength. The plan requires 32/128-pair screens, a 256-pair variance/power stage after performance acceptance, and 1,000 fresh final pairs. Advancing that sequence needs owner approval. If quality is to justify a slower engine, predeclare the superiority/tradeoff criterion separately before using the final holdout. Keep the binding comparison at equal 20 ms budgets and the planned fixed-iteration diagnostic: playing strength under a deadline depends on throughput. Lack of superiority alone is not failure of the original migration goal.

**B: revisit the value engine with a measured budget.** Consider adopting row fusion and one demonstrated complementary improvement only after authorization. Roughly another 1 percent might close the acceptance gap if the fusion gain transfers; the new combined artifact still needs full verification. Do not conflate the 1.02 migration bar with the more ambitious trial targets.

**C: amend the performance or production-architecture requirement.** This is an owner tradeoff, not an inference from failed selectors. Any relaxation leaves the remaining correctness, throughput, path, memory, and quality gates in force. Permanent dual deployment is a separate choice with dual-maintenance cost, not the meaning of the proposed diagnostic.

**D: stop the migration.** Keep legacy in production and preserve the candidate and evidence. This remains legitimate if the measured benefits do not justify further work.

## Recommendation and decision

Proceed with the diagnostic-first plan in section 5 before declaring legacy reuse impossible or relaxing the performance bar. The evidence establishes a fast kernel, expensive surrounding work, and unsuccessful tested fixes. It does not establish that the remaining gap is intrinsic to fast reachability.

Owner direction recorded for this revision: diagnostic prototype first; revise this document only. Before execution, approve the bounded runbook, temporary contract exceptions, selector thresholds, and legacy comparison budget. Production remains legacy, and the current qualification failure remains unchanged.
