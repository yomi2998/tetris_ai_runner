Execution HEAD: 1884768fa927272b0436b944eeb7ea4630201fbb.
Ancestry: e02f476 present, 369d447 present, 19ea6a1 present, bffa256 present.
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/child_soa_trial_2026-09-09/count_attempt3/collect.sh, SHA-256 b3e6e686bc190b7676a4bc0c536dbfa1686be57bc6c529f4b9ff6056f2ee314f, invoked exactly once. Collector exit 0, terminal COLLECTED.
No recorder, profile, prewarm, or test command was invoked outside the collector. No collector command was repeated. Zero reruns.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal tetris_profile_value = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765.
SoA tetris_profile_child_soa = 721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91.
normal candidate_partition = 5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1.
SoA candidate_partition_child_soa = 6b5b520fdf253def718b3743f8fd53d54eb7636329c83f9c39b2510a65fa582a.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
prerequisite transcript prerequisite_tests_2026-09-09.txt = 82a22a0cfd7afc288b2255007aca0c82bb3278b49544f9bbd0092e6e3ee797d0.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 1.01 0.78 0.77 at 2026-09-09T15:20:46Z, five minute value 0.78 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T15:20:46Z to 2026-09-09T15:22:13Z.
CPU 15 was taken offline once before the partition trace, stayed continuously offline through both trace records and all eight count runs, and was restored online after run 8. State read 0 before the normal trace, before the SoA trace, before every count run, at every completion, and at collection complete. Verified online after collection. No anomaly.

Partition trace, outputs under trace_attempt3, exit 0 each, stderr empty each:
normal record start 2026-09-09T15:20:46Z end 2026-09-09T15:20:50Z, load 1.01 0.78 0.77, seed 1 warmup 0 moves 20 maxdepth 6 iters 1000.
SoA record start 2026-09-09T15:20:50Z end 2026-09-09T15:20:53Z, load 1.01 0.78 0.77, same workload.
Raw files made read-only after collection. The trace_attempt3 directory is fresh; no prior attempt wrote there.

Count runs, telemetry on timers off, seed 1 warmup 20 moves 80 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T15:20:53Z end 2026-09-09T15:21:03Z, load 1.09 0.80 0.77.
run2 soa start 2026-09-09T15:21:03Z end 2026-09-09T15:21:13Z, load 1.15 0.82 0.78.
run3 soa start 2026-09-09T15:21:13Z end 2026-09-09T15:21:23Z, load 1.13 0.83 0.78.
run4 normal start 2026-09-09T15:21:23Z end 2026-09-09T15:21:33Z, load 1.26 0.87 0.80.
run5 normal start 2026-09-09T15:21:33Z end 2026-09-09T15:21:43Z, load 1.30 0.89 0.81.
run6 soa start 2026-09-09T15:21:43Z end 2026-09-09T15:21:53Z, load 1.25 0.89 0.81.
run7 soa start 2026-09-09T15:21:53Z end 2026-09-09T15:22:03Z, load 1.30 0.91 0.82.
run8 normal start 2026-09-09T15:22:03Z end 2026-09-09T15:22:13Z, load 1.25 0.92 0.82.

Per-run load5 values never reached the 2.0 abort gate. Half-gate load5 before run1 was 0.80 and before run5 was 0.89, both below 2.0, so no wait was required. All eight timed runs exited 0. No surviving process.
