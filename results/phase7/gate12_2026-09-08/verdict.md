# Gate 1/2 campaign verdict, 2026-09-08 (optimization-stack candidate, first compliant run)

Candidate: `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08`, sha256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`, built from HEAD `1b046db` by clean reconfigure and rebuild of `linux-gcc-self-release`; the rebuild reproduced the retained-session binary byte for byte.

Baseline: frozen production `tetris_profile.baseline`, sha256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`.

Controls: `docs/phase7/campaign_machine_record_2026-09-08.md`. CPU 7 pinned, governor and EPP `performance`, boost fixed at 1, SMT sibling CPU 15 offline for the whole block (2026-09-08T16:12:03Z to 16:15:25Z, frequency 5293670 to 5282401 kHz), no operator work during the window, `TETRIS_AI_PARAM_FILE` unset on every run.

Workload: seed 1, 200 moves, 1000 iterations, depth 6, warmup 20, telemetry off, absolute frozen parameter path, quiet-version 2 baseline and 3 candidate. Five pairs in first-engine order baseline, candidate, candidate, baseline, baseline (B then C, C then B, C then B, B then C, B then C). Raw rows and manifest: `rows.txt` and `MANIFEST.txt` in this directory, no aggregation during collection. All 10 exits zero.

## Verdict

- Gate 1 (total time): pair ratios 1.05275, 1.02564, 0.98159, 1.01410, 1.02674. Median 1.02564 against the 1.02 bar: FAIL.
- Gate 2 (p95): pair ratios 1.04563, 1.01597, 0.94282, 1.00023, 0.98981. Median 1.00023 against the 1.02 bar: PASS on median, but the pair spread (0.10280) blocks any pass claim under the standing noise rule; the same spread also means the gate-1 fail sits inside the measurement band.

Overall campaign result: NOT QUALIFIED at gate 1. No favorable pairs are appended and no rerun is taken on a failing number; the sanctioned one-time control-corrected rerun remains available to the owner if the background-load controls can be strengthened.

## Anomalies recorded honestly

- The first baseline block (17.516 s) is 5.4 to 7.6 percent faster than the other four baseline blocks (17.944, 18.846, 18.792, 18.137). It inflates pair 1.1 to 1.05275 and pulls the total median up. Recomputing the total median with that one block replaced by the baseline steady state (about 18.5 s) gives about 1.014, under the bar, which is why the spread rule matters: the gate-1 fail is not separable from machine noise with this data.
- The baseline within-kind spread (17.516 to 18.846, 7.6 percent) exceeds the 0.7 percent back-to-back reproducibility observed earlier tonight for short runs, despite the pinned governor and offline SMT sibling. The residual driver is background desktop load (observed load average about 1.8), which no operator control in this session can remove.
- Candidate blocks were steadier: 18.440, 18.404, 18.499, 19.057, 18.622 (3.5 percent spread).

## Next step per the standing owner instruction

The predeclared decision tree says: on a gate-1/2 failure, pursue one bounded GCC PGO effort while preserving both 1.02 limits, with the corpus and screening controls already fixed (training corpus frozen before building, qualification seeds 1 through 3 excluded, frozen production parameters, fixed-work and 20 ms timed runs in the corpus, corpus commands and profile-data hashes preserved, bit-identical count ABBA required, GCC as the binding compiler with Clang correctness builds retained, at most two training-corpus attempts, and held-out candidate-to-candidate screening near 3 percent on both total and p95 before any formal qualification).
