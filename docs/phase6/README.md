# Phase 6 evidence

Value-based TOJ engine (`src/tetris_engine.h`,
`src/tetris_engine.cpp`) with `tetris_engine::Engine` composing the
new rule adapter, fast-reachability enumeration, and
`toj_policy::Policy`. The legacy template engine is unchanged and
production targets still use it.

## Slice 6.4 scope

Bounded board-evaluation cache with explicit identity and lifecycle:

- The cache stores `Evaluation` by value keyed on logical occupancy;
  policy transitions stay per surviving branch and policy state is
  not part of the key. The parent-local per-expansion memo stays in
  front of the cache in every configuration, including disabled.
- `EvalCache` hashes the eight logical occupancy words with the same
  FNV primitive family as the transposition key (no raw bytes, no
  padding), and every hit verifies the full packed occupancy before
  returning; a hash match alone never returns an evaluation. Entries
  are value-only: no pointers into replaceable slots are exposed or
  stored.
- Storage is a fixed vector reserved during `init` and covered by
  the memory budget; nothing allocates or grows in the search loop.
  Replacement fills empty ways first, then replaces the lowest stamp
  in circular order (ties to the lowest index), which is deterministic
  under the deterministic access order and stays correct across
  64-bit stamp wrap, covered by a seeded near-wrap gate. Cache
  clearing rides on `init`/policy reinitialization, cache counters
  are per-search and reset with the run state while entries survive
  `set_root` as groundwork for root reuse without implementing it,
  and enabled-cache geometry (nonzero entries, positive ways,
  divisibility, power-of-two set count, and a byte product within
  the budget) is validated in `init` before allocation with
  rejected configurations fail-closed: the non-cache fixed
  reservation is established first, cache bytes must fit the
  remaining budget with checked arithmetic, and an over-budget
  cache is rejected regardless of the arena capacity, including
  zero-capacity configurations.
- Telemetry distinguishes board-evaluation requests, local memo
  hits, cache requests, hits, misses, replacements, and actual
  evaluations; with the cache enabled every miss computes exactly
  one evaluation and requests split into memo hits and cache
  lookups.
- Layout selection is measured, not assumed: direct-mapped forces
  one way per set and set-associative uses the configured count, so
  the benchmark compares genuinely different structures. At an equal
  2,097,152 byte budget (16,384 entries), all variants completed
  identical work (4,790 passes) and identical per-board result
  fingerprints. The measured medians are close and noisy across
  runs: direct-mapped 90.868 ms, set-associative-4 91.348 ms,
  disabled 93.441 ms, with the two cached variants trading places
  between repetitions. Direct-mapped is the production default for
  its nominal timing lead and simpler structure; no robust speed
  advantage is claimed. Hit rate on the direct-mapped layout was
  44,610 of 127,663 lookups (34.9 percent), reducing evaluations
  127,663 to 83,053. Full data in `cache_benchmark.txt`.

## Slice 6.5 scope

Exact root reuse across turns:

- Reuse candidates are the previous root's depth-one children. The
  incoming position matches by full occupancy, the complete policy
  state, hold piece and availability, and the complete remaining
  queue: exact remaining length (extension takes the clean-root
  path), shifted piece and boundary metadata (fixing the active
  piece), a raw marker count that never grows across the turn, and
  a recomputed horizon inside the consistency bound documented in
  `reuse_design.md`. A mismatch falls back to the clean root
  initialization, and rejected inputs leave the live tree untouched.
- On a match the matched child's subtree is retained: the arena is
  compacted in place through the engine-owned idmap reservation
  (no new scratch is allocated by the rotation), depths and cursors
  shift by the played cursor, and first-move identity is recomputed
  from each retained depth-one move's fingerprint, which the rotation
  preserves. Retention is verified link by link (smaller parent id,
  consistent depth linkage, retained parent), so duplicate roots and
  orphans are dropped rather than kept. Children enumerate as
  intrusive sibling chains (one link inside existing `Node` padding,
  size pinned at 320 bytes): re-expansion appends fresh children and
  owned merges with deduplication instead of overwriting the retained
  links, and the rerooted root keeps the matched child's move history
  while a fresh root carries defaults. Transposition entries are
  remapped (depth, cursor, node, recomputed identity) and rehashed
  into fresh slots; entries outside the retained subtree or the new
  horizon are dropped. The evaluation cache keeps its entries while
  its counters reset to the per-search scope.
