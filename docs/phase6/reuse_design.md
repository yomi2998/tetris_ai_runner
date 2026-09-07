# Slice 6.5 design note: exact root reuse across turns

Legacy behavior established from `src/tetris_core.h`: `update_root`
matches the incoming map against the current root and then each
child by map equality. A matched child is rotated to the root
(unhooked, `parent = nullptr`), the per-depth transposition tables
rotate down with the deepest cleared, and the old root is
deallocated. With no match, the transposition tables are cleared and
a fresh root is allocated. `update_version` then resets `width` to
zero and clears both frontier heaps, so the tree nodes are retained
while the widening state restarts and re-materialization dedups
against the retained nodes.

## Contract for the value engine

- Reuse candidates are the previous root's depth-one children only
  (the legacy rotation scope). The incoming turn matches at most one.
- A match requires exact agreement on full board occupancy, all
  policy-state fields (approved field-wise semantics), hold piece
  and availability, and the complete remaining queue: the incoming
  piece count must equal the old count minus the played cursor
  (queue extension takes the clean-root path in this slice), every
  remaining piece and virtual-boundary bit must match the shifted
  old suffix (which fixes the active piece), and the incoming raw
  marker count must not exceed the old count (markers are consumed
  by advancing, never created).
- The effective horizon is checked as a consistency bound rather
  than a strict equality. The recomputed incoming horizon must not
  exceed the retained horizon, and it must cover the retained
  horizon minus the played cursor. Strict equality would wrongly
  reject the empty-hold advance: consuming two pieces shrinks the
  base horizon by two while the newly occupied hold legitimately
  adds one back. Both bounds hold for every legitimate turn
  (ordinary placement, hold swap, empty-hold consumption, marked
  queues) and reject extension, truncation, and horizon lies toward
  the safe clean-root path.
- On a match the matched child becomes the new root. The arena is
  compacted in place to the retained subtree: the child and every
  node reachable from it through retained parent links, dropping
  nodes whose new depth exceeds the new horizon. Compaction assigns
  new ids through the engine-owned idmap reservation; no additional
  workspace is allocated and the scratch capacity is unchanged by
  the rotation. Retention is verified, not assumed: beyond the
  identity, bound, and horizon filters, a kept node must have a
  smaller parent id with consistent depth linkage whose parent is
  itself retained. This drops duplicate second roots and orphans
  instead of retaining them, so the single-root assumption below
  cannot be violated by stale links. The target is the smallest
  retained old id (every retained node descends from it), so it
  compacts to id zero. Child links are rebuilt by an ascending scan
  into intrusive sibling chains; parent links, depths (shifted by
  one), and cursors (shifted by the played cursor) are remapped;
  the new root keeps the matched node's own board, policy state,
  and evaluation, never a reseeding of
  caller-owned state. Equal cursors across a link are legitimate
  (the hold branch clamps the cursor at the queue end) and are kept.
- Transposition entries are remapped, not cleared: entries with an
  out-of-range or dropped node, above the old depth-one level, with
  a cursor below the played cursor, or beyond the new horizon are
  dropped; the rest shift depth down by one, shift cursor
  identically, take the recomputed first-move identity of their
  remapped node, and are rehashed into fresh slots. Fresh slots are
  required because shifted keys probe different chains than the
  stale ones. Boundary count, boundary bits, active piece, and hold
  metadata need no rewrite: under the exact-queue contract the new
  queue equals the old suffix, so every retained key already
  describes the new queue. Re-expansion of retained positions
  therefore merges with retained nodes instead of duplicating them.
- Children are enumerated as intrusive sibling chains, not as one
  contiguous range. Each node carries a sibling link inside the
  existing `Node` padding (`sizeof(Node)` stays 320 bytes); the
  parent keeps the head link and the total owned count. Re-expansion
  appends fresh children and owned merges (a merge never reparents
  a node or links it under a second parent) with deduplication, so
  retained children survive the resumed search instead of being
  overwritten by the fresh run. The public range-linking entry
  rejects before writing when the count exceeds the remaining arena
  tail, so wrapping or oversized ranges preserve existing linkage.
  Linking cost observation: each append walks the sibling chain,
  so one append is linear in the current sibling count and
  constructing or rescanning an entire sibling group can be
  quadratic. The debug engine suite runs 16.4s at 15939 checks including
  bidirectional membership verification over every parent of every
  reuse tree, versus 21.0s at 879 checks before the reuse gates;
  that suite-time comparison is an observation, not an isolated
  measurement of linking cost. Production throughput qualification
  remains deferred. The rerooted root keeps the matched child's
  move history (played piece and source) while a fresh root carries
  defaults, so warm-versus-cold node parity excludes the root's
  move-history fields and compares everything else exactly.
- The widening state restarts per legacy `update_version`: width
  returns to zero, both frontier heaps clear, expansion trackers
  reset, and every retained non-root node becomes unregistered so the
  next re-expansion re-registers it through transposition merging.
  Retained evaluations avoid recomputation through the persistent
  cache and merged materialization; enumeration for re-expanded
  levels is redone, matching legacy.
- The evaluation cache keeps its entries across root changes while
  its counters reset, preserving the documented per-search telemetry
  scope. Policy or configuration reinitialization clears reusable
  tree state through the existing `init` reset path, which releases
  every buffer reservation including the idmap scratch.
- Rejected `set_root` inputs leave the previous tree untouched so a
  later valid call can still reuse it.

## Table size

The transposition table holds 262,144 entries with an equal rehash
scratch, measured as follows on the slice gate matrix in the debug
configuration: 8,192 entries exhaust on the first three-piece
production-policy search; 16,384 entries exhaust on the unlocked
empty-hold variant; 32,768 entries complete every gate including
warm-versus-cold parity and successive turns. The size stays a
power of two for the direct-mapped probe mask and remains covered
by the fixed workspace budget.

### Sizing addendum (slice 7.2D): production demand exceeds the gate sizing

The gate-matrix calibration above never approached production workload
volume and is insufficient as a capacity basis: at the Section 17.2
fixed-work reference (warmup 20, moves 200, iterations 1000, maxdepth 6),
the 32,768-entry table exhausts on every move, fail-stopping each search
at exactly 32,768 materialized nodes with about 32,000 states still
pending. A 1,048,576-entry experimental build under a raised budget
measured the true per-move distinct-state demand with zero exhaustion on
all 660 fixed-work moves: peak 448,536 (seed 1), 429,964 (seed 2),
408,023 (seed 3); medians cluster near 320,000 with minima above 221,000.
Timed mode (20 ms) peaks at 22,084 with no exhaustion anywhere.

262,144 entries is the largest power of two fundable within the 256 MiB
budget (2 by 262,144 by 320 bytes of table plus scratch against a
173,957,408-byte fixed workspace, leaving a 291,598-node arena); 524,288
entries alone would exceed the whole budget at 320 bytes per entry. The
new size therefore carries 12x headroom over the timed (production)
demand peak while fixed-work reference volumes above it still fail-stop
gracefully with best-so-far intact: a measured residual, not a silent
change. Key compaction or a replacement policy would be required to hold
the full fixed-work peak inside the budget; both are deferred, and the
re-campaign will show how far the 8x relief carries gates 3, 4, and 6.
