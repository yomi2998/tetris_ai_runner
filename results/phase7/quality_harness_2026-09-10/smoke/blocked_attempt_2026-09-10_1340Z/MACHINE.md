# Smoke stage machine record, 2026-09-10 (BLOCKED)

Execution HEAD: 911e8768da044867c050d6ac6d3e5ada9811c847.
Tracked state at invocation: clean.
Collector: results/phase7/quality_harness_2026-09-10/collect.sh, SHA-256 5ba94d5aa1bc45ac436c128dd1111320175ab7bbaccc72870cdf3179e5eff502, invoked exactly once.
Collector exit: 30, sole terminal TERMINAL=BLOCKED reason=load_precheck.

Preconditions verified before invocation:
driver quality_match = b92ee3b2e68661c1d822de25fddc532ed842a90cfaf2728cab9d44a4f6be9d9d.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
normal tetris_profile_value = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, byte-identical.
smoke/matches.csv and smoke/collect.log absent before invocation.
CPU 15 online. CPU 7 governor performance, EPP performance, boost 1.
CPU control preflight passed.

Load precheck series (load1 load5 load15, UTC):
attempt 0, 2026-09-10T13:30:13Z, 1.34 2.13 5.33.
attempt 1, 13:31:13Z, 1.19 1.96 5.07.
attempt 2, 13:32:13Z, 0.98 1.77 5.07.
attempt 3, 13:33:13Z, 0.99 1.63 5.07.
attempt 4, 13:34:13Z, 1.47 1.65 4.39.
attempt 5, 13:35:13Z, 1.47 1.65 4.21.
attempt 6, 13:36:13Z, 15.16 5.04 5.18.
attempt 7, 13:37:13Z, 25.75 9.86 6.83.
attempt 8, 13:38:13Z, 12.16 9.09 6.77.
attempt 9, 13:39:13Z, 17.28 10.94 7.54.
attempt 10, 13:40:13Z, 27.91 15.23 9.24.

Five-minute load was falling toward the 1.5 gate (2.13 to 1.65 across attempts 0 through 5) when an external load spike arrived at attempt 6 (load1 15.16, later 27.91). The five-minute gate was never met within the frozen initial-read-plus-ten-recheck budget, so the collector terminated BLOCKED.

Nothing ran: no quality_match invocation, no matches.csv, no per-seat rows, CPU 15 was never taken offline (CLEANUP_RESTORE result=not_needed), verified online after the terminal. Machine load remained elevated after the terminal (load5 14.30 at 13:41Z), consistent with the external spike, not with any diagnostic process; no quality_match process existed after the terminal.
