# Selector machine record, 2026-09-09

Source commit: a716223.
Execution HEAD: 7f94be79647dbe6c06eb0e18c4add7b67d014413.
Ancestry check: a716223 is an ancestor of HEAD.
Tracked tree: clean, no tracked modifications, no staged changes. Untracked files only.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: gcc 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Load precheck samples (1min 5min 15min, UTC):
sample1 1.59 1.57 1.34 2026-09-09T08:22:17Z
sample2 1.46 1.54 1.34 2026-09-09T08:23:17Z
sample3 1.87 1.63 1.39 2026-09-09T08:24:17Z
sample4 1.11 1.49 1.36 2026-09-09T08:25:17Z, quiet threshold met (5min 1.49 below 1.5).

CPU 15 offline windows (SMT sibling of CPU 7):
prewarm window 2026-09-09T08:25:30Z to 2026-09-09T08:25:37Z.
timed window 2026-09-09T08:25:51Z to 2026-09-09T08:28:52Z.
CPU 15 restored online after both offline windows and verified online after collection.

Prewarm, outputs discarded, exit 0 each:
A start 2026-09-09T08:25:34Z end 2026-09-09T08:25:35Z.
B start 2026-09-09T08:25:35Z end 2026-09-09T08:25:37Z.
Prewarm command used moves 40 iters 200 telemetry off.

Artifact hashes:
A copy /home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_a = 35b6c5bd095ab02450de9004cc44e066a2e7f26d5af2eaa010dd2a6f5ab698f2, matches local build and runbook.
B copy /home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_b = e5c55131f9313ee9f7d5a1f3a508fc2efbbddfe225be56076f84b6ec095c41ce, matches local build and runbook.
Parameters artifacts/frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
Copies made read-only, execution used only the copies.

Per-run load samples are recorded in MANIFEST.txt. No pair triggered the 2.0 load abort gate. All eight timed runs exited 0. No surviving index process.

Protocol deviations with no observed validity effect: CPU 15 used two offline windows with a 14-second online gap between prewarm and timed collection, although it was offline for every prewarm and timed run. Timed commands recorded the parameter path as repository-relative `artifacts/frozen_29d.bin` rather than the runbook's absolute spelling; the fixed repository working directory resolved the same file whose required hash was verified.
