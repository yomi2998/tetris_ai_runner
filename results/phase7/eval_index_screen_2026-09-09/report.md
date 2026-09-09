# Offline exact node-backed evaluation-index screen, 2026-09-09 (repair revision)

## Scope and authorization

Diagnostic screen only. No production engine or build behavior was modified,
nothing is committed or staged. The raw trace was read in place from /tmp
and neither copied into git nor regenerated.

Two superseded executions are recorded here as rejected infrastructure
execution, not evidence, and no number below derives from them:

- The original Python full-matrix workflow timed out at the tool cap with no
  full-trace output.
- The untracked tests/eval_index_screen.py behind that workflow and its
  generated tests/__pycache__ directory are deleted as superseded invalid
  evidence. The accepted analyzer source is untouched.

This revision regenerates the full summary deterministically from the
repaired core described below.

## Method

Standalone core tests/eval_index_screen_core.cpp (untracked, hand-compiled to
/tmp/eval_index_screen_core, absent from every build file) streams the
5.16 GB trace once and simulates the complete required matrix in one pass:

- Measured-only filtering: warmup move 0xFFFF records count as skipped and
  never touch state; state starts empty so warmup cannot prime metrics.
- Record validation matching tests/eval_reuse_analyze.py: record kind range,
  reserved byte at offset 3, reserved bytes at offsets 6 and 7, reserved word
  at offset 12, and zero word payload on reset records. Any violation aborts
  with nonzero exit; the trace passes all of them.
- ArenaReset handling: live-node state is cleared for ids at or above the
  reset new count, survivors below it are kept (exactly the accepted
  analyzer semantics), every index is fully cleared (slots to empty, 80
  clears per design), and survivors repopulate only from following NodeLive
  records.
- Parent identity is (measured reset epoch, NodeId). Requests repeating the
  previous requester of the same exact board bypass as parent-local memo
  hits; only memo misses are queried.
- Every claimed index hit requires full exact 64-byte Board equality between
  the requested board and the currently live referenced node. Fingerprints,
  set indexes, and tags never establish equality; a tag match followed by a
  board mismatch counts as a tag collision, never a hit.
- Associativity model: ordinary per-set LRU, not stateless hash-victim
  placement. Each set keeps an explicit MRU-first way order (one order byte
  per slot for ways 2/4/8, zero metadata for direct mapped). Insertion
  refreshes the slot when the board is already indexed under live
  verification, otherwise it evicts the LRU way. All order bytes are
  included in the per-design memory totals, so the reported index bytes are
  complete: slots (4 or 8 bytes each) plus LRU order bytes.
- Hashes: raw_current_fnv1a (FNV-1a 64 over 8 occupancy words, identical to
  engine occupancy_hash), splitmix64_finalized_fnv (splitmix64 of the raw
  value), murmur_fmix64_finalized_fingerprint (murmur3 fmix64 of the rot-xor
  fingerprint). Set index masks the finalized hash to the power-of-two set
  count; the 8-byte slot tag is the high 32 bits of the finalized hash.
- Grid: capacities 65536, 262144, 524288, and 1048576 entries, plus a
  2097152-entry sensitivity point; ways 1/2/4/8; slot widths 4-byte NodeId-only
  and 8-byte tag plus NodeId. Memory eligibility is calculated separately for
  every layout below. The grid contains 60 physical LRU indexes and 120 logical
  designs (hit and probe counts shared per physical index; full Board comparison
  counts differ by slot width).
- Determinism: no randomness, no pointer-derived hashing, no
  iteration-order dependence; fixed config order and deterministic LRU
  updates.

## Reconciliation identities (all pass, asserted by the core, exit 0)

- eval_requests 26632382, distinct eval boards 9031593, same-parent bypass
  684163, memo-miss queries 25948219, node events 25757237, reset events 80.
- bypass equals PROFILE eval_memo_hits 684163; queries equal PROFILE
  eval_computed 25948219; every design reports lookups equal to queries,
  hits plus misses equal to queries, and 80 reset clears.
- Cross-check against the accepted analyzer: raw 8-way/1048576 index hits
  5858030 sit below the analyzer's board-identity cache value 5993345 at
  the same shape, as reset and invalidation semantics require. Every design
  hits at or below the live-attainable cross count 13106967.
- Monotonicity holds for every hash in capacity and in ways; 4-byte and
  8-byte slot kinds report identical hits and probe counts by construction.

## Resource bounds

- Full screen: exit 0, wall 1:42.05 (user time about 95 s), peak RSS
  1479308 KiB (1.41 GiB) under the hard 4 GiB address-space cap
  (ulimit -v 4194304), no surviving process.
- Bounded prefix validation (2M measured events, 64K-cap subset grid, hard
  1024 MiB address space): exit 0, BOUNDS OK peak_rss_kib=198096
  (193.5 MiB) at or below the 512 MiB cap, wall 0.50 s.
