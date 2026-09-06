# Phase 6 evidence

Value-based TOJ engine (`src/tetris_engine.h`,
`src/tetris_engine.cpp`) with `tetris_engine::Engine` composing the
new rule adapter, fast-reachability enumeration, and
`toj_policy::Policy`. The legacy template engine is unchanged and
production targets still use it.

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

Deferred to later slices: widening, beam pruning, timed budgets,
best-root projection, root reuse, final-path materialization,
persistent evaluation cache tuning, and production cutover.

## Memory derivation

`default_arena_capacity` equals the 256 MiB production budget minus
a 1 MiB workspace reserve, divided by measured `sizeof(Node)` (256
bytes, about one million nodes). Queue input is capped at 256
pieces, per-expansion scratch is bounded by the enumerated candidate
count, and materialization additionally refuses to exceed the
`NodeId` range, so the arena bound is enforced rather than
assumed. Node pointers are invalidated by root replacement and
materialization growth; callers re-fetch by id. Tests use explicit
small capacities to prove fail-closed materialization with
exhaustion reporting. The persistent cache, frontiers, and workspace
shares of the total budget arrive with the full search loop; until
then this README distinguishes the verified arena bound from the
deferred total-engine accounting.

## Gate status

`tests/tetris_engine_tests.cpp` (CTest `tetris_engine_tests`, 384
checks, 0 failures in all five builds) covers queue parsing against
the legacy `queue.csv` shapes, cursor and hold-swap arithmetic,
lock and exhaustion edges, per-child state wiring against direct
calls with explicit contexts, the played-piece context sequence,
cursor-overflow termination, input rejection, stats reset, dedup,
arena limits and linkage, repeated-run determinism, lockout dead
results with a per-child lowest-row rule, spawn death, and budget
derivation. Mutation probes confirm the gates: zeroed transitions,
child-cursor policy contexts, and danger-mask shifts all fail.

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
