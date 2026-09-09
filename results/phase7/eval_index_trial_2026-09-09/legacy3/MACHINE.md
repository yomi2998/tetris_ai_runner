# Legacy3 machine record, 2026-09-09

Source commit: a716223.
Selector commit: d60c5bd.
Execution HEAD: e8c92e7bef6d1c50bec5e3b6f18c830dda136d5c.
Ancestry check: a716223, 7f94be7, and d60c5bd are ancestors of HEAD.
Tracked tree: clean, no tracked modifications, no staged changes. Untracked files only.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: gcc 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Load precheck: 0.60 0.67 0.97 at first sample, five minute value 0.67 below 1.5. No wait loop needed.

CPU 15 continuous offline window: 2026-09-09T08:36:38Z to 2026-09-09T08:38:44Z.
CPU 15 was taken offline once before prewarm, stayed continuously offline through both prewarm runs and all six timed runs, and was restored online by the single shell trap after the final run. Verified online after collection.

Prewarm, outputs discarded, exit 0 each:
Legacy: start 2026-09-09T08:36:38Z end 2026-09-09T08:36:39Z, moves 40 iters 200 quiet version 2 telemetry off.
B: start 2026-09-09T08:36:39Z end 2026-09-09T08:36:40Z, moves 40 iters 200 quiet version 3 telemetry off.

Artifact hashes verified before prewarm:
Legacy /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline = 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376.
B /home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_b = e5c55131f9313ee9f7d5a1f3a508fc2efbbddfe225be56076f84b6ec095c41ce.
Parameters /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
All read only. Execution used only these absolute paths.

Per pair load gate: load5 before pair 1.1 was 0.67, before pair 1.2 was 0.77, before pair 1.3 was 0.83. No wait needed. Per run load1 and load5 are recorded in MANIFEST.txt. All six timed runs exited 0. No surviving process. No anomalies.
