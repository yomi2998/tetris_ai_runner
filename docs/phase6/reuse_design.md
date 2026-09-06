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
  policy-state fields (approved field-wise semantics), the active
  piece, hold piece and availability, the complete remaining concrete
  queue sequence, the remaining virtual-boundary metadata shifted
  with the queue, and the effective horizon: the recomputed
  `max_length` for the incoming queue, hold, and marker count must
  equal the retained one. Boundary-bit equality alone is insufficient.
- On a match the matched child becomes the new root. The arena is
  compacted in place to the retained subtree (the child and every
  node whose first-move identity was that child), dropping nodes
  whose new depth exceeds the new horizon. Compaction assigns new
  ids through a scratch field inside `Node`; no additional workspace
  is allocated. Child links are rebuilt by an ascending scan; parent
  links, depths (shifted by one), and cursors (shifted by the played
  cursor) are remapped; the new root keeps the matched node's own
  board, policy state, and evaluation, never a reseeding of
  caller-owned state.
- Transposition entries are remapped, not cleared: depth shifts down
  by one, cursor shifts identically, first-move identity is
  recomputed from each retained depth-one move (the fingerprint of
  the node's own incoming move, which the rotation preserves), and
  entries outside the retained subtree or the new horizon are
  dropped. The remapped entries are rehashed into fresh slots.
  Re-expansion of retained positions therefore merges with retained
  nodes instead of duplicating them.
- The widening state restarts per legacy `update_version`: width
  returns to zero, both frontier heaps clear, expansion trackers
  reset, and every retained non-root node becomes unregistered so the
  next re-expansion re-registers it through transposition merging.
  Retained evaluations avoid recomputation through the persistent
  cache and merged materialization; enumeration for re-expanded
  levels is redone, matching legacy.
- The evaluation cache survives root changes; its telemetry keeps the
  documented per-search scope. Policy or configuration
  reinitialization clears reusable tree state through the existing
  `init` reset path.
- Rejected `set_root` inputs leave the previous tree untouched so a
  later valid call can still reuse it.
