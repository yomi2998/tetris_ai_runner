# Pre/Post Diagnostics Timing Campaign — summary

DIAGNOSTIC ONLY. These measurements are attribution diagnostics, not binding gate
verdicts. Binding gates require the full campaign protocol with re-frozen artifacts.

All timed runs: `taskset --cpu-list 7`, `TETRIS_AI_PARAM_FILE` unset, seed 1 unless
noted, `--warmup-moves 20 --moves 200 --maxdepth 6 --param-file
/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000`.
Raw rows: `rows.txt` (execution order; microbench runs occupy two physical lines
each: RESET + SANITY). Run metadata: `MANIFEST.txt` (46 lines, one per run).

## Machine state

- kernel: 7.2.3-1-cachyos
- cpu: AMD Ryzen 7 7700 8-Core Processor
- governor (cpu7): powersave (`/sys/devices/system/cpu/cpu7/cpufreq/scaling_governor`)
- pinning: every timed run under `taskset --cpu-list 7` (core 7); no concurrent load
  during any timed run (file appends/ratio math done between runs only)
- repo: branch fast-reachability-migration, HEAD 510b35668db19a3c94e5adbe7ae9f79a63d1fa1f
- param file sha256: ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037

## Binary / hash table (all verified pre-campaign; full hashes)

| id | path | sha256 |
|----|------|--------|
| A (frozen 7.2E candidate) | /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate | 816bcd7d33a7996a207d1c8bbba8bcf838baccfc6246ecb084d445f3b49938fe |
| B (three-repair build) | /home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/tetris_profile_value | 58f4674ddf3f495915404f36922e49c841b9092ea3fe43ec1d131af6f7d8d730 |
| D (post-epoch build) | /home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/tetris_profile_value.d | 77fac930547cc3ff84ab6c7712f68a010ec13f88b89cb898e85efb25fc055d6d |
| old comparator | /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp | 08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450 |
| new comparator | /home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/tetris_profile_legacy_cmp | fa830879ab9a775f590d8b327d3fa12e229ad4607cd8b0e2acbd0901bf9681c5 |
| frozen legacy baseline | /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline | 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376 |
| microbench pre | /home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/transposition_reset_bench.pre | 299bf72bdaa5c513be89702b198982d281086efe914bb4c7839b000ff5838200 |
| microbench post | /home/icly/Documents/tetris_ai_runner_results/phase7_diag/binaries/transposition_reset_bench.post | 3cc89cea398986b472ed607a1b52b5670292697ba5d6cb567147e0ba02df0a5b |

All hashes matched the expected prefixes before any timed run. No mismatches.

## LEG 1 — count identity (counters-only, A/B/D/D2; MANIFEST lines 1-4)

GATE RESULT: PASS. All 20 required count fields bit-identical across A, B, D, D2:
searches=2398357, unique_candidates=62283081, transitions=53598323, parents=1257436,
widening_iters=166334, materialized_nodes=51971320, transposition_merges=1622222,
eval_memo_hits=1256473, eval_computed=49502217, cache_requests=52341850,
cache_hits=2839633, cache_misses=49502217, cache_replacements=49499594,
dead_moves=0, games=0, texhaust_moves=174, pending_end_max=257661,
raw_landings=136269623, path_states=191721, replay_failures=0.
(Timing fields total_s/min/median/p95 and all *_ns excluded per protocol, as expected
they differ: total_s A=115.404 B=101.266 D=87.832 D2=87.967.)

Non-gate observation (not a mismatch): `mem_retained_bytes` (A=268435160 vs
B/D=268435272) and `arena_reserved_bytes`/`idmap_reserved_bytes`
(A=93311360/1166392 vs B/D=176161920/2202024) differ between A and B/D. These fields
are outside the gate list; consistent with a changed arena sizing between builds.

## LEG 2a — off-mode totals A vs D, five pairs (lines 5-14)

Pairs executed back-to-back as (A,D),(D,A),(A,D),(D,A),(A,D). NOTE on ordering: the
work order's prose order-string ("A,D,D,A,A then D,A,A,D,D") is not self-consistent
with consecutive back-to-back pairing, so the explicitly listed pair sequence above
was followed and is recorded per-run in MANIFEST.txt.

