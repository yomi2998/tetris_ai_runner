# Gates 8/9 measurement summary — `current_arrival` vs `legacy_corpus` (33-board subcorpus)

(i) These measurements run the committed bench at
ea6bf7f41ad516a24cddbdc605b530b3ad51dae6: both exercised binaries stamp
`root=ea6bf7f… root_worktree=clean` (see MANIFEST.txt; full stamps in
gate89_corpus.txt detail lines).
(ii) Verdicts are made by the coordinator, not this document. Harness-printed
verdicts below are quoted verbatim and labeled as such; no pass/fail is claimed here.

Run: PERF_GATE_CORE=7, REPS=5, ITERS_T=300, ITERS_RAW=3000, WARMUP=10,
ONLY=current_arrivalvslegacy_corpus, 2026-09-07T15:08:45Z-15:08:46Z, exit 0.

## Verbatim gate rows printed by the harness (gates section of gate89_corpus.txt)

```text
legacy_subcorpus_cases_current_arrival 231 (expected 231 on both sides) PASS
legacy_subcorpus_cases_legacy_corpus 231 (expected 231 on both sides) PASS
legacy_subcorpus_time_ratio_medians_current_over_legacy total T=0.0762 Z=0.2399 S=0.2026 J=0.2248 L=0.2022 O=0.2218 I=0.1796 (lower is better; gate 8 is every non-T piece <= 1.000)
| Raw non-T enumeration versus frozen legacy search (33-board legacy subcorpus) | at most 1.000 per parent | worst non-T ratio 0.2399 (Z) | PASS |
legacy_subcorpus_T_per_candidate_ratio_median 0.0422 (raw T time ratio 0.0762, T candidates-per-parent current=40.45 legacy=22.42 from each side's own CASE rows; gate 9 is <= 1.020)
| T per normalized candidate versus frozen legacy search | at most 1.020 | T per-candidate ratio 0.0422 (raw T time ratio 0.0762, candidates-per-parent current 40.45 over 33 cases, legacy 22.42 over 33 cases) | PASS |
| T raw time current versus legacy (informational) | none | T time ratio 0.0762 | - |
| Perft vectors | exact | 8 of 8 in `fast_reachability_perft` | PASS |
```

Full gate table (all rows the filtered run prints):

```text
| Gate | Requirement | Measured | Verdict |
|---|---|---|---|
| Raw non-T enumeration versus frozen legacy search (33-board legacy subcorpus) | at most 1.000 per parent | worst non-T ratio 0.2399 (Z) | PASS |
| T per normalized candidate versus frozen legacy search | at most 1.020 | T per-candidate ratio 0.0422 (raw T time ratio 0.0762, candidates-per-parent current 40.45 over 33 cases, legacy 22.42 over 33 cases) | PASS |
| T raw time current versus legacy (informational) | none | T time ratio 0.0762 | - |
| Perft vectors | exact | 8 of 8 in `fast_reachability_perft` | PASS |
```

(Rows for the filtered-out sequences — T-vs-Reference-A, raw-vs-180fix, raw-vs-plain —
are absent by design of PERF_GATE_ONLY; the static header and Perft row always print.)

## Gate 8 — per-piece time-per-parent ratios current/legacy (all five ABBA pairs + median)

Source: `RATIO current_arrival/legacy_corpus <piece> total` lines (search lines identical;
neither side has an enum split, so total == search).

| piece | rep0 | rep1 | rep2 | rep3 | rep4 | median | spread | spread/median |
|---|---|---|---|---|---|---|---|---|
| Z | 0.2437 | 0.2434 | 0.2399 | 0.2333 | 0.2360 | 0.2399 | 0.0104 | 4.3% |
| S | 0.2012 | 0.2026 | 0.2037 | 0.1983 | 0.2134 | 0.2026 | 0.0151 | 7.5% |
| J | 0.2248 | 0.2213 | 0.2477 | 0.2148 | 0.2394 | 0.2248 | 0.0329 | 14.6% |
| L | 0.2030 | 0.2013 | 0.2022 | 0.1939 | 0.2165 | 0.2022 | 0.0226 | 11.2% |
| O | 0.2271 | 0.2207 | 0.2218 | 0.2214 | 0.2278 | 0.2218 | 0.0071 | 3.2% |
| I | 0.1765 | 0.1796 | 0.1810 | 0.1734 | 0.1836 | 0.1796 | 0.0102 | 5.7% |

