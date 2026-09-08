# Fast-reachability migration: coordination session report, 2026-09-07/08

Full record of the coordinator session that audited slices 7.1C-7.2E and
continued the phase-7 qualification-repair arc. Branch
`fast-reachability-migration`; starting HEAD `920aa12`, ending HEAD
`40abf12` (ten commits). All work products referenced below are
committed; frozen evidence under
`/home/icly/Documents/tetris_ai_runner_results/` was never modified.

## 1. Mandate and method

Complete `docs/fast_reachability_port_plan.md` from phase 7, slice 7.2E,
with independent validation of everything executed since slice 7.1C
(work before 7.1C was previously validated). Method: low-cost subagents
for mechanical, research, and implementation tasks; the coordinator
personally performed evidence-critical validation (source review of
every change, verdict adjudication, protocol design) and committed only
after validation. Nine subagent tasks ran in total: one build/test
matrix, three read-only research/design tasks, two implementation
tasks, one measurement campaign, one profiling task, plus one resumed
after a provider rate limit and one unblocked after a `pgrep -f`
self-match hang (the loop's own wrapper shell matched the pattern).

## 2. Audit of 7.1C-7.2E: findings and resolutions

Three Luna workers had audited comparator code, campaign evidence, and
transposition memory before the pause. The coordinator verified each
finding directly:

- Comparator identity defect: matched candidate equality keyed on a
  64-bit occupancy digest violated the exact occupied-cell contract.
  Real; the repair (sorted `candfmt::Cells` in the key, hash retained
  only as probe accelerator) was reviewed line by line and accepted.
- Compact transposition keys (320 -> 160-byte entries via eight logical
  words): hash inputs, equality field set, and signed-zero
  normalization verified preserved; `static_assert`s pin 152/160.
- `is_landing` rule change: equivalent to the old workspace-checker
  predicate for all in-board poses (exhaustive 904,022-check
  differential over 32 boards, floor and ceiling boundaries analyzed
  against the kernel's open-ceiling semantics), and strictly more
  compliant with plan 9.4 for out-of-board inputs.
- Gates 8/9 PASS claims were UNSUPPORTED (the corpus harness compares
  kernel-vs-kernel and Reference A only; no legacy placement
  enumerator comparator existed). Corrected to UNPROVEN in both verdict
  documents, then closed for good (section 4).
- 7.2C old-binary reproducibility is not claimable (superseded
  `c4579a87...` binary absent everywhere); rows preserved as collected.
- All recorded 7.2E numbers were independently recomputed from raw rows
  by the repaired `tests/audit_phase7_campaign.py` and match exactly.
- Full four-preset build/CTEST matrix on the combined change: 29/29
  everywhere; the paused session's Clang debug crash did not reproduce
  on stable sources (concurrent-edit explanation confirmed).

Committed as `d928a05`.

## 3. Optimization arc: five accepted, count-gated changes

Each change was gated on bit-identical work vectors where semantics
preserving, or partition-adjudicated where volume-changing, plus the
full four-preset build/CTEST matrix.

| Commit | Change | Measured effect (paired, pinned core 7) |
|---|---|---|
| `510b356` | Epoch-stamp transposition reset (u32 epoch replaces the used bool; entry stays 160 B; O(1) reset; wrap-safe) | cold reset 2.0 ms -> 280 ns per root; D/A totals 0.7487 |
| `e055cf5` | Word-wise transposition hashing (142 -> 25 FNV steps) + cells caching in candidate canonicalization (order-preserving) | E/D totals 0.7961; count identity 37/37 fields exact |
| `deaf420` | Fingerprint-gated 16-byte entries, T = 2^20, arena 705,851 (design in `transposition_capacity_design.md`) | fail-stop 174/200 -> 0; full 200,000-iteration budget completed; +22 percent work at 0.573x wall time |
| (reverted `40abf12`) | Dedup hash set + 64 MiB eval-cache rebalance | NEGATIVE: 1.0250 wall regression; reverted, bounds recorded |

Trajectory of the candidate against the frozen 7.2E baseline (cross-
session arithmetic, labeled approximate): 6.81x legacy -> ~5.1x (epoch)
-> ~4.1x (hash/cells) -> ~2.98x (fingerprint). Every step's work-vector
identity was verified exactly (up to 37 count fields), or partition-
adjudicated with zero defects.

## 4. Evidence gaps closed permanently

- Gates 8 and 9 (plan 17.3 items 8/9): a frozen legacy corpus comparator
  (`legacy_corpus_bench`, `ea6bf7f`) was built per the resolved design
  in `legacy_corpus_comparators_design.md` — 33-board legacy-comparable
  subcorpus (ASan-clean board identity, 231/231 BOARDCASE via an
  independent recomputation), one engine per process, like-for-like
  timed span. Results (`results/phase7/gate89/`, `755e2fd`): gate 8
  non-T time-per-parent worst piece 0.2399 against the 1.00 bar; gate 9
  T per normalized semantic candidate 0.0422 against the 1.02 bar;
  Reference A half 0.0928 (10.77x). PASS, ~4x margin.
- Gate 3 (count partition): the instrument designed in
  `count_partition_instrument_design.md` was implemented (`019fed2`) and
  run on the timed workload, seeds 1/2/3. Result: the entire per-input
  semantic delta is class (a) new-legal candidates proven by live
  scalar+replay oracles (seed 1: +395,300), zero defects on both legs
  across all seeds, exact integer accounting independently rechecked,
  and the campaign count deltas fully reconciled into the semantic
  partition plus exactly-counted volume terms (legacy 238,654 parents
  vs value 117,589 — re-walk asymmetry, no merging).

## 5. Measurement campaigns and attributions

- Pre/post diagnostics campaign (`6501bfc`,
  `results/phase7/prepost_diagnostics/`): 46 runs, count-identity gates,
  paired off-mode totals, comparator provenance (the frozen legacy
  comparator reproduces its recorded 7.2E counts bit-exactly to this
  day).
- Hotspot attribution #1 (`96c1366`): materialize/transposition 54.3
  percent of run (probe 46.9 percent of cycles) — drove the epoch and
  fingerprint work.
- Hotspot attribution #2 (`65ab235`): post-fingerprint landscape — rule
  17.7, eval 17.1, enum 14.9, scaffold 23.3 percent; kernel ~0.4
  percent; no dominant single lever remains.

## 6. Negative results (recorded, reverted)

- The 524,288-entry table at 320-byte entries fails into the arena
  cliff (retrodicts the 7.2D experiment exactly; capacity curve in
  `transposition_capacity_design.md`).
- Dedup hash set + 64 MiB eval-cache rebalance: count-safe and correct
  but a consistent ~2.5 percent wall regression (three tight pairs);
  reverted (`40abf12`), bounds recorded in the attribution addendum.
  The two targets are bounded as small-or-negative levers in this
  design space.

## 7. Current gate status

| Gate | Status |
|---|---|
| 1, 2 (totals, p95 <= 1.02) | approx 2.98x — FAIL pending further optimization |
| 3 (count partition) | Instrument built and run; zero defects; methodology complete |
| 4 (component rates) | Partially improved (materialize 10x per-unit); others open |
| 5 (path overhead) | PASS |
| 6 (timed throughput) | FAIL pending; now truncation-free on both sides after re-freeze |
| 7 (seed diagnostics) | Clean whenever run |
| 8, 9 (legacy comparators) | PASS (proven this session) |
| Comparator overhead bound | 1.0121 in diagnostics (recorded 1.02528 UNMET) |

Not qualified. No production cutover, tuner/match/DLL switch, or legacy
deletion is authorized.

## 8. Remaining work

1. Remediation of the broadly distributed ~2.98x gap: rule geometry
   dispatch, eval memo strategy (decision-adjacent; needs partition
   adjudication), enum adapter overhead (the raw kernel is 4x faster
   than legacy per gate 8, so the cost is adapter-side), child
   scaffold. Negative results bound two former targets.
2. Re-freeze candidate and comparator artifacts from a clean committed
   tree; regenerate the binding campaign under the full protocol
   (twin-run amendment, five ABBA+A pairs, seeds 1-3, gates 8/9 and the
   partition instrument re-run included).
3. After qualification: tuner/match/DLL cutovers, quality campaign,
   legacy deletion per plan phases 7-8.

## 9. Decision points for the repository owner

1. Continue the optimization campaign (weeks-scale, uncertain against a
   mature legacy engine) versus running the binding campaign now at
   approx 2.98x to validate the machinery and establish the honest
   baseline (recommended sequence: campaign first, then continue).
2. The 2 percent bar predates the discovery that the candidate performs
   structurally different, budget-complete search. Any margin revision
   is an explicit owner decision; the plan forbids silent relaxation.
