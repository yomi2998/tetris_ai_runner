# Selector verdict, 2026-09-09

Raw per-run inputs in execution order (total_s, p95_ms):
run1 1.1-a A total 21.114 p95 142.584.
run2 1.1-b B total 20.254 p95 132.245.
run3 1.2-a B total 20.592 p95 135.799.
run4 1.2-b A total 21.041 p95 142.707.
run5 1.3-a B total 20.219 p95 132.903.
run6 1.3-b A total 20.980 p95 142.702.
run7 1.4-a A total 21.037 p95 142.899.
run8 1.4-b B total 20.374 p95 134.769.

B divided by A ratios per pair:
pair 1.1 total 0.95927 p95 0.92749.
pair 1.2 total 0.97866 p95 0.95159.
pair 1.3 total 0.96373 p95 0.93133.
pair 1.4 total 0.96848 p95 0.94311.

B/A summary, median over four values as mean of two middle sorted values:
median total 0.96611, spread 0.01939.
median p95 0.93722, spread 0.02410.

Selection check for B:
median total 0.96611 at most 0.99 yes.
median p95 0.93722 at most 1.02 yes.
total spread 0.01939 at most 0.04 yes.
p95 spread 0.02410 at most 0.04 yes.

All B selection conditions hold, so no reciprocal A evaluation changes the outcome.

Controls: CPU 7 pinned, governor performance, EPP performance, boost 1, CPU 15 offline during prewarm and timed block, telemetry off, frozen hashes verified, precheck load gate passed, no abort trigger.

Anomalies: none affecting validity. CPU 15 offline coverage used two windows with a 14-second online gap between prewarm and timing; both prewarm and all timed runs executed with CPU 15 offline. Timed commands used the repository-relative parameter path rather than the absolute spelling in the runbook; it resolved the same hash-verified file from the fixed repository working directory.

Advancement boundary: B is selected only for one fresh three-pair telemetry-off comparison against the frozen legacy baseline. This is not qualification or production cutover.

SELECT-B
