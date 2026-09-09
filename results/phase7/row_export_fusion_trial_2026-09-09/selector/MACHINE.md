# Row export fusion telemetry-off selector machine record, 2026-09-09

Execution HEAD: 6dd157aa9bf78e22e7af69e3c3280cc31a7df83d.
Ancestry: 2c75c94 present (accepted row fusion count gate).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: /home/icly/Documents/tetris_ai_runner/results/phase7/row_export_fusion_trial_2026-09-09/selector/collect.sh, SHA-256 02273adaa2a1c5b957051d3efb96707685f1a9e923afbb9bf4a90d4bb78cea04, invoked exactly once. Collector exit 0, terminal COLLECTED.
No profile or prewarm command was invoked outside the collector. The collector was never invoked again. Zero reruns.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal selector copy = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, mode 555, executable.
row fusion selector copy = ce2bd5a65967772bdbb89ac25b94a009c4d189cb1396a16a38ce96c27594ee7c, mode 555, executable.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
accepted count verdict PARENT_VERDICT.md = 6211dd00e6b554a3338520fb4285a2c4d4c098a4db81131d11a03da3bc790a19.

Label mapping: collector template labels spell the trial variant `soa` in log labels and filenames such as `prewarm_soa` and `run2_soa`. Every such command provably invoked the hash-verified row fusion artifact. Raw files were not renamed.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 0.99 0.93 0.99 at 2026-09-09T19:42:51Z, five minute value 0.93 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T19:42:51Z to 2026-09-09T19:45:33Z.
CPU 15 was taken offline once before prewarm, read 0 before and after every prewarm and timed command, and was restored online after run 8. Verified online after collection.

Prewarm, stdout and stderr discarded to /dev/null, exit 0 each:
prewarm normal start 2026-09-09T19:42:51Z end 2026-09-09T19:42:52Z, load 0.99 0.93 0.99.
prewarm row fusion (label prewarm_soa) start 2026-09-09T19:42:52Z end 2026-09-09T19:42:53Z, load 0.99 0.93 0.99.

Timed runs, telemetry off timers off, seed 1 warmup 20 moves 200 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T19:42:53Z end 2026-09-09T19:43:13Z, load 0.99 0.93 0.99.
run2 row fusion start 2026-09-09T19:43:13Z end 2026-09-09T19:43:33Z, load 1.21 0.98 1.01.
run3 row fusion start 2026-09-09T19:43:33Z end 2026-09-09T19:43:53Z, load 1.15 0.98 1.01.
run4 normal start 2026-09-09T19:43:53Z end 2026-09-09T19:44:13Z, load 1.71 1.13 1.06.
run5 row fusion start 2026-09-09T19:44:13Z end 2026-09-09T19:44:33Z, load 1.58 1.14 1.06.
run6 normal start 2026-09-09T19:44:33Z end 2026-09-09T19:44:53Z, load 1.63 1.17 1.07.
run7 normal start 2026-09-09T19:44:53Z end 2026-09-09T19:45:13Z, load 1.66 1.21 1.09.
run8 row fusion start 2026-09-09T19:45:13Z end 2026-09-09T19:45:33Z, load 1.56 1.21 1.09.

Pair order: pair 1 normal then row fusion, pair 2 row fusion then normal, pair 3 row fusion then normal, pair 4 normal then row fusion.
Pair-gate load5 before pairs 1 through 4 was 0.93, 0.98, 1.14, and 1.21, each below the 2.0 wait gate, so no wait was required. No per-run load5 reached 2.0. All ten commands exited 0. No surviving process.

Anomalies: none. Timer attribution note from the count gate: on fused children safe-margin work moves from the policy scope into the eval-miss scope; this has no effect here because timers and telemetry were off for every timed row and no timer field was used.

rows.txt is the exact eight-file execution-order concatenation of the retained runN stdout files. All eight rows are single PROFILE_V3 lines with moves=200, warmup_moves=20, seed=1, iters=1000, maxdepth=6, budget_ms=0.000, mode=iters, telemetry=off, dead_moves=0, games=0, and all stderr files empty.