- Gates: positive reuse with per-search telemetry reset asserted
  before the warm run and avoided evaluation plus avoided
  materialization work asserted separately after it, a positive
  control plus eighteen single-component identity negatives (ten
  policy fields, row-40 occupancy, active piece, boundary
  bit, hold piece and availability, shorter and extended remaining
  sequences, invented marker), each on an independently populated
  tree, queue advancement through ordinary placement, occupied-hold
  swap, empty-hold consumption with a nonempty remainder, queue
  exhaustion to a clean root, and marked queues in both directions,
  the attribution invariant, full warm-versus-cold node parity
  (complete evaluation and policy state plus depth, parent, played
  piece, source, and hold wiring, excluding the rerooted root's kept
  move history), child-chain membership proven bidirectionally for
  every parent after warm, marked, hold-swap, multi-turn, exhausted,
  and cold searches, a capacity-37 exhausted-reroot-resume reproducer
  with retained-first-child enumeration, a mid-size partial-exhaustion
  promotion gate, lifecycle coverage (successive turns with cold parity
  at each turn, move followed by a matching reroot with scratch-transfer
  and no-allocation checks, rejection preserving the live tree for a
  later reuse, shrinking reinitialization releasing idmap scratch,
  cycled-versus-fresh retained-byte equality), and bounded retained
  bytes under separately counted arena and idmap capacities.

## Slice 6.3 scope

Timed and iteration budgets on the shared search machinery:

- `run(SearchBudget)` ports the legacy budget loop exactly: the
  deadline is the clock reading at entry plus the millisecond budget,
  a do-while always runs at least one pass, time is checked between
  passes (a pass that overruns the deadline still completes), a
  completion break stops the loop without counting further work, and
  the iteration kind runs `max(1, n)` passes. Exhaustion and
  invariant stops break the loop with best-so-far selection intact.
- The clock is injectable through `EngineConfig::clock_nanos`
  (nanoseconds on a monotonic scale), defaulting to `steady_clock`,
  so deadline semantics are tested deterministically with a
  controllable fake clock and saturating budgets are overflow-safe.
  The engine move constructor moves the configuration, so an
  injectable clock with a throwing copy constructor is never copied
  by the move, and a regression gate arms such a clock before the
  move. The production throughput gate for timed mode
  remains the measured performance gate in the plan, not a unit
  assertion.
- The deterministic `run(max_passes)` entry keeps its exact-pass
  semantics (zero passes allowed); the legacy-faithful budget entry
  forces at least one pass for either kind.

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
  direct-mapped linear-probe table (8,192 entries, resized to 32,768
  in slice 6.5 per the measurement in `reuse_design.md`) with
  full-key verification and
  no overwrite of distinct states. The key is depth,
  occupancy, field-wise policy state (doubles bit-exact with `-0.0`
  normalized to `+0.0` in both equality and hashing), active piece,
  resulting hold and availability, queue cursor, remaining
  virtual-boundary bits, and the root-child identity as first-move
  identity (a `NodeId` rank in slice 6.2, the rotation-stable move
  fingerprint since slice 6.5). Same-depth, same-root-identity equivalents materialize
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
  search loop; `set_root` normalizes adopted queue storage into the
  bounded engine-owned reservations.

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
best-root projection. Deferred from 6.2, now delivered in 6.3: timed
budgets. Deferred from 6.3, now delivered in 6.4: the bounded
board-evaluation cache. Still deferred to later slices: root reuse,
final-path materialization, and production cutover.

## Memory derivation

Slice 6.2 replaced the 1 MiB reserve with explicit accounting over
measured structure sizes. The full 256 MiB budget is split once in
`init` between the node arena and the fixed search workspace: the
candidate buffer (full search-domain bound per source), the child
buffer and evaluation memo (the same bound for both sources), the
32,768-entry transposition table plus the equal rehash scratch, the
257-frontier metadata arrays
(pending-heap roots and counts plus the expanded trackers and width
cache), the queue reservation, and a conservative stack peak
allowance covering the measured kernel and expansion frames (the
largest kernel workspace instantiates to 320 bytes and the frames
nest only a few deep). `engine_buffer_reservation` computes the
same inventory for the compile-time constant `engine_fixed_workspace`
and for the post-reservation check over actual buffer capacities;
the buffer reservations are conservative documented bounds (the
queue at the 256-piece cap, the stack at a fixed peak allowance)
rather than runtime inspection of every control allocation.
`default_arena_capacity` is the remainder divided by measured
`sizeof(Node)` plus `sizeof(NodeId)` (320 and 4 bytes; the pinned
values are a 27,091,232-byte
fixed workspace including the 2,097,152-byte direct-mapped
evaluation cache and a 744,889-node arena with the heap links, root
identity, and played-piece fields added in slice 6.2). The arena
and the rotation idmap are counted separately by actual capacity,
never by substituting one capacity for the other. Every buffer is a single reservation requested before any
storage is committed: `init` releases every previous reservation
first (including idmap scratch and rehash storage), rejects
capacities beyond the `NodeId`
range or the byte allowance without allocating, re-checks the actual
arena and idmap reservations, and rejects the whole configuration
if the
post-reservation total across all buffers exceeds the budget.
Rejected configurations release scratch so they behave as
zero-capacity engines.
`retained_bytes()` reports the same inventory after the fact. Adopted
queue storage is normalized: `set_root` copies at most
`max_queue_length` pieces and boundary bits into the engine-owned
reservations, so a caller's oversized vector capacity is never
retained. The engine is move-constructible and non-copyable; a moved
engine's pending heap rebinds to the destination arena and the idmap
scratch transfers with it, and copy and
move assignment are deleted. On the
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
persistent evaluation cache share of the total budget is part of
the validated accounting above.

