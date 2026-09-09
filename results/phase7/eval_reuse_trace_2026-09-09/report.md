eval-reuse fixed-work diagnostic, accepted measured-only results 2026-09-09
============================================================================

Scope: ARCHIVE plus DIAGNOSTIC TRACE only. No production cache, no
optimization, no commits, no staging. Raw trace retained in /tmp, not in git.

Headline (measured only, ROWCHECK OK against the trace-on PROFILE_V3 row)
--------------------------------------------------------------------------
eval_requests 26632382 (exactly the required value)
distinct_boards 9031593
repeats 17600789 (same_parent 684163, cross_parent 16916626)
same_parent_repeats equals PROFILE eval_memo_hits 684163 exactly
requesting_parent_expansions 599166 equals PROFILE parents 599166 exactly,
  so parent distance covers all expansions (scope all-expansions)
live_attainable_hits 13357692, live_attainable_cross_hits 13106967
fingerprint_collisions 164123
calibration: full-associative LRU at capacity 16777216 hits 17600789,
  equal to expected eval minus distinct exactly
fully_associative_lru_hits:
  1024: 4685129, 4096: 7590760, 16384: 9921830, 65536: 13412419,
  262144: 17498204, 1048576: 17598837 (monotone, all <= repeats)
set_associative_hits: 24 configs, 1/2/4/8-way x 6 capacities
  (e.g. 8-way/1048576: 5993345, 1-way/1024: 620614)

Parent identity and distance definitions
----------------------------------------
An expansion parent is identified by (measured reset epoch, NodeId).
NodeId equality across different measured moves is cross-parent, because
arena indices are reused across ArenaReset boundaries. The engine clears
its eval memo in every expand_parent call, so the memo is parent-local and
same-parent repeats equal eval_memo_hits exactly.
Parent distance is defined only for cross-parent repeats: the number of
distinct request-bearing expansion parents first observed after the previous
request for the same board and before the current request is processed. A
current parent first seen on this request is excluded. Same-parent repeats
and first requests have no parent distance. Because every PROFILE
parent expansion issued at least one request (599166 of 599166), the
measured parent distance covers all expansions.
Request distance is unchanged: the number of eval requests issued strictly
between two consecutive requests for the same board, in log2 buckets.

Cold, capacity, and conflict miss partitions (per config, in summary.json)
--------------------------------------------------------------------------
For every cache config, misses split into cold (first-ever requests,
equal to distinct_boards 9031593), capacity (misses remaining under
fully-associative at the same capacity), and conflict (fully-associative
hits minus set-associative hits at the same capacity; zero by construction
for fully-associative). Every config reconciles:
hits + cold + capacity + conflict == eval_requests 26632382.
Set-associative capacity misses equal fully-associative capacity misses at
the same capacity; all conflict counts are nonnegative; hits are monotone
in capacity and in ways. Examples: FA 1024 capacity 12915660;
FA 1048576 capacity 1952; 8-way/1048576 conflict 11605492;
1-way/1024 conflict 4064515.

Reconciliation identities (all pass)
------------------------------------
same_parent + cross_parent == repeats (684163 + 16916626 == 17600789)
repeats + distinct == eval (17600789 + 9031593 == 26632382)
calibration_hits == calibration_expected_hits == eval - distinct
source_depth_matrix total == eval (26632382)
request_distance_log2_hist total == repeats (17600789)
parent_distance_hist total == cross_parent_repeats (16916626, by
  construction the parent histogram counts only cross-parent repeats)
records 64500912 == measured eval 26632382 + measured node 25757237
  + measured reset 80 + warmup eval 6132302 + warmup node 5978891
  + warmup reset 20
warmup excluded at the first measured move: 6132302 eval, 5978891 node,
  20 reset skipped; live set, board map, repeat history, and parent birth
  log cleared there, so warmup cannot prime measured metrics
measured NodeLive 25757237 == materialized_nodes 25757157 + 80,
  one root NodeLive per measured move
spool_bytes 532647640 == eval_requests x 20 exactly
exact-board identity uses 64-byte board-image dict keys; fingerprint
  collisions (164123) are reported separately and are far below repeats

Noninterference (trace-on vs trace-off PROFILE_V3, 67 fields)
--------------------------------------------------------------
All work/result counters bit-identical (evals 26632382,
transitions 26632382, searches 1142252, parents 599166,
raw_landings 65566529, unique_candidates 32446394,
rule_transitions 32446394, eval_memo_hits 684163,
eval_computed 25948219, materialized_nodes 25757157,
transposition_merges 875225, promotions_refused 27680,
path_states 76513, mem_retained 266338276, arena 208720320,
idmap 4348340, dead_moves 0, games 0, texhaust 0, replay_failures 0).
Only 19 timing/throughput fields differ (total_s, min/median/p95/p99/max_ms,
emove_*_ms, evals/transitions/searches_per_s, setup_ms, run_ms, path_ms,
apply_ms, init_ms). Zero non-timing diffs.
Full rows: profile_trace_on.txt, profile_trace_off.txt in this directory.
The dedicated binary now rejects --eval-trace with telemetry off before
creating any file (focused CTest eval_trace_rejects_telemetry_off).

Determinism
-----------
Raw trace byte-identical across two independent collections with the final
dedicated binary build (sha256 below).
Analyzer deterministic: 20M-record prefix reruns with chunk sizes 65536
vs 8192 give byte-identical summaries
(sha256 d548089be3fc366cb326d887ee13bd638e4d7c418fd75436bca9df07b26d824b).
Selftest chunk-invariance plus LRU-vs-reference equivalence: 118 checks pass,
including a regression fixture with the same numeric NodeId in two measured
moves classified as cross-parent.

Memory and runtime
------------------
Prefix proof (13M records, hard 2 GiB address-space cap): peak RSS
147900 KiB (144 MiB) under the 512 MiB ceiling, exit 0, 10.33 s wall.
Full analysis (hard 4 GiB address-space cap via ulimit -v 4194304):
peak RSS 3011796 KiB (2.87 GiB) below 4 GiB, exit 0, wall 30:44.61
(user 1829.89 s, sys 3.23 s), no surviving process afterward.

Tests
-----
All pass: eval_reuse_analyze_selftest (118 checks), eval_trace_noninterference,
eval_trace_absent_from_normal_build, eval_trace_rejects_telemetry_off,
eval_reuse_trace_warmup_exclusion, eval_reuse_analyze_bounds,
tetris_engine_tests, profile_value_tests, profile_value_determinism,
profile_value_no_hold, profile_value_telemetry_off, profile_value_timers_off,
candidate_partition_selftest, path_differential.

Rejected prior attempts
-----------------------
See rejected_attempt_2026-09-09.md (warmup-contaminated in-memory run) and
rejected_attempt_parent_identity_2026-09-09.md (NodeId-only parent identity,
same_parent 684196). Their summaries and reports were deleted; this report
and the current summary.json are the only accepted figures.

Limitations
-----------
Single-threaded analyzer; full run takes about 31 min wall.
Results are specific to seed 1, frozen_29d.bin, iters 1000, maxdepth 6,
80 measured plus 20 warmup moves. Trace (5.16 GB) lives in /tmp only.
