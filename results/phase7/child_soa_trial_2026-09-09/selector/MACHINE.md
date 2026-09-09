# Child SoA telemetry-off selector machine record, 2026-09-09 (ABORTED)

Execution HEAD: 26384a5631ea7eee9420128ca27628f08be39f0b.
Ancestry: b1ab3bc present (accepted Child SoA count gate).
Tracked state at collection: clean, no tracked modifications, no staged changes.
Collector: results/phase7/child_soa_trial_2026-09-09/selector/collect.sh, SHA-256 0b2708056843ee08b5816d8448c263ac95ff8f742020a2572c688383a80f667b, invoked exactly once. Collector exit 31, terminal ABORTED.
No profile or prewarm command was invoked outside the collector. The collector was never invoked again. Zero reruns.

CPU: AMD Ryzen 7 7700 8-Core Processor.
Kernel: 7.2.3-1-cachyos.
Compiler: g++ (GCC) 16.2.1 20260810.
CPU 7 governor: performance.
CPU 7 energy performance preference: performance.
Global boost: 1, unchanged.

Artifact hashes verified before collection, all match RUNBOOK:
normal selector copy = 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765, mode 444.
SoA selector copy = 721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91, mode 444.
parameters frozen_29d.bin = ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037.
accepted count verdict PARENT_VERDICT.md = 94a95df71e382d59d216b7be8cc4ce5458a99a84f60aae36d154c9963878062d.

CPU 15 control preflight passed with CPU 15 online and noninteractive write authority confirmed before any evidence was created.
Load precheck: 0.93 0.80 0.75 at 2026-09-09T15:43:38Z, five minute value 0.80 below 1.5. No wait loop needed.

CPU 15 offline window: 2026-09-09T15:43:38Z to 2026-09-09T15:43:38Z.
CPU 15 was taken offline once before prewarm, read 0 at the prewarm start and at the prewarm completion, and was restored online by the single shell trap. Verified online after collection (CLEANUP_RESTORE before=0 after=1 result=pass_attempt_1).

Prewarm, stdout and stderr discarded to /dev/null:
prewarm normal start 2026-09-09T15:43:38Z end 2026-09-09T15:43:38Z, load 0.93 0.80 0.75, exit 126.
prewarm SoA never started. Zero timed runs started. Zero timed rows collected.

Root cause of exit 126: the frozen selector artifact copies carry mode 444 with no execute bit, and the frozen collector executes them directly. The shell reports permission denied (exit 126) before the profile binary starts. No benchmark logic ran. This is a frozen-protocol defect: the RUNBOOK requires read-only mode 444 artifacts and direct execution of those same paths in one collection. Correcting it requires a new frozen protocol revision, not a retry under this RUNBOOK.

Partial evidence preserved unchanged: collect.log with exactly one START, one COMMAND, one END, and one TERMINAL=ABORTED line. No runN stdout or stderr file was created. No rows.txt exists. No replacement or extra command was run. No surviving process.