## Gate status

`tests/tetris_engine_tests.cpp` (CTest `tetris_engine_tests`, 10164
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
marker boundary values, reinitialization state resets including
exhaustion flags and expansion counters on success and rejection,
exhaustion projection that preserves the best evidence and
attribution under the tie-break in both a zeroed-policy capacity-25
scenario and the production-policy capacity-18 probe, a move-semantics
gate asserting the engine is move-constructible, non-copyable, and
non-assignable with a moved engine running a full search, adapter
canonical keys verified strictly increasing with each occupied-cell
set keeping exactly one rotation so the representative rule is
deterministic, and the four remaining policy-key negative cases.
The slice 6.3 gates add the controllable-clock budget probes: a
hand-checked three-pass deadline stop at four milliseconds per clock
call, the legacy single-pass behavior for an expired time budget and
a zero iteration budget, exact iteration-budget counts, completion
breaking the budget loop without counting further work, a frozen
clock matching the deterministic budget bit for bit, a real-clock
smoke run that returns with at least one pass without asserting
completion, arena exhaustion under a large time budget with intact
best-so-far selection, and a saturating budget without overflow.
A clock-move gate arms a callable whose copy throws after
initialization and proves the engine move transfers it without
copying, with the destination completing a timed search.
The slice 6.4 gates add direct EvalCache probes: a forced same-set
collision between distinct boards never returning a false hit,
exact verification, lowest-stamp replacement in a four-way set,
repeated hits, clear invalidating entries and counters; cache
parity gates proving disabled, direct-mapped, and set-associative
runs produce identical selections and arena sizes on the same
workload; counter accounting (requests split into memo hits and
cache lookups, misses equal to computed evaluations, disabled runs
reporting zero cache counters); byte-allowance rejection
participating the cache reservation; cache behavior across engine
movement and reinitialization; layout-distinguishing replacement
behavior with equal lookup streams; full materialized-tree parity
(boards, evaluations, policy states) across disabled and both cached
layouts; reinitialization with changed evaluation parameters matching
a fresh engine while warm-cache results differ; an
upper-storage-domain identity case; and a seeded near-wrap stamp
rollover gate. The slice 6.5 repair gates add a positive reuse
control plus eighteen single-component identity negatives over
independently populated trees (ten policy fields, row-40
occupancy, active piece, boundary bit, hold piece and availability,
shorter and extended remaining sequences, invented marker),
per-search telemetry reset asserted immediately after a matching
root change, avoided materialization asserted separately from
cache savings, full warm-versus-cold node parity over complete
evaluation and policy state with depth, parent, played piece,
source, and hold wiring (excluding the rerooted root's kept move
history), child-chain membership proven bidirectionally for every
parent after warm, marked, hold-swap, multi-turn, exhausted, and
cold searches, a capacity-37 exhausted-reroot-resume reproducer,
a mid-size partial-exhaustion promotion gate, advancement through
hold swap,
empty-hold consumption, exhaustion to a clean root, and marked
queues in both directions, successive turns with cold parity at
each turn, move followed by a matching reroot with scratch
transfer and no-allocation checks, rejection preserving the live
tree for a later reuse, reinitialization releasing idmap scratch
with cycled-versus-fresh retained-byte equality, and separately
counted arena and idmap capacities. Mutation probes confirm
the repair gates: prefix queue matching, a removed marker bound,
a missing cache reset, dropped idmap transfer or release, and
fresh-only child linking each
fail, alongside the earlier zeroed transitions, child-cursor policy
contexts, and danger-mask shift probes.

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