- A full-matrix prefix probe reached 600144 KiB (586 MiB), so the accepted
  512 MiB validation uses the subset grid above; the full matrix is validated only
  under the 4 GiB cap. That probe is superseded, not evidence.
- Peak live nodes over the full trace: 432781 (final live 281352).
- Prefix determinism: repeated subset runs reproduce identical figures;
  full rerun reproduces all headline identities exactly.

## Memory fit: raw arithmetic kept separate from practical eligibility

Retained baseline 266338276 B, cap 268435456 B, headroom 2097180 B.
Raw arithmetic fit (retained plus index at or below cap): 42 of 120.
Practical eligibility (retained plus index plus an explicit 65536-byte
minimum remaining safety margin at or below cap, recorded per design as
practically_eligible_without_arena_change): 36 of 120. The margin exists
because a 28-byte remainder is not a shippable budget.

The six 2,097,152-byte designs (4-byte 524288-entry 1-way and 8-byte
262144-entry 1-way across all three hashes) satisfy raw arithmetic with
exactly 28 bytes to spare and are practically ineligible under the margin.
Best practical layout is unchanged: splitmix 262144-entry 8-way 4-byte,
13071432 hits (50.375 percent of memo misses), index 1310720 B.

## Results

Hits by capacity, 8-way, 4-byte slots:

| hash | 64K | 256K | 512K | 1M | 2M |
|---|---|---|---|---|---|
| raw fnv1a | 3902324 | 4958954 | 5392626 | 5858030 | 6284408 |
| splitmix | 11956889 | 13071432 | 13105694 | 13106953 | 13106967 |
| fmix | 11952307 | 13070481 | 13105580 | 13106941 | 13106967 |

Ways scaling, splitmix, 4-byte slots (1-way / 2-way / 4-way / 8-way):
64K: 10773347 / 11428704 / 11772145 / 11956889.
1M: 12894833 / 13082168 / 13105644 / 13106953.
2M: 12998904 / 13100274 / 13106869 / 13106967.

Standouts:

- Highest counts: splitmix (and fmix) 2097152-entry 8-way, 13106967 hits
  (50.512 percent of queries), avg 4.50 probes, 0 exact-mismatch tag
  collisions for splitmix (fmix shows 309907 tag collisions at the same
  hits, extra comparisons only). Replacements 6 to 13: capacity covers the
  live set within each move. Exceeds headroom (10.0 MiB).
- Raw FNV low bits are weak for set mapping: 17.4M to 21.4M replacements
  at 8-way across caps versus 6 to 8.7M for the mixers, and roughly half
  the hits (raw 1M 8-way 5858030 versus splitmix 13106953). Any production
  index must use a mixed hash, never raw low bits.
- 8-byte tags cut full Board comparisons substantially wherever the mapping
  collides, at zero hit cost; exact verification remains mandatory.

## Practical-layout cost table

residual = 268435456 - 266338276 - index_bytes. perlk = comparisons per
memo-miss lookup (25948219 lookups); perhit = comparisons per hit. Hash cost
is integer operations per board hash from the defined functions (time cost
unmeasured): raw FNV-1a 8 xor + 8 mul; splitmix adds 1 add + 3 shift + 3 xor
+ 2 mul; fmix path adds rot-xor fingerprint (8 xor plus rotates) + 3 shift
+ 3 xor + 2 mul. All rows below are practically eligible except the marked
direct-map sensitivities.

