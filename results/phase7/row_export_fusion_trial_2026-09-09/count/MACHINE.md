# Row export fusion count gate machine record, 2026-09-09

Execution HEAD: 9a5a163477635832b037311cb1a4dc36c1e6763e.
Ancestry: abf870d present (trial-only row export fusion implementation).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/row_export_fusion_trial_2026-09-09/count/collect.sh, SHA-256 a1a79cc8dfd014323d0437919c2a05312f5f66308ba6c33f7740889bc335ceff, invoked exactly once. Collector exit 0, terminal COLLECTED.
No recorder, profile, prewarm, or test command was invoked outside the collector. No collector command was repeated. Zero reruns.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection and again during collection, all match RUNBOOK:
normal tetris_profile_value = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765.
row fusion tetris_profile_row_fusion = ce2bd5a65967772bdbb89ac25b94a009c4d189cb1396a16a38ce96c27594ee7c.
normal candidate_partition = 5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1.
row fusion candidate_partition_row_fusion = 71cdadc5981465e602df066b7dfdaf73117bbe107d2aece42844f4ddeee61dd2.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
prerequisite transcript prerequisite_tests_2026-09-09.txt = 5e7a7328b500b330a7b291bb6e1ea74c495dc9c3859a52e2148d0b16c7727361.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 1.03 1.16 1.24 at 2026-09-09T19:24:07Z, five minute value 1.16 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T19:24:07Z to 2026-09-09T19:25:35Z.
CPU 15 was taken offline once before the partition trace, stayed continuously offline through both trace records and all eight count runs, with state read 0 at every start, end, half check, and mid check, and was restored online after run 8. Verified online after collection. No anomaly.

Label mapping: the collector retains template labels that spell the trial variant `soa`, for example trace_soa and run2_soa. Every such command provably invoked the hash-verified row fusion artifact, candidate_partition_row_fusion or tetris_profile_row_fusion, as recorded in MANIFEST.txt and commands.txt. Raw files keep the template spelling and were never renamed.

Timer attribution disclosure: on fused children the safe-margin computation moves from the policy_ns timer scope into the eval-miss scope. Count rows use timers off, so no verdict evidence is affected. Any later timer comparison must not read this shift as a work change.

Partition trace, outputs under the external trace_attempt path row_export_fusion_trial_2026-09-09/trace, exit 0 each, stderr empty each:
normal record start 2026-09-09T19:24:07Z end 2026-09-09T19:24:10Z, load 1.03 1.16 1.24, seed 1 warmup 0 moves 20 maxdepth 6 iters 1000.
row fusion record start 2026-09-09T19:24:10Z end 2026-09-09T19:24:14Z, load 1.11 1.18 1.24, same workload.
inputs.bin, moves.tsv, and run_totals.tsv are byte-identical between variants with hashes c6b5ecf6c2b7e956b7770ea957820d77fefc4fcaad0a9e1be22a55d1d1473da5, 723c2c83ef6de336ef8750d76fec7198cdea2b94545f8d10ad4badfc4d7c674b, and 46934e5f13682b02e1df729c26c86d0f2c74f599f4e6136b3610ce5b3c374785. Both stdout rows carry 67 fields and match on every non-timing field including mem_retained_bytes 266338276 on both sides. Raw files made read-only after collection. The trace directory is fresh; no prior attempt wrote there.

Count runs, telemetry on timers off, seed 1 warmup 20 moves 80 maxdepth 6 iters 1000 quiet-version 3, exit 0 each, stdout is the single PROFILE_V3 row, stderr empty each:
run1 normal start 2026-09-09T19:24:14Z end 2026-09-09T19:24:24Z, load 1.10 1.17 1.24.
run2 row fusion start 2026-09-09T19:24:24Z end 2026-09-09T19:24:34Z, load 1.16 1.18 1.25.
run3 row fusion start 2026-09-09T19:24:34Z end 2026-09-09T19:24:44Z, load 1.37 1.23 1.26.
run4 normal start 2026-09-09T19:24:44Z end 2026-09-09T19:24:54Z, load 1.31 1.22 1.26.
run5 normal start 2026-09-09T19:24:54Z end 2026-09-09T19:25:04Z, load 1.34 1.23 1.26.
run6 row fusion start 2026-09-09T19:25:04Z end 2026-09-09T19:25:14Z, load 1.45 1.26 1.27.
run7 row fusion start 2026-09-09T19:25:14Z end 2026-09-09T19:25:24Z, load 1.61 1.30 1.28.
run8 normal start 2026-09-09T19:25:24Z end 2026-09-09T19:25:34Z, load 1.59 1.30 1.28.

Per-run load5 values never reached the 2.0 abort gate (maximum 1.30). Half-gate load5 before run1 was 1.17 and before run5 was 1.23, both below 2.0, so no wait was required. All eight timed runs exited 0. No surviving process.

Identity results, computed from the preserved bytes with read-only analysis:
All 67 fields present in every row. Normal repeats run1 run4 run5 run8 identical on all non-timing fields. Trial repeats run2 run3 run6 run7 identical on all non-timing fields. Cross-engine, every non-timing field is identical across all eight rows, including mem_retained_bytes 266338276 on every row, difference zero, as required for a trial that adds no storage. All 37 frozen expected values matched exactly in every row, including evals 26632382, transitions 26632382, searches 1142252, parents 599166, raw_landings 65566529, unique_candidates 32446394, rule_transitions 32446394, eval_memo_hits 684163, eval_computed 25948219, materialized_nodes 25757157, transposition_merges 875225, pending_end_max 425068, path_states 76513, and timers off with telemetry on.

Anomalies: none. No extra command, no replacement row, no subset selection, no CPU-state breach, and no load breach.
