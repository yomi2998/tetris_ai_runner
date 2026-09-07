# Binding qualification verdicts (7.2C execution)

No cutover decision is made here; verdicts feed a separate decision. Overall
campaign result: NOT QUALIFIED. Gates 1, 2, and 5 pass per formula; gates
3, 4, and 6 fail; gates 8 and 9 report separately below. frozen baseline,
candidate, and comparator artifacts are those recorded in
`qualification_protocol.md` Section 1; all 124 rows plus MANIFEST are
preserved in `results/phase7/campaign/` with no aggregation during
collection.

## Central finding (confounds gates 1 through 6)

The value engine materializes exactly 32,768 nodes per fixed-work move
(6,553,600 over 200 moves: precisely the transposition-table capacity),
then fail-stops the search on table exhaustion (`texhaust_moves` set on all
200 seed-1 moves, `pending_end_max` 32,184 left unexpanded). Legacy explores
about 275,000 nodes per move with no such cliff. Every verdict below must
be read with this volume asymmetry: the candidate searches roughly 10x
less per move at higher per-unit cost.

## Gate 1 (total time, seed 1 fixed): PASS

Per-pair candidate/baseline `total_s` ratios: 0.39304, 0.37638, 0.37785,
0.38405, 0.37700. Median 0.37785 against the 1.02 bar. Seeds 2 and 3
diagnostics: medians 0.36864 and 0.36474.

## Gate 2 (p95 latency, seed 1 fixed): PASS

Per-pair ratios: 0.32382, 0.31788, 0.31654, 0.32254, 0.31423. Median
0.31788 against the 1.02 bar. Seeds 2 and 3 diagnostics: medians 0.31464
and 0.31612.

## Gate 3 (count partition): FAIL (blocked, unclassified remainder)

Seed 1 deltas: unique candidates 7,760,873 vs 55,838,330 (relative
-0.86101); policy transitions 6,732,608 vs 64,334,998 (relative -0.89535).
Seeds 2 and 3 show the same shape (-0.86080/-0.89505 and -0.86171/-0.89704).
All deltas far exceed 2 percent, so a 100-percent-accounted partition into
fixture-identified new legal candidates, removed semantic duplicates,
lockout-semantic changes, and defects is required, and none can be produced
from the collected evidence:

- Legacy runs all 1,000 anytime iterations on every move (200,000 total,
  never complete); the value engine stops at 19,549 passes after table
  exhaustion. Legacy expands 1,535,377 parents against 159,205.
- Per-parent yields are comparable (value 48.7 uniques per parent against
  legacy 36.4), so the delta is search volume, not per-expansion richness.
- Legacy evaluation hits 76 percent of requests (41.8M of 55.1M) against
  about 8 percent value-side, consistent with legacy re-walking overlapping
  subtrees every iteration while value touches each distinct board once.
- Legacy allocates 55.1M nodes almost entirely through free-list recycling
  (55.06M) with 748k status-identity reuses and zero search roots (every
  move reuses a child subtree as root); value retains 6.55M arena nodes.
- The four plan classes do not carve this phenomenon: the volume gap comes
  from completion semantics (never-complete widening vs table-capacity
  fail-stop) and cross-move retention asymmetry (recounted vs retained
  subtrees), for which no fixture-identified integer counts exist in the
  collected evidence. The unclassified remainder is therefore the near
  totality of the 48M/58M deltas, which invalidates the campaign per item 3.

## Gate 4 (component rates, seed 1 fixed): FAIL