| layout | index B | residual B | hits | probes/lk | compares total | per lk | per hit | repl | hash cost |
|---|---|---|---|---|---|---|---|---|---|
| 8B 64K 4-way fmix | 589824 | 1507356 | 11761086 | 2.8661 | 12014949 | 0.4630 | 1.0216 | 8981059 | fp+fmix |
| 8B 64K 4-way splitmix | 589824 | 1507356 | 11772145 | 2.8600 | 11772145 | 0.4537 | 1.0000 | 8965038 | fnv+splitmix |
| 4B 256K 8-way fmix | 1310720 | 786460 | 13070481 | 4.7398 | 54291723 | 2.0923 | 4.1538 | 708730 | fp+fmix |
| 4B 256K 8-way splitmix | 1310720 | 786460 | 13071432 | 4.7319 | 54022283 | 2.0819 | 4.1329 | 689298 | fnv+splitmix |
| 4B 64K 1-way raw (sens.) | 262144 | 1835036 | 2140124 | 1.0000 | 25315653 | 0.9756 | 11.8291 | 23588803 | fnv |
| 4B 64K 1-way splitmix (sens.) | 262144 | 1835036 | 10773347 | 1.0000 | 21288916 | 0.8204 | 1.9761 | 10436584 | fnv+splitmix |
| 8B 64K 1-way raw (sens.) | 524288 | 1572892 | 2140124 | 1.0000 | 2140124 | 0.0825 | 1.0000 | 23588803 | fnv |
| 8B 64K 1-way splitmix (sens.) | 524288 | 1572892 | 10773347 | 1.0000 | 10773347 | 0.4152 | 1.0000 | 10436584 | fnv+splitmix |
| 4B 1M 1-way raw (sens., no fit) | 4194304 | -2097124 | 3305368 | 1.0000 | 24597201 | 0.9479 | 7.4416 | 21854662 | fnv |
| 4B 1M 1-way splitmix (sens., no fit) | 4194304 | -2097124 | 12894833 | 1.0000 | 14130540 | 0.5446 | 1.0958 | 1227796 | fnv+splitmix |
| 8B 1M 1-way raw (sens., no fit) | 8388608 | -6291428 | 3305368 | 1.0000 | 3305368 | 0.1274 | 1.0000 | 21854662 | fnv |
| 8B 1M 1-way splitmix (sens., no fit) | 8388608 | -6291428 | 12894833 | 1.0000 | 12894833 | 0.4969 | 1.0958 | 1227796 | fnv+splitmix |
| 4B 2M 1-way raw (sens., no fit) | 8388608 | -6291428 | 3544591 | 1.0000 | 24383171 | 0.9397 | 6.8790 | 21491264 | fnv |
| 4B 2M 1-way splitmix (sens., no fit) | 8388608 | -6291428 | 12998904 | 1.0000 | 13634802 | 0.5255 | 1.0489 | 631821 | fnv+splitmix |
| 8B 2M 1-way raw (sens., no fit) | 16777216 | -14680036 | 3544591 | 1.0000 | 3544591 | 0.1366 | 1.0000 | 21491264 | fnv |
| 8B 2M 1-way splitmix (sens., no fit) | 16777216 | -14680036 | 12998904 | 1.0000 | 12998904 | 0.5010 | 1.0000 | 631821 | fnv+splitmix |

Reading notes: direct mapping probes exactly once per lookup; its per-hit
compare cost is 1.0 with 8-byte tags but large with 4-byte slots under a
weak hash (raw 64K 1-way: 11.83 compares per hit) because every occupied
probe pays a full compare. Multi-way 4-byte slots pay several compares per
hit (about 4.1 at 256K 8-way). Tags remove non-matching compares without
changing hits. None of these counts is a timing measurement.

## Opportunity extrapolations, labeled and non-predictive

The 78 ns figure is the mean eval-miss time from a different,
component-timer run (eval_miss_ns 2023903666 over eval_computed 25948219,
about 77.9978 ns). Multiplying avoided computes by that mean and dividing
by a candidate total mixes instrumentations: the 10.430 s denominator is
trace-on with timers off, the 8.058 s denominator is trace-off with timers
off, and neither quotient predicts net wall speed. Lookup probes, exact
Board compares, LRU order updates, insertions, and cache side effects are
all unmeasured, and timer overhead differs across the rows. With that
understood, the two extrapolations are:

- Highest observed counts 13106967: about 1.0223 s of avoided miss time,
  9.802 percent of 10.430 s, 12.687 percent of 8.058 s.
- Best practical layout 13071432: about 1.0195 s, 9.775 percent of
  10.430 s, 12.653 percent of 8.058 s.
- Raw 1M 8-way 5858030: about 0.4569 s, 4.381 percent of 10.430 s,
  5.670 percent of 8.058 s.

These bound nothing about production speed; they only size the reuse
opportunity under stated, mismatched assumptions.

## Required reductions (facts from the frozen ratios)

Current ratios need these candidate-time reductions (1 - target / ratio):
total 1.04155 to 0.90: 13.590 percent; to 0.95: 8.790 percent.
p95 1.02547 to 0.90: 12.235 percent; to 0.95: 7.360 percent.
Qualification itself is measured medians, which an offline screen cannot
supply. Hence PRIMARY_0.90=NO-ADVANCE on this evidence, and whether a
0.95 measured trial is worthwhile is an owner decision (see status.txt).

## Limitations and risks

- Single workload (seed 1, frozen_29d.bin, 1000 iters, depth 6, 80 measured
  plus 20 warmup moves). No claim across other workloads.
- Counts are opportunity counts, never time savings. Real lookup cost,
  memory-system effects, and invalidation paths need a measured build.
- LRU order bytes model one production-plausible policy; true NRU, CLOCK,
  or partitioned policies would differ within the same capacity class.
- The 8-byte tag is 32 bits; no false hit is possible by construction
  (exact verification), but tag-collision compare waste grows where the
  mapping is weak.

## Files

- summary.json: full-trace aggregate metrics and all 120 designs, including
  practically_eligible_without_arena_change and the 65536-byte margin.
- manifest.txt: commands, hashes, resource evidence, artifact sizes.
- status.txt: PRIMARY_0.90=NO-ADVANCE,
  FALLBACK_0.95_MEASURED_TRIAL=OWNER-DECISION.
