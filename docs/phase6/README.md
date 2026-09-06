# Phase 6 evidence

Value-based TOJ engine (`src/tetris_engine.h`,
`src/tetris_engine.cpp`) with `tetris_engine::Engine` composing the
new rule adapter, fast-reachability enumeration, and
`toj_policy::Policy`. The legacy template engine is unchanged and
production targets still use it.

## Slice 6.2 scope

Deterministic frontier search over the slice 6.1 primitive:

- Outer anytime-widening passes matching the legacy `run()` contract
  (width starting at 2, one pass per budget unit). Per-pass quotas are
  `max(1, trunc(width_cache(level) * width * div_ratio))` with
  `width_cache(level) = pow(level + 2, ratio)` over the runtime
  `Parameters::ratio` and `div_ratio = 2 / max(width_cache)`, the
  maximum taken explicitly. Expanded counts below quota promote
  pending nodes best-first; at or above quota, at most one promotion
  happens and only on a strict `expanded_top.value < pending_top.value`
  comparison. Expanded sets persist across passes; un-promoted nodes
  stay pending.
- Frontier indexes follow legacy: index `i` holds depth
  `max_length + 1 - i`, index 0 is deepest and never expanded, and a
  pass is incomplete when it encounters a nonempty pending heap at
  indexes 1..max_length before processing it, even if that pass drains
  it. The hold horizon extends `max_length` by one exactly when the
  hold slot is occupied and the legacy raw next length
  (`piece_count - 1 + marker_count`) exceeds one or the hold is
  unlocked. `Queue::marker_count` preserves the raw `?` count that
  boundary bits alone cannot reconstruct; the engine consumes only
  the predicate that the raw length exceeds one, computed without
  overflow for any supplied marker count, and directly built queues
  are validated for nonempty pieces, matching boundary length, and
  the queue cap.
- Ranking is legacy `Status::operator<`: cumulative `State::value`
  only, with an explicit lower-`NodeId` tie-breaker where legacy order
  was incidental. Frontier pending sets are intrusive two-pass pairing
  heaps (links inside `Node`); expanded sets are insert-only trackers
  (count plus best node), which covers every legacy use.
- Same-depth transposition with exact bounded deduplication: a
  direct-mapped linear-probe table (8,192 entries) with full-key
  verification and no overwrite of distinct states. The key is depth,
  occupancy, field-wise policy state (doubles bit-exact with `-0.0`
  normalized to `+0.0` in both equality and hashing), active piece,
  resulting hold and availability, queue cursor, remaining
  virtual-boundary bits, and the root-child `NodeId` as first-move
  identity. Same-depth, same-root-identity equivalents materialize
  once; cross-root equivalents deliberately stay separate, so no
  attribution indirection exists and projection walks the parent
  chain. Table exhaustion stops the run incomplete with best-so-far
  selection intact. Promotion is failure-safe: the popped parent
  joins the expanded trackers before any child materializes, the
  transposition probe precedes allocation for depth-two and deeper
  children, child links finalize even when exhaustion breaks the
  child loop, and every promotion path checks exhaustion immediately.
- Projection follows legacy `get_best()`: deepest frontier with any
  content wins, pending top versus expanded best compared by strict
  value with expanded winning ties, then the parent chain walks back
  to the root child. The result separates the selected root candidate
  and its immediate post-move state from the deeper evidence node.
- Telemetry counts completed widening passes, expanded parents,
  enumeration calls, raw kernel landings at the landing-mask boundary,
  unique semantic candidates, rule applications, board-evaluation
  requests split into local memo hits and actual evaluations, policy
  transitions, materialized nodes, transposition merges, promotions
  refused by quota, pending occupancy, and transposition exhaustion.
- Expansion scratch is engine-owned: candidate, child, memo, and
  transposition buffers are reserved once in `init` and never grow.
  The candidate bound is the full search domain (4 orientations x 10
  columns x 48 rows x 2 arrival classes per source, 2 sources per
  parent); every insertion enforces capacity and overflow stops the
  search rather than dropping candidates. The kernel workspace is
  fixed-array. `default_arena_capacity` is derived from the 256 MiB
  budget minus these measured fixed structures, and `init` validates
  the full sum after reservation. No allocation occurs during the
  search loop; `set_root` adopts caller-provided queue storage.

Deferred to later slices: timed budgets, root reuse, the persistent
evaluation cache, final-path materialization, and production cutover.

## Slice 6.1 scope

One-parent expansion primitive with queue and hold representation:

- Bounded `Node` arena with integer `NodeId`, value-owned boards
  and policy states, explicit parent and child-range references.
- Engine-owned queue (pieces plus virtual-boundary bits) with
  per-node cursors; `parse_queue` rejects leading markers, unknown
  pieces, and empty input.
- Eleven-step-equivalent expansion per parent: branch resolution for
  the current piece and, when unlocked, the hold piece with
  empty-hold consumption; spawn gating; candidate enumeration; rule
  application with lockout kept as unexpandable dead results;
  within-source dedup by occupancy, spin, clear, and lockout keeping
  the first representative; evaluation once per unique board within
  the expansion; one policy transition per surviving branch.
- Explicit ordering contract: current-branch children in enumeration
  order, then hold-branch children, first wins dedup.
- Hold availability resets after placement and lock; only the root
  lock from the caller gates holding. `used_hold` stays move
  history and never becomes the child lock.
- Policy context carries the parent remainder after the current
  piece for every branch, preserving the legacy scoring input
  including pieces played by empty-hold branches. Child cursors
  still track consumption separately and never exceed the queue
  length.
