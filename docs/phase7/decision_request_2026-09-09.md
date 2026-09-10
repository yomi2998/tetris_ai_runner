# Decision request: fast-reachability migration, performance gate unresolved

Date: 2026-09-09.
Status: decision required from the owner.
Prepared after the executed optimization program ended per the owner's sequential-attempts directive.

## 1. What we are trying to achieve, and what happened

### The goal

Replace the production Tetris AI engine's pointer-network reachability stack with a clean engine built on the pinned fast-reachability kernel (`src/fast-reachability/`, commit `0c35e13`). The plan is `docs/fast_reachability_port_plan.md`. The new engine must preserve the exported ABI, path commands, and TOJ behavior, and must satisfy:

1. Performance: no more than 2 percent slower than the frozen legacy baseline under the frozen profile protocol (plan item 9).
2. Quality: 3 percentage point WR margin and 3 percent relative APP/APL margins over 1,000 seat-swapped pairs (plan item 10).

### The legacy side, anchored

- Frozen legacy baseline binary:
  `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`
  SHA-256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`.
- Legacy comparator binary used for count parity: SHA-256 `08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450`.
- The legacy engine stays in production and byte-frozen throughout; nothing in this program modified it.

### What happened

The new engine is built, correct, and semantically complete: 16,236-check engine regression, replay and corpus differentials green, gates 3, 8, and 9 of the qualification protocol proven. Its kernel is fast per call (about 0.62 times legacy search time per call). But it is slower overall than legacy, and performance qualification is blocked:

- Binding gate rerun (`5809409`, report `b3edf56`): candidate/legacy total median ratio 1.04155, p95 1.02547 against the 1.02 bar. Gates 1 and 2 FAIL.
- The architecture review (`f6796ce` request, `7b9f8c5` verdict) found the cause: the candidate computes 4.653x more evaluations than legacy (25,948,219 versus 5,576,912 on the fixed workload) because exact evaluation reuse is scoped to a single parent, and it processes 40.52 percent more canonical candidates because fast reachability genuinely finds more legal placements. These volumes are the price of the new capability.

An optimization program then tested every ranked removable cost, one mechanism at a time, each under fail-closed count gates and frozen telemetry-off selectors. All four returned `NO-ADVANCE` (details in section 2). The program ended at HEAD `d46e682` with the performance gate still failing.

## 2. What has already been attempted

| # | Mechanism | Commit anchor | Selector result | Measured effect |
|---|---|---|---|---|
| 0 | Retained optimization stack (word-wise hash, cells caching, epoch transposition reset, compact 152/160-byte keys, landing validation) | `58e2739`, `e055cf5`, `510b356`, `d928a05` | gate campaign `5809409` | Brought the gap from about 6 percent down to 1.04155 |
| 1 | PGO build | `795b2f1` | rejected pre-selector | 1.05980, worse |
| 2 | Exact live-node evaluation index (two variants A/B, screen `abbdcb1`, trial `a716223`, selector `d60c5bd`, legacy comparison `8cd342d`, report `f7fa419`) | as listed | B beat A internally, then `NO-ADVANCE` versus legacy | About 14.2 percent slower than legacy despite a 41.2 percent post-memo hit rate |
| 3 | Child SoA staging (two-array layout) | `e02f476`, count gate `b1ab3bc`, selector `52d6ee0` | `NO-ADVANCE` | About 10.8 percent slower than normal candidate |
| 4 | Direct-view transposition keys | `e0da804`, count gate `368a332`, selector `0b30dd8` | `NO-ADVANCE` | About 8.1 percent slower than normal candidate |
| 5 | Evaluate-and-safe row export fusion | `abf870d`, count gate `2c75c94`, selector `1e71abf` | `NO-ADVANCE` | About 1.1 percent faster than normal; real but an order of magnitude too small |

Every trial was count-identical to the normal candidate before timing (byte-identical partition traces, all 37 frozen work counts exact, exact memory accounting), so the timing results are clean. The normal production binary remained byte-identical to `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765` throughout, and no legacy evidence beyond the single authorized index-legacy comparison was consumed.

## 3. Why we are not simply reusing the legacy engine

The rewrite is not a preference; the legacy engine cannot host the replacement:

1. The legacy search is a precomputed pointer graph (`TetrisNode`, `TetrisOpertion`, thousands of runtime nodes built in `TetrisContext::prepare`). Pose identity is tied to pointer lifetime, and the same rule data is represented three different ways. The fast-reachability kernel is a bit-parallel BFS over a packed 10 by 48 board with compile-time piece and kick data. There is no meaningful way to run one inside the other.
2. Reachability, T-spin arrival tracking, and path reconstruction are fused in `search_tspin::Search` with special-case modes the target deletes (20G, `allow_rotate_move`, `X/Z/C`). Separating them inside the legacy code would be a rewrite with none of the cleanup.
3. The whole point of the port is capability: fast reachability finds legal placements the legacy search misses. Keeping the legacy engine keeps the old placement semantics forever.
4. The candidate already meets every correctness, replay, and structural gate. The only failing dimension is wall time.

## 4. Which phase we are in

Plan phases 1 through 6 (kernel port, rule, adapter, policy, engine, harnesses) are complete and committed. We are in Phase 7, "cut over production harnesses one at a time", specifically the qualification campaign, which is blocked by the gates 1 and 2 performance failure. Phase 8 (final validation) and the legacy deletion have not started. Production still runs the legacy engine.

## 5. Decision required: any other way?

Yes. Four realistic paths remain. The optimization-only path inside the current architecture is effectively exhausted: the four executed mechanisms bound the removable implementation cost, and the three remaining unexplored ideas (canonical-rank bitmap ordering, frontier replacement, Node hot/cold split) each target less removable work than mechanisms that already failed, with expected effects below 1 percent.

**Option A: decide with quality evidence (recommended).** The plan's quality campaign (1,000 seat-swapped pairs, WR/APP/APL margins) has never run because it was sequenced after qualification. It does not actually depend on performance. Fast reachability's whole purpose is finding better placements; the 4 percent time premium buys unmeasured playing strength. Run the quality campaign now with the current candidate. If quality is materially better than the 3 percent margins, the honest trade is to renegotiate plan item 9 (for example, accept at most 5 percent slower) and proceed to qualification, cutover, and legacy deletion with the row-fusion improvement folded in. If quality is not better, the migration loses its justification and Option D follows naturally. Cost: one campaign, already fully specified.

**Option B: renegotiate the performance bar without quality evidence.** Accept the current candidate (optionally plus the proven 1.1 percent row fusion) at about 4 percent slower, and unblock qualification immediately. Fastest path, but the quality premium stays unmeasured.

**Option C: hybrid deployment.** Ship the legacy engine as the default and the new engine behind a flag or a separate build. Preserves both, defers the decision indefinitely, and leaves the legacy deletion blocked forever. This is the plan's stated non-goal and carries permanent dual-maintenance cost.

**Option D: stop the migration.** Keep the legacy engine in production, archive the work. The new engine remains available on the branch.

## Recommendation

Option A. It is the only path that resolves the actual open question (is the capability worth 4 percent?) with evidence rather than assumption, and every outcome produces a defensible decision. The performance optimization program has done its job: it proved the remaining gap is a design trade, not an implementation defect.

## Decision

Owner selects: A / B / C / D, or directs otherwise.