Median candidate-to-comparator per-unit ratios (five pairs each):
enumeration 3.73619, unique-candidate 1.08620, eval-hit 1.32432,
eval-miss 1.86114, transition 1.36136, materialized 3.44075,
selected-path-state 0.65908. Six of seven legs exceed the 1.02 bar; only
the path leg passes. Seeds 2 and 3 repeat the pattern (enum 3.96/3.56,
unique 1.05/1.05, hit 1.39/1.28, miss 1.82/1.82, trans 1.36/1.35, mat
3.35/3.43, path 0.70/0.68). Contributing scope asymmetries, stated not
assumed: value enumeration and materialization absorb canonicalization and
transposition work that legacy performs in untimed status-identity maps;
legacy search calls mix cached and fresh traversals; the value miss span
includes memo and cache insertion while the legacy miss span covers the
policy call only. The truncation confound from the central finding applies
to every leg: truncated searches against full ones.

## Gate 5 (path overhead): PASS

Value `path_ms` over the full move total peaks at 0.00174 (seed 1), 0.00166
(seed 2), 0.00169 (seed 3) against the 0.02 bar.

## Gate 6 (timed throughput, 20 ms, counters-only both sides): FAIL

Per-pair candidate-to-comparator completed-work-per-second ratios, seed 1:
widening 0.28487, 0.28412, 0.28259, 0.28297, 0.28295 (median 0.28297);
parents median 0.29453; transitions median 0.31165. Seeds 2 and 3 repeat
(0.28-0.32 throughout). The bar requires at least 0.98; the candidate
completes roughly one third the legacy work per second under equal
deadlines. Absolute timed counts (seed 1, 200 moves): legacy about 48k
widening iterations, 390k parents, 15.8M transitions against value about
14k, 118k, 5.1M. Both sides honor their 20 ms budgets (loop walls 4.01 s
legacy, 4.14 s candidate).

## Comparator counters-only bound: MET (marginal)

Comparator-vs-frozen fixed-work total ratios: 0.99422, 1.03845, 1.01801,
1.02938, 1.01701. Median 1.01801 against the 1.02 bound. Spreads cross the
bound on individual pairs; under the noise rule this prerequisite holds
only narrowly and must be re-established if any gate 6 leg is revisited.

## Gates 8 and 9 (corpus rates)

`tests/run_perf_gate.py` over the frozen corpus on pinned core 7
(`results/phase7/corpus_gate.txt`, five ABBA-plus-control repetitions,
per-piece medians):

- Item 8 (raw non-T enumeration vs frozen kernel): worst total ratio 0.9868
  (O), worst search-only 0.9879 (O) against the 1.00 bar: PASS. 180-off
  informational leg: worst 0.9893/0.9857: PASS.
- Item 9 (T search): time ratio 0.0928 vs the Reference A wrapper (gate: at
  most 0.50): PASS with Reference A 10.77x slower; raw T vs the
  equal-semantics frozen comparator 0.8804 total / 0.8007 search-only: PASS.
- Perft vectors exact, 8 of 8: PASS.
- Provenance notes the worktree dirty with the 7.2C correction files; none
  touches the measured kernel (`search.hpp` identical), and the comparator
  flag is unset on every measured binary, so the dirt cannot affect these
  rates.

## Gate 7 (seed diagnostics)

Seeds 2 and 3 rows parse on every row kind with zero replay failures (hard
asserted during computation). Count deltas fall under the gate 3 block.
Latency diagnostics: seeds 2/3 fixed totals medians 0.36864/0.36474 and p95
medians 0.31464/0.31612; timed legs repeat the seed 1 shape.

## Items 10 and 11

Item 10 completeness: warmup handling stated in every row, artifact hashes
in the protocol table and MANIFEST, candidate counts and cache hits
present, completed work recorded, ABBA+A run order in the manifest,
machine record attached. Item 11: the same absolute parameter file and
combo bytes feed every collected command (verified from MANIFEST); no
retuning occurred.

## Disclosure triplets (seed 1 fixed, adjacent runs)

Value off/counters-only/full totals: 7.033/6.954/7.939 and
6.948/7.011/8.263. Comparator: 18.813/18.601/33.543 and
19.080/19.173/34.149. Full-timer overhead runs about 14 to 18 percent over
counters-only on the value side and about 80 percent on the comparator
side; counters-only sits within about 1 percent of detached on both.