| pair | A total_s | D total_s | D/A total_s | A p95_ms | D p95_ms | D/A p95 |
|------|-----------|-----------|-------------|----------|----------|---------|
| AD-1 | 115.607 | 86.553 | 0.7487 | 827.656 | 566.053 | 0.6839 |
| AD-2 | 115.281 | 86.367 | 0.7492 | 803.208 | 564.114 | 0.7023 |
| AD-3 | 114.760 | 85.015 | 0.7408 | 789.764 | 554.940 | 0.7027 |
| AD-4 | 116.928 | 88.174 | 0.7541 | 809.764 | 570.665 | 0.7047 |
| AD-5 | 115.054 | 84.828 | 0.7373 | 819.752 | 545.957 | 0.6660 |
| median | — | — | **0.7487** | — | — | **0.7023** |

## LEG 2b — off-mode totals B vs D, three pairs (lines 15-20)

Pairs executed back-to-back as (B,D),(D,B),(B,D). NOTE: the work order lists only five
tokens ("B,D,D,B,B") for three pairs (six runs); the trailing D is assumed omitted and
pairs (B,D),(D,B),(B,D) were run, as recorded in MANIFEST.txt.

| pair | B total_s | D total_s | D/B total_s | B p95_ms | D p95_ms | D/B p95 |
|------|-----------|-----------|-------------|----------|----------|---------|
| BD-1 | 97.838 | 85.273 | 0.8716 | 660.560 | 548.384 | 0.8302 |
| BD-2 | 99.832 | 85.122 | 0.8527 | 666.026 | 553.692 | 0.8313 |
| BD-3 | 99.957 | 87.613 | 0.8765 | 670.867 | 571.273 | 0.8515 |
| median | — | — | **0.8716** | — | — | **0.8313** |

## LEG 2c — control (line 21)

Single off-mode run of A: total_s=117.706, p95_ms=834.306. Slightly above the LEG 2a
A range (total_s 114.760-116.928, p95 789.764-827.656): +0.7% vs the slowest LEG 2a A
on total_s, +0.8% on p95. No action; reported for drift context.

## LEG 3 — component-rate attribution (full-timer singles A/B/D; lines 22-24)

UNPAIRED SINGLE RUNS — attribution diagnostics only. Per-unit components (ns/unit):

| component | A | B | D | D/A | D/B | B/A |
|-----------|---|---|---|-----|-----|-----|
| enum_ns/searches | 6868.72 | 6932.64 | 7178.61 | 1.0451 | 1.0355 | 1.0093 |
| rule_ns/unique_candidates | 109.15 | 117.41 | 115.73 | 1.0603 | 0.9857 | 1.0757 |
| eval_hit_ns/(eval_memo_hits+cache_hits) | 69.59 | 67.72 | 65.89 | 0.9468 | 0.9729 | 0.9731 |
| eval_miss_ns/eval_computed | 154.73 | 150.03 | 148.14 | 0.9574 | 0.9874 | 0.9696 |
| policy_ns/transitions | 59.50 | 60.67 | 59.97 | 1.0080 | 0.9886 | 1.0196 |
| materialize_ns/materialized_nodes | 1634.82 | 1269.86 | 1032.80 | 0.6318 | 0.8133 | 0.7768 |
| (path_find_ns+path_replay_ns)/path_states | 73.83 | 75.17 | 70.16 | 0.9503 | 0.9333 | 1.0182 |

Full-timer totals: A=130.062 s, B=111.088 s, D=98.608 s. Counts in all three
full-timer rows are identical to the LEG 1 counters (e.g. searches=2398357,
unique_candidates=62283081), confirming identity under full-timer mode as well.
Largest mover is materialize (D/A 0.6318); enum_ns/searches rose in D (D/A 1.0451).

## LEG 4a — comparator old vs new, full-timer, three pairs (lines 25-30)

Pairs (old,new),(new,old),(old,new), back-to-back.

