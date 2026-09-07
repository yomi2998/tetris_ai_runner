# Design: fingerprint-gated transposition entries (capacity redesign)

Status: design accepted by the coordinator 2026-09-08; implementation
authorized. Read-only design produced from committed source constants and
recorded campaign rows; every prediction is labeled as such and measured
by the plan in section 5.3. Supersedes the entry-size status quo from
`transposition_reset_design.md` (epoch mechanics carry forward
unchanged).

## 0. Anchors

Measured facts reused: probe 46.94 percent of cycles, materialize 54.29
percent of run at 959.0 ns/node (hotspot attribution); entry 160 B =
152 key + 4 node + 4 epoch; fixed-work demand peaks 448,536 / 429,964 /
408,023 seeds 1/2/3, medians ~320k, timed peak 22,084 (7.2D sizing);
fail-stop 174/200 moves seed 1; post-epoch D/A 0.7487 with bit-identical
work vectors; partition timed legs 100 percent class (a), zero defects,
all seeds.

Memory cost model (derived, cross-checked against two independent
recorded rows): `total = F0 + (1+k) * T * E + A * 324 <= 268,435,456`
with `F0 = 6,185,248` (fixed buffers, EvalCache, queue/stack/frontier
reservations), `k` = rehash-scratch ratio (today 1), `E` = entry bytes,
`A` = arena nodes (stride 324 = 320 + 4). The model reproduces both
recorded arena capacities exactly (291,598 at 320 B entries; 550,506 at
160 B).

## 1. Why capacity alone cannot work at E=160 (tradeoff curve)

| T | k | tables (B) | arena A | headroom vs 448,536 peak | verdict |
|---|---|---|---|---|---|
| 262,144 | 1 | 83,886,080 | 550,506 | +22.7% | table exhaustion (measured 174/200) |
| 524,288 | 1 | 167,772,160 | 291,598 | -35.0% | arena cliff replaces table cliff (retrodicts the 7.2D experiment) |
| 1,048,576 | 1 | 335,544,320 | - | - | infeasible |
| 524,288 | 0 | 83,886,080 | 550,506 | +22.7% | fundable but alpha=0.857 keeps long chains |
| 1,048,576 | 0 | 167,772,160 | 291,598 | -35.0% | arena cliff again |

Proof by exhaustion of the curve: no fundable (T, A) pair at E=160 with
k=1 holds the fixed-work peak. Partial entry shrinks (boundary_bits
32->8 B, state packing) reach best E~121 and still fall short; Node
compaction to 256 B/node is still short and is the most invasive
surface. Rehash-scratch analysis: measured staging need is ~10^2
entries per reroot (carryover 133), but the static worst case equals the
table; eliminating or half-sizing the scratch is the fallback, not the
primary.

## 2. Primary recommendation

**Fingerprint-gated entries with exact key verification, T = 2^20,
full scratch retained, epoch mechanics untouched.**

- `TranspositionEntry { std::uint64_t fp; NodeId node; std::uint32_t
  epoch; }` = 16 bytes (align 8; new static_assert; TranspositionKey and
  its asserts unchanged).
- `transposition_entries = 1048576` (power of two for the probe mask).
- `transposition_hash` and `build_key` byte-for-byte unchanged.
- `transposition_probe`: slot = h & (T-1); per step the epoch test runs
  first (unchanged order); on a current-epoch entry compare
  `entry.fp == h`; on match only, rebuild the key via
  `key_from_node(arena[entry.node], queue_)` and compare with
  `operator==`; merge on equality, else continue the linear probe.
  Insert stamps fp/node/epoch. Counting points, both depth-path returns,
  fail-stop flags, and the reroot stage/advance/reinsert shape are
  unchanged — plus a deterministic first-staged-wins skip for the
  never-triggered reroot duplicate-collision quirk (documented micro-pin,
  forced-collision unit test required).
- Arithmetic: tables 2 x 1,048,576 x 16 = 33,554,432 B; arena =
  228,695,776 / 324 = 705,851 nodes; retained 268,435,404 (52 B slack,
  guarded by the init-time budget check). Load at fixed-work peak:
  448,536 / 1,048,576 = 42.8 percent; timed peak 2.1 percent. Arena
  headroom +57 percent over peak. Fallback if the slack bites: T = 2^19
  (retained 268,435,232, alpha 0.857, arena 757,632).

## 3. Semantics preservation

