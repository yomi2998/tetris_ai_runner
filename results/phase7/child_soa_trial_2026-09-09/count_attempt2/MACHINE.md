# Child SoA count gate machine record, attempt 2, 2026-09-09

Execution HEAD: da42c5e7e434bd2770a36590e9f6788cd2587986.
Ancestry: e02f476 present, 369d447 present, 19ea6a1 present.
Tracked state at collection: clean, no tracked modifications, no staged changes. Only pre-existing untracked research documents.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal tetris_profile_value = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765.
SoA tetris_profile_child_soa = 721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91.
normal candidate_partition = 5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1.
SoA candidate_partition_child_soa = 6b5b520fdf253def718b3743f8fd53d54eb7636329c83f9c39b2510a65fa582a.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.

Load precheck: 1.24 1.09 1.22 at 2026-09-09T14:39:38Z, five minute value 1.22 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T14:39:38Z to 2026-09-09T14:41:03Z.
CPU 15 was taken offline once before the partition trace, stayed continuously offline through both trace records and all eight count runs, and was restored online by the single shell trap after run 8. State read 0 at trace start, at both trace completions, and before every count run. Verified online after collection.

Partition trace, outputs under trace_attempt2, exit 0 each, stderr empty each:
normal record start 2026-09-09T14:39:38Z end 2026-09-09T14:39:42Z, seed 1 warmup 0 moves 20 maxdepth 6 iters 1000.
SoA record start 2026-09-09T14:39:42Z end 2026-09-09T14:39:45Z, same workload. No dedicated load sample was taken at the SoA trace start; bracketing samples are 1.24 1.09 1.22 at 14:39:38Z and 1.36 1.12 1.23 at 14:39:45Z, so no gate applies.
Raw files made read-only after collection. The trace_attempt2 directory is fresh; no prior attempt wrote there.

Count runs, telemetry on timers off, seed 1 warmup 20 moves 80 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T14:39:45Z end 2026-09-09T14:39:55Z, load 1.36 1.12 1.23.
run2 soa start 2026-09-09T14:39:55Z end 2026-09-09T14:40:05Z, load 1.41 1.14 1.24.
run3 soa start 2026-09-09T14:40:05Z end 2026-09-09T14:40:14Z, load 1.65 1.20 1.26.
run4 normal start 2026-09-09T14:40:14Z end 2026-09-09T14:40:24Z, load 1.70 1.23 1.27.
run5 normal start 2026-09-09T14:40:24Z end 2026-09-09T14:40:34Z, load 1.90 1.29 1.28.
run6 soa start 2026-09-09T14:40:34Z end 2026-09-09T14:40:44Z, load 1.99 1.33 1.30.
run7 soa start 2026-09-09T14:40:44Z end 2026-09-09T14:40:53Z, load 1.99 1.35 1.31.
run8 normal start 2026-09-09T14:40:53Z end 2026-09-09T14:41:03Z, load 2.00 1.37 1.31.

Per-run load5 values never reached the 2.0 abort gate. Half-gate load5 before run1 was 1.12 and before run5 was 1.29, both below 2.0, so no wait was required. All eight timed runs exited 0. No surviving process. No reruns of any recorder, profile binary, or count row: zero reruns.

Anomalies: one discarded pre-collection occurred earlier the same day when CPU 15 offline control failed with permission denied, so two traces plus eight rows ran with CPU 15 online. Every file from that run was deleted and the external directory removed; zero rows were retained. The sudo control path was then verified offline and online, and this fresh collection ran under the continuous window above. A provider 429 error interrupted post-collection evidence finalization only; it touched no run, required no replacement, and all collection bytes were preserved.
