# Risk review: direct-view transposition key trial

Scope: design at `outputs/5569f8cc-bc9b-4fd0-816a-63f485983125/direct-key/design.md`
against `src/tetris_engine.cpp` 440 to 460 and 1960 to 2110 and verdict item 4.
Each risk carries a concrete mitigation with a test or gate that must pass.

## R1. Fingerprint divergence between direct hash and materialized hash

`transposition_hash` mixes about 26 words in declaration order with
zero-extended short fields, `normalize_zero` applied to three doubles
before bit copy, and FNV offset basis with prime 1099511628211. Any field
reorder, missed normalization, wrong zero extension, or different
occupancy word source changes slot mapping, probe walks, merges, NodeIds,
and every downstream count.

Concrete traps: `TranspositionOccupancy::logical_word(i)` order must match
the hash loop exactly; `boundary_bits` in a fresh key are value
initialized to zero and the direct view must zero all four words before
setting bits; `active_piece` and `hold_piece` use `no_piece_code` defaults
that must be reproduced; the batch path in `tetris_engine.h` 224 to 295
mixes the same fields per lane and the trial eight-at-once variant must
produce lane fingerprints equal to scalar fingerprints for the same child.

Mitigation: differential fingerprint tests over randomized boards, states
with positive and negative zero doubles, queue lengths and boundary
patterns, root and deep parents, all hold variants, and all branch
sources; plus a batch-versus-scalar lane equality test. The count ABBA
then proves slot mapping identity because any fingerprint difference
changes merge decisions and work vectors.

## R2. Equality divergence on fingerprint match

`transposition_probe_prehashed` rebuilds the stored side with
`key_from_node` and compares with `TranspositionKey::operator==`. The
direct comparator must reproduce every predicate, including
`normalize_zero` on both sides. The stored node policy may hold negative
zero because normalization in `key_from_node` applies to a copy, so
comparing raw stored doubles without normalizing the node side first is a
divergence. Occupancy must compare logical words exactly as the defaulted
`TranspositionOccupancy` equality does, not padded storage bytes.

Mitigation: verdict-agreement tests over randomized child and node pairs
with near misses injected on every field, signed-zero cases on both
sides, and probe-outcome agreement tests over scripted table states with
collisions, mixed epochs, and exhaustion edges. The count ABBA proves
merge identity end to end.

## R3. Context staleness across reset epoch, arena growth, and rehash

Contexts carry no epoch, so `advance_transposition_epoch` cannot stale
them, and they copy parent depth and root child by value, so arena growth
cannot stale them either. The remaining hazards are a context array sized
for three sources while some parent stages more, and any use of a context
built before a queue or hold mutation within the same expansion.

Mitigation: size the scratch for the maximum staged source count with a
fail-closed bound check; reset the array at each `expand_parent`;
contexts reference no arena memory; tests cover maximum-source parents
and epoch-boundary expansions. The rehash loop at 440 to 460 keeps
materialized keys, so no context participates in rehash.

## R4. Context selection ambiguity among staged sources

At materialize time the child resolves its context by parent, cursor,
hold piece, and hold lock. Two sources of one parent could stage children
with identical tuples but different played pieces or candidates. This is
harmless only because every context field derives from parent, cursor,
and hold alone, so identical tuples imply identical contexts. If any
context field ever depends on played piece, candidate, or source, that
property breaks.

Mitigation: a context selection test asserting that children of a
multi-source parent resolve to the tuple built from their own source
inputs, plus an assertion that contexts built from equal tuples are
field-equal. Keep per-child root fingerprints outside the context, as
designed, because root parents vary per child.

## R5. View aliasing and reference lifetime during probe

The probe walks `transposition_` while comparing staged child memory
against arena nodes. Staging buffers and the arena are distinct, so direct
aliasing does not occur, but `arena_.emplace_back` inside
`materialize_soa` can reallocate, invalidating any `Node const &` held
across the growth point. The design passes node references into
`direct_key_node_matches`, so call sites must re-fetch after any growth.