Worst-of (gate 8): Z = 0.2399; every non-T median is <= 0.24 against a 1.000 bar.
Harness-printed verdict for this row: PASS (quoted, not claimed here).

## Gate 9 — T per normalized candidate (each side's own CASE counts)

T CASE totals (recomputed from CASE rows, `--reps 0` report passes):
current 1335 candidates / 33 cases = 40.4545 per parent;
legacy 740 candidates / 33 cases = 22.4242 per parent (UNMATCHED legacy_corpus 0).
Normalization factor legacy/current = 740/1335 = 0.55430712.

| rep | raw T time ratio | x 740/1335 | per-candidate ratio |
|---|---|---|---|
| rep0 | 0.0775 | | 0.0430 |
| rep1 | 0.0773 | | 0.0428 |
| rep2 | 0.0751 | | 0.0416 |
| rep3 | 0.0747 | | 0.0414 |
| rep4 | 0.0762 | | 0.0422 |
| median | 0.0762 | | 0.0422 |

(Per-candidate pairs derived from the harness-printed 4dp time ratios; the harness's own
full-precision median is 0.0422, identical.)
Harness-printed verdict for this row: PASS (quoted, not claimed here).

## Informational ratios
- T raw time current/legacy median: 0.0762 (pairs 0.0775 0.0773 0.0751 0.0747 0.0762).
- CORPUS totals: current 231 cases / 4346 candidates (hash 4c3b405583f2b953);
  legacy 231 cases / 3700 candidates (hash 461f648c2699fe5f, identical across san,
  release, and pre-check runs). Candidate hashes are not expected to match across
  engines (documented migration asymmetry: new-side sky placements above row 39).

## 231-cases-per-side check
- current_arrival: 231 (expected 231) — harness prints PASS.
- legacy_corpus: 231 (expected 231) — harness prints PASS.
- All 21 sequence invocations report `cases 231` with stable candidate hashes on both sides.

## Anomalies and notes for the coordinator
1. Duration ~1 s vs the brief's 30 min-2 h: explained in MANIFEST.txt (both sides are
   fast; no Reference A / raw-bench work in this sequence). No reps were reduced;
   the design's 300-internal-rep x 5-ABBA-pair structure ran as specified (21/21
   invocations present, seq00-seq20, trailing A control present).
2. Frequency ramp: rep0 runs hot on both sides (A T 263.9/262.6 ns vs steady ~218-225;
   B T ~3392-3406 vs steady ~2900-2995), the known powersave-ramp signature also
   visible in results/phase7/corpus_gate.txt. Pairing is within-rep (ABBA), so both
   sides of each pair share the phase; rep0 T pair (0.0775) sits inside the 0.0747-0.0775
   band, no differential distortion evident.
3. Two single-invocation spikes, both on the current (A) side, i.e. in the
   ratio-inflating (conservative) direction:
   - seq08 J=301.1 (neighbors ~252-274) drives the J rep2 pair to 0.2477, the max J
     value and the widest spread in the table (14.6% of median).
   - seq16 S=281.6/J=308.1/L=272.9/I=340.5 (neighbors ~240-260/~258/~228/~297-321)
     drives the rep4 maxima for S (0.2134), L (0.2165), I (0.1836).
   Medians are robust to both; no action needed unless the coordinator wants a
   re-run for tighter J/L bands.
4. Nothing sits near a threshold: the nearest approach to any bar is gate 8's worst
   median 0.2399 (Z) vs 1.000 — a ~4x margin. There is no 1.00/1.02 boundary decision
   for the coordinator in this data; the noise-band question does not arise.
5. Stale binaries: reference_a_frozen and the three raw_bench_* binaries in the release
   dir predate ea6bf7f (stamps root=510b356-dirty) and were not invoked; only their
   hashes/stamps appear in the captured header. Rebuilding them is unnecessary for
   gates 8/9 but the coordinator should know the dir is mixed-vintage.
