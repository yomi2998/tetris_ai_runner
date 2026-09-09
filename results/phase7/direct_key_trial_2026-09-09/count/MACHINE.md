# Direct-view transposition key count gate machine record, 2026-09-09

Execution HEAD: ef4772ede2af20f152163f79a62e6dd2ddd5fd69.
Ancestry: e0da804 present (trial-only direct-view transposition key implementation).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/direct_key_trial_2026-09-09/count/collect.sh, SHA-256 86d99434a7fadcf62061238844c93346fc37de0f9db8158ca8a47a742b0a38c3, invoked exactly once. Collector exit 0, terminal COLLECTED.
No recorder, profile, prewarm, or test command was invoked outside the collector. No collector command was repeated. Zero reruns.

Label mapping: the collector's template labels use the spelling soa for the trial variant. Every file the collector named trace_soa or runN_soa was produced by the direct-key trial artifact (candidate_partition_direct_key or tetris_profile_direct_key). All metadata maps these labels to variant direct-key.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal tetris_profile_value = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765.
direct-key tetris_profile_direct_key = 420002a847f6fea6cb9e87b868f82ad1497bc0b12b7e817ebbe612c9dce6ee89.
normal candidate_partition = 5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1.
direct-key candidate_partition_direct_key = 4642f85a76e6f04635cca482c9654f3e465d266ae6e43441d0c539776da83fd8.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
prerequisite transcript prerequisite_tests_2026-09-09.txt = 5c40bf19b78b435396719857c24a471310618c52b87281414a7eadc4cc04a27f.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 0.73 0.96 1.02 at 2026-09-09T17:27:29Z, five minute value 0.96 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T17:27:29Z to 2026-09-09T17:28:57Z.
CPU 15 was taken offline once before the partition trace, stayed continuously offline through both trace records and all eight count runs, and was restored online after run 8 by the single shell path. State read 0 before and after every command. Verified online after collection. No surviving process.

Partition trace, outputs under the external trace directory, exit 0 each, stderr empty each:
normal record start 2026-09-09T17:27:29Z end 2026-09-09T17:27:32Z, load 0.73 0.96 1.02, seed 1 warmup 0 moves 20 maxdepth 6 iters 1000.
direct-key record start 2026-09-09T17:27:32Z end 2026-09-09T17:27:36Z, load 0.75 0.96 1.02, same workload.
Raw files made read-only by the collector after collection. The external trace directory is fresh; no prior attempt wrote there.

Count runs, telemetry on timers off, seed 1 warmup 20 moves 80 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T17:27:36Z end 2026-09-09T17:27:46Z, load 0.75 0.96 1.02.
run2 direct-key start 2026-09-09T17:27:46Z end 2026-09-09T17:27:56Z, load 0.79 0.97 1.02.
run3 direct-key start 2026-09-09T17:27:56Z end 2026-09-09T17:28:06Z, load 0.98 1.00 1.03.
run4 normal start 2026-09-09T17:28:06Z end 2026-09-09T17:28:16Z, load 1.06 1.02 1.03.
run5 normal start 2026-09-09T17:28:16Z end 2026-09-09T17:28:26Z, load 1.12 1.03 1.04.
run6 direct-key start 2026-09-09T17:28:26Z end 2026-09-09T17:28:36Z, load 1.10 1.03 1.04.
run7 direct-key start 2026-09-09T17:28:36Z end 2026-09-09T17:28:46Z, load 1.09 1.03 1.03.
run8 normal start 2026-09-09T17:28:46Z end 2026-09-09T17:28:57Z, load 1.15 1.05 1.04.

Per-run load5 values never reached the 2.0 abort gate. Half-gate load5 before run1 was 0.96 and before run5 was 1.03, both below 2.0, so no wait was required. All eight timed runs exited 0. No anomaly.
