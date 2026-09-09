Rejected figures: NodeId-only parent identity, 2026-09-09
=========================================================

Attempt: streaming analyzer with warmup exclusion (tests/eval_reuse_analyze.py
before the parent-identity fix) run over the full 80+20-move trace
/tmp/eval_reuse_2026-09-09-seed1.evtrc. Completed exit 0 under the 4 GiB cap.

Rejected headline figures (parent-misclassified, NOT accepted):
eval=26632382 distinct=9031593 repeats=17600789 same_parent=684196
cross_parent=16916593 attainable=13357692 attainable_cross=13106967
collisions=164123.

Reason rejected: the analyzer classified same parent by NodeId alone.
NodeIds are arena indices reused across measured-move ArenaReset
boundaries, so 33 repeats requested by a different expansion than the
previous request for the same board were counted as same-parent. The exact
no-eviction parent-local memo count is eval_memo_hits=684163 (the engine
clears the eval memo in every expand_parent call), which must equal
same_parent_repeats. The prior summary.json and report.md presenting 684196
were deleted; this note and rejected_attempt_2026-09-09.md are the only
records. No 684196 figure is presented as accepted anywhere.

Repair: expansion-parent identity is now (measured reset epoch, NodeId);
NodeId equality across different moves is cross-parent. Corrected figures
are same_parent=684163, cross_parent=16916626, requesting parents 599166
equal to PROFILE parents, scope all-expansions. See report.md.
