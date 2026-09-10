# Final campaign machine record, 2026-09-10

Execution HEAD: b6357e1d8996d52262071c650aa536ce25c283b6.
Tracked state at invocation: clean.
Collector: results/phase7/quality_harness_2026-09-10/collect.sh, SHA-256 5ba94d5aa1bc45ac436c128dd1111320175ab7bbaccc72870cdf3179e5eff502, invoked exactly once with argument `final`. Collector exit 0, terminal COLLECTED.
Driver: out/build/linux-gcc-self-release/quality_match, SHA-256 b92ee3b2e68661c1d822de25fddc532ed842a90cfaf2728cab9d44a4f6be9d9d.
Normal binary: tetris_profile_value SHA-256 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, verified unchanged, zero quality-harness symbols.
Parameters: artifacts/frozen_29d.bin, SHA-256 ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.

Stage: final, 1000 seat-swapped pairs, 2000 games, seed 999, threads 8, 20 ms budgets per seat per move, max 3600 rounds.

Controls: CPU 7 governor performance, EPP performance, boost 1, CPU 15 taken offline once at 2026-09-10T14:21:31Z after load precheck passed on attempt 4 (load5 1.47 below 1.5; earlier attempts 2.30, 2.09, 1.89, 1.58), continuously offline through the whole match, restored online at 14:44:57. Match wall time 1405.899 s. No surviving process at end.

Raw outputs: matches.csv (2000 rows), collect.log, match.stderr.txt (empty).
