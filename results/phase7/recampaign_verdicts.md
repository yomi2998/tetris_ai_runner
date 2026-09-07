# Binding re-campaign verdicts (7.2E execution, resized candidate)

No cutover decision is made here; verdicts feed a separate decision. Overall
campaign result: NOT QUALIFIED. Gate 5 passes; gates 1, 2, 3, 4, and 6 fail;
gates 8 and 9 are UNPROVEN (corrected 2026-09-07, see the final section);
the comparator bound is unmet this session. All 124 rows plus
MANIFEST are preserved in `results/phase7/recampaign/` with no aggregation
during collection. Gate 7 seed diagnostics and items 10/11 are covered at
the end.

## Volume context (confounds every fixed-work verdict)

With the 262,144-entry table the candidate explores about 261,000 nodes
per fixed-work move (run medians) against about 275,000 legacy nodes: the
volumes are now comparable where the 7.2C campaign compared 33k against
275k. Residual truncation persists: 174 to 190 of every 200 moves still
fail-stop on table exhaustion with best-so-far intact, so most fixed-work
searches end at the table rather than at completion or budget. Per-move
materialized clusters just under capacity (259,856 to 261,608 averages)
rather than pinning it exactly; per-move exactness is not directly
recorded, only run totals plus exhaust flags. Arena pressure stays inside
the reservation (`mem_retained_bytes` 268,435,160 against the 268,435,456
budget, idmap scratch 1,166,392 bytes as sized). Timed demand never
approaches either cliff.

## Gate 1 (total time, seed 1 fixed): FAIL as expected

Per-pair candidate/baseline ratios: 7.31626, 6.51379, 6.90783, 6.29812,
6.81284. Median 6.81284 against the 1.02 bar. Seeds 2/3: 6.76115/6.67510.
The honest volume costs about 6.8x the baseline per move; the 7.2C passes
were truncation artifacts. Collected cleanly, not softened; no optimization
started.

## Gate 2 (p95 latency, seed 1 fixed): FAIL as expected

Per-pair ratios: 8.23214, 6.79433, 7.69742, 6.82492, 7.66833. Median
7.66833 against the 1.02 bar. Seeds 2/3: 7.22709/7.07977.

## Gate 3 (count partition): FAIL (blocked, narrowed but unclassified)

Seed 1: unique candidates 62,283,081 vs 55,838,330 (relative +0.11542);
policy transitions 53,598,323 vs 64,334,998 (relative -0.16689). Seeds 2
and 3 repeat (+0.15868/-0.15614, +0.12225/-0.17906). The gap narrowed from
the -86/-90 percent class toward mixed-sign mid-teens deltas, and the
truncation carve is quantified above (residual fail-stop on ~90 percent of
fixed-work moves, pending-at-stop unrecorded per move), but no
100-percent-accounted partition into the four plan classes can be produced
from the collected evidence: the remaining delta mixes broader candidate
discovery (value finds more uniques) against heavier transition dedup
(value merges about 7,400 per move while legacy re-walks), lockout-rule
divergence, and residual truncation in proportions no collected counter
separates. The pre-authorized cross-invocation distinct-state instrument
was not needed for this verdict and was not built. Unclassified remainder
stands: the campaign is invalid at the partition requirement.

## Gate 4 (component rates, seed 1 fixed): FAIL

Median candidate-to-comparator per-unit ratios (five pairs each):
enumeration 3.82578, unique-candidate 1.08216, eval-hit 1.44282,
eval-miss 1.90900, transition 1.31386, materialized 12.89630,
selected-path-state 0.64999. Six of seven legs exceed the 1.02 bar; only
the path leg passes. Seeds 2 and 3 repeat (enum 3.99/3.77, unique
1.05/1.07, hit 1.50/1.50, miss 1.68/1.92, trans 1.30/1.29, mat 14.74/13.60,
path 0.65/0.67). The materialized leg carries the stated 100-percent
occupancy caveat: median demand exceeds capacity, so probes walk long
chains at full load on most moves. The 7.1A scope asymmetries from the
prior campaign still apply.

## Gate 5 (path overhead): PASS

Value `path_ms` over the full move total peaks at 0.00011 on every seed
against the 0.02 bar.

## Gate 6 (timed throughput, 20 ms, counters-only both sides): FAIL

