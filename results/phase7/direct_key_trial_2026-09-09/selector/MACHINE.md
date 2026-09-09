# Direct-key telemetry-off selector machine record, 2026-09-09

Execution HEAD: 7b67fc910859ea6af8c34b48e94b2c95ee657102.
Ancestry: 368a332 present (accepted direct-key count gate).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/direct_key_trial_2026-09-09/selector/collect.sh, SHA-256 7a436c3a3d8d474d83c2957943727b5842c4cbeef0109c559f012ebf2ac901f5, invoked exactly once. Collector exit 0, terminal COLLECTED.
No profile or prewarm command was invoked outside the collector. The collector was never invoked again. Zero reruns.

Label mapping: collector template labels spelling `soa` (prewarm_soa, run2_soa, run3_soa, run5_soa, run8_soa) denote the direct-key variant. Every such command invoked the hash-verified tetris_profile_direct_key artifact. Raw files keep their original names.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal selector copy = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, mode 555, executable.
direct-key selector copy = 420002a847f6fea6cb9e87b868f82ad1497bc0b12b7e817ebbe612c9dce6ee89, mode 555, executable.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
accepted count verdict PARENT_VERDICT.md = 7b2e2dc7ecfb576afee3710b93465b8f914f591e5a685269be26fb6a14dc142e.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 0.97 0.73 0.85 at 2026-09-09T17:40:49Z, five minute value 0.73 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T17:40:49Z to 2026-09-09T17:43:44Z.
CPU 15 was taken offline once before prewarm, read 0 before and after every prewarm and timed command, and was restored online after run 8 by the single shell path. Verified online after collection.

Prewarm, stdout and stderr discarded to /dev/null, exit 0 each:
prewarm normal start 2026-09-09T17:40:49Z end 2026-09-09T17:40:50Z, load 0.97 0.73 0.85.
prewarm direct-key (label prewarm_soa) start 2026-09-09T17:40:50Z end 2026-09-09T17:40:51Z, load 0.97 0.73 0.85.

Timed runs, telemetry off timers off, seed 1 warmup 20 moves 200 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T17:40:51Z end 2026-09-09T17:41:11Z, load 0.97 0.73 0.85.
run2 direct-key (label run2_soa) start 2026-09-09T17:41:11Z end 2026-09-09T17:41:34Z, load 1.10 0.78 0.87.
run3 direct-key (label run3_soa) start 2026-09-09T17:41:34Z end 2026-09-09T17:41:57Z, load 1.61 0.93 0.91.
run4 normal start 2026-09-09T17:41:57Z end 2026-09-09T17:42:18Z, load 1.72 1.02 0.94.
run5 direct-key (label run5_soa) start 2026-09-09T17:42:18Z end 2026-09-09T17:42:40Z, load 1.52 1.01 0.94.
run6 normal start 2026-09-09T17:42:40Z end 2026-09-09T17:43:01Z, load 1.59 1.06 0.96.
run7 normal start 2026-09-09T17:43:01Z end 2026-09-09T17:43:21Z, load 1.49 1.07 0.97.
run8 direct-key (label run8_soa) start 2026-09-09T17:43:21Z end 2026-09-09T17:43:44Z, load 1.35 1.07 0.97.

Pair-gate load5 before pairs 1 through 4 was 0.73, 0.93, 1.01, and 1.07, each below the 2.0 wait gate, so no wait was required. No per-run load1 reached 2.0 either. All eight timed runs exited 0. No surviving process.

Anomalies: none.