| pair | old total_s | new total_s | new/old total_s | new/old search_ns | new/old norm_ns |
|------|-------------|-------------|-----------------|-------------------|-----------------|
| CMP-1 | 35.091 | 35.562 | 1.0134 | 1.0228 | 0.8870 |
| CMP-2 | 35.193 | 34.535 | 0.9813 | 0.9944 | 0.8599 |
| CMP-3 | 34.918 | 35.341 | 1.0121 | 1.0112 | 0.8746 |
| median | — | — | **1.0121** | **1.0112** | **0.8746** |

IDENTITY HARD CHECK: PASS. unique_candidates=55838330 and transitions=64334998 in
all six rows. In fact every comparator count field is identical across old/new
(eval_requests=55090360, eval_hits=41760497, eval_calls=13329863, searches=2491971,
widening_iters=200000, parents=1535377, raw_landings=55838330,
materialized_nodes=55090360, recycled_nodes=55060969, reused_nodes=747970,
dedup_survivors=55838330, path_states=8336, dead_moves=0, games=0,
unmatched_candidates=0, search_roots=0). No digest-collision signal, no defect signal.
Note norm_ns drops ~13% in new (median new/old 0.8746) while search_ns is ~flat
(median 1.0112).

## LEG 4b — counters-only bound mirror (lines 31-36)

Three back-to-back pairs of (frozen baseline off-mode PROFILE_V2, new comparator
counters-only `--quiet --timers off`). Fresh CMP/V2 total_s values reported without
pass/fail claim:

| pair | V2 total_s | CMP total_s | CMP/V2 |
|------|------------|-------------|--------|
| BM-1 | 19.324 | 19.017 | 0.98411 |
| BM-2 | 18.416 | 18.880 | 1.02520 |
| BM-3 | 18.078 | 18.727 | 1.03590 |
| median | — | — | **1.02520** |

Fresh median 1.02520 reproduces the recorded 7.2E value 1.02528 to 8e-05. BM-1 at
0.98411 shows pair-level noise spanning both sides of 1.0 at this short duration.

## LEG 5 — microbench, pinned core 7 (lines 37-46)

Strictly alternating pre,post x5. All ten SANITY lines ok=1
(reroots_retained=200/200, transposition_used_after_run=133).

| run | ns_per_cold_root | ns_per_reroot |
|-----|------------------|---------------|
| pre1 | 2401663 | 2986203 |
| post1 | 281 | 466761 |
| pre2 | 2005617 | 3090391 |
| post2 | 313 | 441413 |
| pre3 | 1984472 | 2979467 |
| post3 | 280 | 414860 |
| pre4 | 1977756 | 2962165 |
| post4 | 280 | 473257 |
| pre5 | 3550577 | 2989491 |
| post5 | 272 | 451307 |
| pre median | **2005617** | **2986203** |
| post median | **280** | **451307** |

Outlier note: pre5 ns_per_cold_root=3550577 (+77% over the pre median); all other pre
cold values within ~±20% of median. Post cold_root ~7150x below pre median; post
reroot ~6.6x below pre median.

## Anomalies / notes

- No hash mismatches; no non-zero exit codes (all 46 runs exit=0); no parse failures.
- Wall-time: value runs 84-131 s (full-timer A 130.062 s the longest), comparator
  34-36 s, baseline 18-20 s, microbench ~1 s each. Total campaign ~75 min.
- Pair-ordering ambiguities in the work order (LEG 2a prose order-string; LEG 2b
  five-tokens-for-six-runs) resolved as documented above; MANIFEST.txt records the
  actual per-run order.
- Two unpinned exploratory microbench invocations (`--help`, which the binaries do
  not implement — they ran the bench) were executed before LEG 5 to discover output
  shape; excluded from rows.txt/MANIFEST.txt.
- Comparator counters-only rows report unique_candidates=na / unmatched_candidates=na
  (telemetry counters suppressed in that mode); identity for the comparator rests on
  the LEG 4a full-timer rows.
- powersave governor on core 7 throughout; no governor changes made.

## Diagnostic statement

All of the above are diagnostics, not binding gate verdicts. Binding gates require
the full campaign protocol with re-frozen artifacts. Verdicts are made by the
coordinator.
