# Child SoA telemetry-off selector verdict, 2026-09-09

Terminal classification: ABORTED.

No timed row was collected, so no raw inputs, ratios, medians, or spreads exist. No selector class applies.

Collection record: one collector invocation, exit 31. Prewarm normal exited 126 at 2026-09-09T15:43:38Z under load 0.93 0.80 0.75 with CPU 15 offline. Prewarm SoA and all eight timed runs never started.

Root cause: the frozen selector artifact copies are mode 444 with no execute bit while the frozen collector executes those paths directly. The shell refused execution (exit 126, permission denied) before any benchmark logic ran. Controls were otherwise green: hashes verified, precheck load5 0.80 below 1.5, governor performance, EPP performance, boost 1, CPU 15 offline at every boundary and restored online with no surviving process.

This ABORTED verdict carries no performance information. It does not select, reject, or rank the Child SoA trial and must not be read as a speed result. The accepted telemetry-on semantic and count gate at b1ab3bc is unaffected.

Advancement boundary: no selector classification was produced. No legacy execution, qualification, production change, or appended-pair continuation is authorized under this RUNBOOK. A fresh attempt requires a revised frozen protocol that resolves the mode-444 direct-execution conflict, plus new frozen collector hash and fresh paths. Retrying or repairing under the current frozen RUNBOOK is forbidden.

ABORTED
