# Blocked Step 3 attempt, 2026-09-10 06:17Z

The collector verified all six hashes, then terminated `BLOCKED reason=baseline_mode` because the frozen baseline artifact carries mode 444 and cannot be executed directly. No prewarm or binding run started, CPU 15 was never taken offline, and no evidence was consumed.

Fixed by pre-staging a hash-verified mode-555 copy of the baseline for the anchor runs.
