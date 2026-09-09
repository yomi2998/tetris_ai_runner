Rejected attempt record, 2026-09-09
==================================

Attempt: in-memory analyzer (tests/eval_reuse_analyze.py as of HEAD b3edf56
plus the prior uncommitted diff) run over the full 80+20-move trace
/tmp/eval_reuse_2026-09-09-seed1.evtrc. Process aborted by orchestrator order
after about 16.6 GB RSS. A one-line result and summary.json were produced
before the stop.

Rejected headline figures (warmup-contaminated, NOT accepted):
eval=32764684 distinct=11141075 repeats=21623609 cross=20833031
attainable=16390445 attainable_cross=16100755 collisions=207251.

Reason rejected: the trace contains 20 warmup moves and the analyzer counted
them. Measured-only truth is 26,632,382 eval requests; warmup contributed
6,132,302 eval, 5,978,891 node, and 20 reset records. Every derived metric
(distinct, repeats, distances, matrices, collisions, simulations, attainable)
was therefore contaminated. summary.json and the findings report presenting
these figures were deleted; this note is the only record.

What remains valid from that attempt: the raw trace itself
(sha256 5956cfc5015360cd159ce023f7f05f8ca81119bb872c68dd9a7b675c16bb684f,
kept in /tmp, not in git), the trace-on/trace-off PROFILE_V3 rows
(profile_trace_on.txt, profile_trace_off.txt, bit-identical work/result
counters), and the run provenance (manifest.txt).

Repair direction: streaming analyzer with warmup exclusion at the first
measured move; trace path compiled only into the eval_reuse_trace target
behind TETRIS_EVAL_REUSE_TRACE; accepted metrics must show exactly
26,632,382 measured eval requests with calibration on measured data only.