Mitigation: mirror the existing call order exactly, probe and equality
before materialization, no node reference held across emplace; review
must check every new call site for this ordering. Tests cannot catch a
latent reallocation bug reliably, so this is a review gate item plus
address sanitizer coverage in the trial suite.

## R6. Cold-path rehash and reinsert consistency

The shrink rehash path builds materialized keys with `key_from_node` and
reinserts with stored fingerprints, then trial probes read those entries
with direct equality. Consistency holds if and only if trial fingerprints
equal materialized fingerprints and trial equality equals key equality,
which R1 and R2 cover. The additional trap is
`transposition_reinsert` being called with a stored fingerprint from a
previous epoch mixed with a fresh key; the existing code already handles
this by probing with the given pair, and the trial must not alter that
contract.

Mitigation: keep materialized keys on all cold paths, share the entry
format unchanged, and add a shrink-then-probe agreement test comparing
trial probe outcomes against materialized probe outcomes after a forced
rehash. Test helpers `build_key_for_test` and `key_from_node_for_test`
stay materialized and serve as the differential oracle.

## R7. Probe telemetry and exhaustion counters

`transposition_probe_prehashed` increments `probe_steps`,
`probe_histogram`, and `probe_rebuilds` under telemetry at precise points,
and sets `search_stopped_` and `transposition_exhausted_` on full-table
walk. The trial mirror must increment the same counters at the same
decision points, including `probe_rebuilds` on fingerprint match before
equality is known, or telemetry-on rows diverge and the count gate fails
for instrumentation rather than semantics.

Mitigation: the count ABBA compares every non-timing field including all
probe counters, so any telemetry ordering difference surfaces as a gate
failure with a precise field name. No separate timing may proceed until
this passes.

## R8. Retained-memory accounting

Expected retained memory is exactly equal on both sides at 266338276
because the trial adds no arena, idmap, or table capacity and stores no
context in the arena, nodes, children, or table. The trap is engine-member
scratch being counted if the retained metric ever includes engine object
size, or a context array sized by a capacity constant that a later change
raises.

Mitigation: keep the scratch fixed at the staged-source maximum, keep the
default arena capacity unchanged, and assert exact retained equality in
the profile equivalence test and the count gate rather than a
less-or-equal bound.

## R9. Lesson from the exact-index trial: added per-candidate cost

The index trial avoided 41 percent of recomputations yet lost about 14
percent wall time because lookups, insertions, replacements, and exact
comparisons exceeded the saved arithmetic on every request. Direct-view
keys have no hit-rate break-even: they remove two 152-byte constructions
and one boundary loop unconditionally on every candidate. The residual
added cost is the per-candidate context tuple match and the direct field
walk on fingerprint match, which replaces a full second key build rather
than adding a new structure.

Mitigation: keep the added path strictly smaller than one key build, no
new arrays, no new passes, no capacity changes. If the selector still
fails, accept NO-ADVANCE rather than layering further mechanisms.

## R10. Lesson from the Child SoA trial: stack traffic that does not convert

SoA removed 24 bytes per staging slot and reduced retained memory by
184320 bytes yet lost about 10.8 percent wall time to indirection and
cache pressure. The direct-view trial differs in kind: it removes work
instead of rearranging it, reads only already-hot lines (staged child,
arena node, queue suffix), and adds no indirection. The honest residual
risk is that key construction is too small a share of the 26.20 percent
search materialize scope and the 10.26 percent `Engine::materialize` self
cycles to reach the needed 2.069 percent total reduction even when fully
removed.

Mitigation: set the expectation before collection that a correct but
sub-threshold result is a valid NO-ADVANCE, not a defect. Count identity
first, then the frozen four-pair selector decides. No timing inference
from count rows under any outcome.

## Verdict

No blocker found in the design. All ten risks have a named test or gate.
The trial is safe to implement under the trial macro with mutual exclusion
against the three other trial macros, normal binary byte identity
preserved, cold paths unchanged, and the mandatory count ABBA before any
timing.