- Instrumentation counts enumeration, evaluation, and transition
  calls. The engine never references the pathfinder.

Deferred from slice 6.1, now delivered in 6.2: widening, beam
pruning (quota-based deferral only, no permanent pruning), and
best-root projection. Still deferred to later slices: timed budgets,
root reuse, final-path materialization, persistent evaluation cache
tuning, and production cutover.

## Memory derivation

Slice 6.2 replaced the 1 MiB reserve with explicit accounting over
measured structure sizes. The full 256 MiB budget is split once in
`init` between the node arena and the fixed search workspace: the
candidate buffer (full search-domain bound per source), the child
buffer and evaluation memo (the same bound for both sources), the
8,192-entry transposition table, the 257-frontier metadata arrays
(pending-heap roots and counts plus the expanded trackers and width
cache), the queue reservation, and a conservative stack peak
allowance covering the measured kernel and expansion frames (the
largest kernel workspace instantiates to 320 bytes and the frames
nest only a few deep). `engine_buffer_reservation` computes the
identical inventory for the compile-time constant
`engine_fixed_workspace` and for the post-reservation check over
actual capacities, so both sides cover the same structures.
`default_arena_capacity` is the remainder divided by measured
`sizeof(Node)` (320 bytes; the pinned values are a 6,644,000-byte
fixed workspace and an 818,098-node arena with the heap links, root
identity, and played-piece fields added in slice 6.2). Every buffer is a single reservation requested before any
storage is committed: `init` rejects capacities beyond the `NodeId`
range or the byte allowance without allocating, re-checks the actual
arena reservation, and rejects the whole configuration if the
post-reservation total across all buffers exceeds the budget.
`retained_bytes()` reports the same inventory after the fact. Adopted
queue storage is normalized: `set_root` copies at most
`max_queue_length` pieces and boundary bits into the engine-owned
reservations, so a caller's oversized vector capacity is never
retained. On the
tested implementations the retained allocation peak equals the
allowance because storage never grows or relocates; a standard
library that over-reserves would transiently allocate more than the
request, and the post-reservation rejection bounds retained storage
only. Node addresses stay stable from materialization onward.
Rejected configurations behave as zero-capacity engines, and
reinitialization resets every run-state field, frontier sentinel, and
transposition entry on success and on rejection. Queue input
is capped at 256 pieces, the frontier pool is bounded by one heap
entry per materialized node (intrusive links live inside `Node` and
are part of its measured size), and materialization additionally
refuses to exceed the `NodeId` range. Tests assert retained bytes
within the budget including a non-power-of-two capacity, bounded
queue adoption from oversized reservations, address stability across
fills, and no growth beyond the reservation. Node pointers are
invalidated only by root replacement; callers still re-fetch by
id after any mutation. Small capacities prove fail-closed
materialization with exhaustion reporting and correct best-so-far
attribution. The
persistent cache share of the total budget arrives with the cache
slice; the frontier, transposition, and scratch shares are part of
the validated accounting above.

## Gate status

`tests/tetris_engine_tests.cpp` (CTest `tetris_engine_tests`, 602
checks, 0 failures in all five builds) covers the slice 6.1 gates
(queue parsing against the legacy `queue.csv` shapes, cursor and
hold-swap arithmetic, lock and exhaustion edges, per-child state
wiring against direct calls with explicit contexts, the played-piece
context sequence, cursor-overflow termination, input rejection, stats
reset, dedup, arena limits, linkage, exact reservation bytes, and
address stability, repeated-run determinism, lockout dead results
with a per-child lowest-row rule, spawn death, and budget derivation)
plus the slice 6.2 gates: marker-count preservation, the four
legacy horizon pin cases with an occupied locked hold, direct-queue
validation, field-wise transposition-key negatives including signed
zero, zero-budget and one-pass behavior, width progression across
passes with hand-checked quota counts, deferred work becoming
eligible later, early completion without counting nonexistent work,
at-quota refusal of equal scores under a stalled quota, same-root
convergence materializing once, cross-root equivalents staying
separate, the depth-one attribution invariant over the whole arena,
projection evidence against an independent deepest-frontier oracle,
immediate root state separated from deeper evidence, arena and
transposition exhaustion with best-so-far selection, and bit-for-bit
determinism across repeated searches. The repair gates add direct
pairing-heap tests (equal scores, long sibling chains, repeated
extraction), the complete memory inventory with retained bytes and
bounded queue adoption from oversized reservations, overflow-free
marker boundary values, reinitialization state resets on success and
rejection, exhaustion projection that preserves the best evidence and
attribution under the tie-break, adapter canonical keys verified
strictly increasing so the representative rule is deterministic, and
the four remaining policy-key negative cases. Mutation probes confirm
the gates: zeroed transitions, child-cursor policy contexts, and
danger-mask shifts all fail.

## Kernel corner note

A test board carrying 20 pre-existing full rows produced different
line-clear collapse results under GCC and Clang release builds of
the frozen kernel (survivor roof 14 versus 0), which changed dedup
counts. `set_root` now rejects any board containing a full row with
a safe row scan, and the 20-full-row board is a rejection
regression, so the invariant is enforced rather than assumed. Real
boards never carry full lines: engine inputs come from cleared
states, gameplay clears at most 4 lines at once, and a placement
adds at most 4 cells to clear-free rows, keeping every clear inside
the validated bound. No submodule change was made. The distinction
stands: `Board::cleared()` accepts completed rows under a bounded
simultaneous-clear precondition, while the engine requires
clear-free roots.
