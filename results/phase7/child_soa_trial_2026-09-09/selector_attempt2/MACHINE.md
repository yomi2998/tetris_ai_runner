# Child SoA telemetry-off selector machine record, fresh attempt 2, 2026-09-09

Execution HEAD: 5c6740f695dbe7c91d7eefe4db27afdf4e8f50f4.
Ancestry: eac8039 present (first selector attempt ABORTED), b1ab3bc present (accepted Child SoA count gate).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/child_soa_trial_2026-09-09/selector_attempt2/collect.sh, SHA-256 6527addc46a3ebd84b0c64196d040e9251363145cbf6509f2b0a5262d8850740, invoked exactly once. Collector exit 0, terminal COLLECTED.
No profile or prewarm command was invoked outside the collector. The collector was never invoked again. Zero reruns.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal selector copy = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, mode 555, executable.
SoA selector copy = 721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91, mode 555, executable.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
accepted count verdict PARENT_VERDICT.md = 94a95df71e382d59d216b7be8cc4ce5458a99a84f60aae36d154c9963878062d.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 0.59 0.67 0.71 at 2026-09-09T15:52:49Z, five minute value 0.67 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T15:52:49Z to 2026-09-09T15:55:43Z.
CPU 15 was taken offline once before prewarm, read 0 before and after every prewarm and timed command, and was restored online after run 8 by the single shell path. Verified online after collection.

Prewarm, stdout and stderr discarded to /dev/null, exit 0 each:
prewarm normal start 2026-09-09T15:52:49Z end 2026-09-09T15:52:50Z, load 0.59 0.67 0.71.
prewarm soa start 2026-09-09T15:52:50Z end 2026-09-09T15:52:51Z, load 0.59 0.67 0.71.

Timed runs, telemetry off timers off, seed 1 warmup 20 moves 200 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T15:52:51Z end 2026-09-09T15:53:12Z, load 0.70 0.69 0.71.
run2 soa start 2026-09-09T15:53:12Z end 2026-09-09T15:53:34Z, load 1.06 0.77 0.74.
run3 soa start 2026-09-09T15:53:34Z end 2026-09-09T15:53:57Z, load 1.17 0.82 0.76.
run4 normal start 2026-09-09T15:53:57Z end 2026-09-09T15:54:17Z, load 1.42 0.90 0.79.
run5 soa start 2026-09-09T15:54:17Z end 2026-09-09T15:54:39Z, load 1.43 0.94 0.80.
run6 normal start 2026-09-09T15:54:39Z end 2026-09-09T15:55:00Z, load 1.59 1.01 0.83.
run7 normal start 2026-09-09T15:55:00Z end 2026-09-09T15:55:20Z, load 1.78 1.09 0.86.
run8 soa start 2026-09-09T15:55:20Z end 2026-09-09T15:55:43Z, load 1.70 1.12 0.88.

Per-pair load5 before pairs 1 through 4 was 0.69, 0.82, 0.94, and 1.09, each below the 2.0 wait gate, so no wait was required. No per-run load5 reached 2.0. All eight timed runs exited 0. No surviving process.

Anomalies: none. The prior attempt 1 ABORTED evidence is untouched. Attempt 1 contributes no row here.