Per-pair candidate-to-comparator completed-work-per-second ratios, seed 1:
widening 0.21501, 0.24235, 0.21942, 0.23682, 0.23624 (median 0.23624);
parents median 0.24699; transitions median 0.26125. Seeds 2 and 3 repeat
(0.23-0.27 throughout). The bar requires at least 0.98. Absolute timed
counts (seed 1, 200 moves): legacy about 48k widening iterations, 390k
parents, 15.8M transitions against value about 14k, 118k, 5.1M; both sides
honor their budgets (loop walls 4.01 s legacy, 4.14 s candidate). These are
the first untruncated production-shaped searches on the candidate side,
which is why this gate is now the most informative one.

## Comparator counters-only bound: UNMET this session

Comparator-vs-frozen fixed-work total ratios: 1.02177, 1.02528, 1.03412,
1.01113, 1.03379. Median 1.02528 against the 1.02 bound. The gate 6 legs
therefore lack their prerequisite quite apart from failing on merit; per
the noise rule the bound must be re-established if those legs are ever
revisited.

## Gate 7 (seed diagnostics)

Seeds 2 and 3 rows parse on every row kind with zero replay failures (hard
asserted during computation). Count deltas fall under the gate 3 block.
Latency diagnostics: seeds 2/3 fixed totals medians 6.76115/6.67510 and p95
medians 7.22709/7.07977; timed legs repeat the seed 1 shape.

## Items 10 and 11

Item 10 completeness: warmup handling stated in every row, artifact hashes
in the protocol table and MANIFEST, candidate counts and cache hits
present, completed work recorded, ABBA+A run order in the manifest,
machine record attached. Item 11: the same absolute parameter file and
combo bytes feed every collected command (verified from MANIFEST); no
retuning occurred.

## Gates 8 and 9 (CORRECTED 2026-09-07: UNPROVEN, not PASS)

The 7.2E close originally recorded "unchanged from the 7.2C verdicts (PASS):
the enumeration kernel is untouched by the capacity repair, so the corpus
rates stand without re-running." That claim is unsupported and is withdrawn.

What `results/phase7/corpus_gate.txt` actually proves (still valid, still
standing as Phase-2 comparisons):

- Phase-2 kernel regression: current raw BFS versus the frozen
  `0c35e13`-lineage kernel builds, every non-T piece at or below 1.020
  (worst total 0.9868 O, worst search-only 0.9879 O): PASS.
- Reference A T gate: the current T semantic enumerator is at least 2x
  faster than the frozen Reference A wrapper (ratio 0.0928; Reference A
  10.77x slower): PASS.
- Perft vectors exact, 8 of 8: PASS.

What it does not prove, and what plan Section 17.3 items 8 and 9 require:

- Item 8: median paired time-per-parent of raw non-T enumeration versus
  frozen **legacy placement enumeration** at the 1.00 bar. No legacy
  placement-enumeration comparator binary exists in `tests/run_perf_gate.py`
  or the CMake target set; the raw leg compares kernel against kernel.
- Item 9: the new T enumerator at most 1.02 per normalized semantic
  candidate versus the frozen **legacy `search_tspin` enumerator**. Only the
  Reference A half of item 9 is covered; the legacy half has no comparator,
  and the harness performs no per-normalized-candidate division on any
  legacy timing.

Gates 8 and 9 are therefore UNPROVEN pending the two frozen legacy
comparators the plan requires. The recorded corpus numbers remain valid
evidence for the comparisons they actually measured; nothing was re-run or
re-measured for this correction. The audit tooling that establishes this is
`tests/audit_phase7_campaign.py` (repaired 2026-09-07; see
`docs/phase7/audit_71c_72e.md`).

RESOLVED 2026-09-07 (later the same day): gates 8 and 9 are now MEASURED
and PASSING. The frozen legacy corpus comparator (`legacy_corpus_bench`,
commit `ea6bf7f`) ran against `arrival_candidates --legacy-subcorpus` on
the 33-board legacy-comparable subcorpus — ASan-clean board identity
(231/231 BOARDCASE via an independent recomputation), pinned core 7,
five ABBA pairs, 300 internal reps (`results/phase7/gate89/`):
gate 8 non-T time-per-parent current/legacy worst piece 0.2399 (Z), all
pieces at or below 0.24 against the 1.00 bar; gate 9 T per normalized
semantic candidate median 0.0422 against the 1.02 bar (per-side T
CASE-row counts 1335/33 current vs 740/33 legacy), with the Reference A
half previously proven (0.0928, 10.77x). Measured on the post-epoch
tree; the binding re-campaign will re-freeze and re-run them with the
rest of the gates.
