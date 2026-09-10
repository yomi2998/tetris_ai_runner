# Screen stage machine record, 2026-09-10

Execution HEAD: 041be1e1862024af3603f9f65db946e86be1432c.
Ancestry: 911e876 present (collector fix and archived blocked smoke attempts).
Tracked state at collection: clean.
Collector: ../collect.sh, SHA-256 5ba94d5aa1bc45ac436c128dd1111320175ab7bbaccc72870cdf3179e5eff502, invoked exactly once, one terminal TERMINAL=COLLECTED, exit 0.

Precondition hashes verified in-log: driver quality_match b92ee3b2e68661c1d822de25fddc532ed842a90cfaf2728cab9d44a4f6be9d9d, parameters ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037. Normal value binary unchanged at 1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765 (verified pre-collection, read-only check).

Load precheck: initial load5 1.99 above 1.5; rechecks at attempts 1 through 4 (1.78, 1.60, 1.55, 1.35); passed at attempt 4, 2026-09-10T14:07:01Z.

CPU 15: offline window 2026-09-10T14:07:01Z to 14:09:59Z continuous, state 0 at prewarm and every boundary, restored online, CLEANUP_RESTORE pass.

Collection: one match command, 128 seat-swapped pairs (256 games), seats legacy,value, 20 ms budgets both seats, seed 911, threads 8, max-rounds 3600, frozen parameters both sides. MATCH_EXIT=0, match.stderr.txt empty, 257 CSV lines (header plus 256 games), all seat pairs mixed legacy/value.

End state verified: CPU 15 online, no quality_match process remaining (the initial PROCESS-ALIVE probe was a self-match artifact of the probe's own pattern; ps and pgrep -af confirm none).
