# Smoke stage machine record, 2026-09-10

Execution HEAD: 7c8d465fa7f91931ddd6d3a053c59b093a92ffd1. Tracked state clean.
Collector: collect.sh SHA-256 5ba94d5aa1bc45ac436c128dd1111320175ab7bbaccc72870cdf3179e5eff502, invoked exactly once, exit 0, terminal COLLECTED. Three prior BLOCKED attempts archived under smoke/blocked_attempt_*/; none executed anything.
Driver: quality_match SHA-256 b92ee3b2e68661c1d822de25fddc532ed842a90cfaf2728cab9d44a4f6be9d9d. Normal binary byte-identical to 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765; parameters ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.

Controls: CPU 7 governor performance, EPP performance, boost 1. Precheck passed at attempt 0 (load1 0.91, load5 1.32 at 2026-09-10T13:59:18Z). CPU 15 offline 13:59:18Z to 14:00:03Z, state 0 at every boundary, restored online. Eight concurrent pairs over 15 usable CPUs (within-thread oversubscription recorded; symmetric within every seat-swapped pair).

Command: quality_match match --param-a <frozen_29d.bin> --param-b <frozen_29d.bin> --pairs 32 --ms 20 --seed 910 --threads 8 --max-rounds 3600 --out smoke/matches.csv --seats legacy,value. Total wall 44.661 s, 64 games.

Results: value win-equivalents 27.0/64, legacy 37.0/64, WR point 0.421875, one-sided 95 percent lower bound 0.325531 (z 1.6449), two-sided interval [0.308696, 0.543902]. APP value 1.040064 versus legacy 0.992562 (ratio 1.0479); APL value 1.645935 versus legacy 1.489595 (ratio 1.1050). Deaths: value 37, legacy 27. replay_failures 0, arena exhaustions 0, capped 0, spawn deaths 20, lockout deaths 0, invalid deaths 0. Value per-move wall: median 20.648 ms, p95 22.863 ms, max 26.205 ms against the 20 ms budget.

Budget-scope note: the driver samples value-seat walls only; legacy-seat wall medians are not emitted by this build. The value median overshoots the nominal budget by 3.2 percent from self-sampling overhead. Both seats received the same nominal 20 ms budget with setup and path outside the timed span; a legacy-side wall sampler is a pending harness improvement and is recorded as a limitation, not silently assumed symmetric.

Sanity gate: zero replay failures, no memory kill, no crash. PASS (sanity only). WR and APP/APL figures are diagnostic at this scale and are not campaign criteria.