1. Slot mapping: same hash over same bytes; mask widened. Merge-set
   independence from layout follows from the no-deletion/no-overwrite
   pins: equal keys always encounter each other before any empty slot,
   regardless of capacity, mask, or associativity. Deterministic
   candidate order: insertion sequence and probe algorithm unchanged.
2. Merge correctness: soundness — merge requires fp equality AND exact
   `operator==` on the rebuilt key (no probabilistic merge exists);
   completeness — equal keys produce equal fingerprints, encounter is
   guaranteed, verification passes. False fp matches cost one rebuild,
   never a wrong merge.
3. Key reconstruction: `key_from_node` re-derives all eight key fields
   from `(arena[node], queue_)` — depth (node.depth == parent.depth+1),
   cursor, root_child (both build_key branches), occupancy, policy state
   with identical normalization, boundary count/bits, active/hold —
   field-by-field equal to `build_key(child)`. The reroot rewrite of
   stored key fields is itself a re-derivation from arena nodes, so
   reconstruction is consistent across reroots. A
   reconstruction-differential test (rebuild == build_key over the
   corpus and edge boards, NaN-guard, signed zeros) is required.
4. Epoch/reset/reroot: untouched except entry width and the quirk-skip.
5. Observability: sizeof asserts, retained_bytes (recomputed, in
   budget), and truncation-relieved counters change; timed-leg counts
   predicted approximately invariant — the canary.

## 4. Work-volume honesty

The 6.8x -> 5.1x gap contained zero truncation effect (bit-identical
work vectors across A/B/D): it was pure per-unit cost removal. Removing
the fail-stop raises completed work per move (SPECULATION +15-25
percent on uniques/transitions/searches/parents; widening_iters climbs
toward budget; texhaust_moves -> 0) while per-unit materialize cost is
predicted 4-6x cheaper (SPECULATION: ~50 s of materialize today ->
~9-15 s); total time likely falls even as work rises, but neither
direction is pre-claimed. Plan 17.3 items 1-2 compare at equal
iteration budget; a candidate completing more of the budgeted work is
more faithful to the protocol. Gate-6 timed throughput reads as
policy-throughput parity evidence (volume asymmetry is by design per
the partition volume table); cost lives in gate-4 per-unit rates.
Presentation: joint (delta-work, delta-time) pairs everywhere.

## 5. Measurement plan (admissibility criteria)

Count-identity on fixed work is EXPECTED TO CHANGE (that is the goal:
texhaust_moves 174 -> 0); the old exact-zero criterion is superseded for
fixed-work legs by:

- Leg 1 (volume honesty): fixed-work count twins — texhaust_moves == 0
  (both flags), replay_failures == 0, work counters move up as a band
  (reported, not gated), comparator twin bit-identical, post-change
  determinism twin bit-identical.
- Leg 2 (mechanism canary): timed partition re-run, seeds 1/2/3 — class
  shares within +/-0.2 pp of the current 100-percent-(a) tables, all
  defect classes exactly zero, exact flags 1. Any timed share shift is
  a semantics defect and blocks.
- Leg 3 (fixed-work partition): full table with d = 0; positive volume
  residual reconciled per-pass against pending-occupancy records;
  pre-exhaustion class shares match timed shares within predeclared
  tolerance.
- Leg 4 (attribution): probe-steps/materialize histogram plus
  fp-rebuild counter (new SearchStats fields, test accessor +
  microbench), materialize_ns/node, paired off-mode totals (five ABBA+A
  pairs, seeds 1-3) as joint (delta-work, delta-time); gates 8/9 corpus
  re-run (untouched paths, must stay PASS); retained <= budget asserted
  from rows; arena-exhaustion flag zero everywhere.
- Step 0 (before the change): land the probe-steps counter and histogram
  on the current binary so before/after separates chain-length collapse
  from per-step cheapening.

## 6. Risks and gaps (labeled)

- All per-unit and wall-time predictions are SPECULATION measured by
  section 5.3; none is a claim.
- 52 B budget slack is thin but mechanically guarded (init-time used >
  budget check fails loudly); the T = 2^19 fallback is pre-computed.
- No per-move arena-occupancy record exists; add an `arena_used_end`
  gauge to the trace during Leg 1 to close this permanently.
- EvalCache thrash (94.6 percent miss) and candidate canonicalization
  (~18 percent) are untouched and remain the next targets; plan 13.5's
  cache-layout benchmark is still owed independently.
