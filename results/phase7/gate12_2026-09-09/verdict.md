# Verdict: control-corrected rerun, gates 1 and 2, 2026-09-09

Directory: results/phase7/gate12_2026-09-09
Block: 2026-09-08T16:44:12Z to 2026-09-08T16:47:28Z, ten timed runs, all exit 0.

## Raw per-run values in execution order

| line | pair | side | total_s | p95_ms |
| 1 | 1.1-a | baseline | 17.255 | 114.543 |
| 2 | 1.1-b | candidate | 17.972 | 117.460 |
| 3 | 1.2-a | candidate | 18.324 | 121.307 |
| 4 | 1.2-b | baseline | 17.441 | 115.241 |
| 5 | 1.3-a | candidate | 18.335 | 118.513 |
| 6 | 1.3-b | baseline | 17.865 | 118.885 |
| 7 | 1.4-a | baseline | 17.400 | 114.603 |
| 8 | 1.4-b | candidate | 18.355 | 120.406 |
| 9 | 1.5-a | baseline | 18.009 | 121.835 |
| 10 | 1.5-b | candidate | 18.330 | 120.339 |

## Per-pair ratios, candidate over baseline

| pair | total inputs | total ratio | p95 inputs | p95 ratio |
| 1.1 | 17.972 / 17.255 | 1.04155 | 117.460 / 114.543 | 1.02547 |
| 1.2 | 18.324 / 17.441 | 1.05063 | 121.307 / 115.241 | 1.05264 |
| 1.3 | 18.335 / 17.865 | 1.02631 | 118.513 / 118.885 | 0.99687 |
| 1.4 | 18.355 / 17.400 | 1.05489 | 120.406 / 114.603 | 1.05064 |
| 1.5 | 18.330 / 18.009 | 1.01782 | 120.339 / 121.835 | 0.98772 |

## Summaries

Gate 1 total median: 1.04155 against bar 1.02.
Gate 1 total spread: 0.03706 against bar 0.04.
Gate 2 p95 median: 1.02547 against bar 1.02.
Gate 2 p95 spread: 0.06492 against bar 0.04.

## Controls observed

HEAD 280c140b7c9a0ad9e7ca8b81aaf653207caff973, tracked tree clean (untracked files only).
CPU 7 pin on every run. Governor performance, EPP performance, boost 1.
CPU 15 offline window 2026-09-08T16:44:10Z to 2026-09-08T16:47:28Z, verified online after.
CPU 7 frequency at block start 5324840 kHz, at block end 5259751 kHz.
Pre-block load check passed on first sample: loadavg 0.89 1.00 1.23.
Per-run load1 and load5 are in MANIFEST.txt controls fields: load5 stayed between 0.93 and 1.12, never near the 2.0 mid-block abort bar, so no wait was needed.
Pre-warm: baseline completed 2026-09-08T16:44:11Z, candidate completed 2026-09-08T16:44:12Z, both with moves 40 iters 200 telemetry off, outputs discarded.
Artifact hashes verified before the block: baseline 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376, candidate 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, param file ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.

## Anomalies

No fast first block this time: pair 1.1 baseline ran 17.255 s, inside the block range 17.255 to 18.009 s. The pre-warm control worked as intended.
Baseline block range 17.255 to 18.009 s is still wide at 0.754 s, and candidate range 17.972 to 18.355 s is 0.383 s. Pair 1.5 baseline at 18.009 s is the slowest baseline while pair 1.1 baseline at 17.255 s is the fastest, with per-run load1 rising from 0.64 to 1.36 across the block. Residual background load drift remains visible but no single run is an outlier comparable to the 2026-09-08 first-block anomaly.
No appended pairs, no cherry picking, no retry of any pair. One fresh block, ten runs, verdict computed on all five pairs.

## Verdict

NO-ADVANCE
