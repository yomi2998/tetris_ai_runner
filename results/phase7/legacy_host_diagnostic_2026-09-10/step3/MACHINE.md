# Step 3 machine record, 2026-09-10 (ABORTED)

Execution HEAD: fd614b97b113381e363008d0f46d263cf67f8fd1.
Ancestry: 578db3b (frozen runbook) present.
Tracked state at collection: clean.
Collector: step3/collect.sh, SHA-256 2adfc6eb318b027c08880e3fb1abebc19bcc8b0ef67886e21568120916d56e51, invoked exactly once. First invocation of the unamended collector (hash 89d... recorded in blocked_attempt archive) terminated BLOCKED reason=baseline_mode and ran nothing; that attempt is preserved in blocked_attempt_2026-09-10_0617Z/.

Collector exit: 137-classified ABORTED, sole terminal TERMINAL=ABORTED reason=nonzero_exit label=run1_L1.
Records: 5 START / 5 COMMAND / 5 END (3 prewarm value/driver/baseline plus run1_L0 and run1_L1), all cpu15=0 at every boundary.
CPU 15: one continuous offline window from prewarm through run1_L1, restored online by the collector trap (CLEANUP_RESTORE before=0 after=1 result=pass_attempt_1). Verified online after the abort.

Prewarm: value, driver, baseline (555 copy, hash 84cb7a30...), all exit 0.
run1_L0: exit 0, total_searches=692749398 over the full 145544-parent replay, DIAG_V1 row preserved in run1_L0.txt, wall roughly 93 minutes (06:32:26 to 08:05 window start).
run1_L1: killed by the operating system (exit 137, SIGKILL) at 2026-09-10T08:05:08Z after exhausting RAM and swap; stderr empty; the owner directly observed 100 percent RAM and 100 percent swap during the run. The Linux OOM killer terminated the process.

Label mapping: template labels map arm names directly (L0, L1, L2, V); no template spelling issue in this collector.
Timer attribution and audit-overhead disclosures: carried per runbook section 8; not yet applicable to any binding ratio since the block aborted.

No replacement run, no appended round, no deletion. All partial evidence preserved.
