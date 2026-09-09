# Direct-view transposition key trial design

## Goal

Replace the 152-byte `TranspositionKey` materialization on the transposition
hot path with direct hashing and direct equality, keeping every merge
decision, probe walk, insertion, count, and ordering bit-identical to the
normal engine. This is architecture verdict experiment 4: direct-view key
hashing and equality with a once-per-source key context. It is a
search-count-safe trial: candidate, order, and search vectors are preserved
and only the representation of exactness changes.

## Current cost being removed

Per staged child on the normal path, `search_materialize` calls
`build_key` (src/tetris_engine.cpp 1971), which constructs a full
152-byte `TranspositionKey` on the stack: depth, cursor, root fingerprint,
board occupancy copy, 40-byte policy state copy, boundary bit loop over the
queue suffix, active and hold piece codes. `transposition_hash` then mixes
about 26 words. On fingerprint match,
`transposition_probe_prehashed` (2049) calls `key_from_node` (2007), which
builds a second 152-byte key from the stored arena node, and compares with
`TranspositionKey::operator==`. The batch path (2280) builds arrays of
eight keys, about 1216 bytes of stack traffic per batch, before hashing.

The removable part is the two key constructions per candidate, the boundary
loop duplication, and the batch key array. Exactness itself is inherent and
stays.

## Mechanism

### Once-per-source context

All children staged by one `expand_source` call share parent, cursor, hold,
played piece, and source. The fields of `build_key` derived only from those
are computed once per call and carried in a small context object:

- child depth (parent depth plus one)
- fixed root child when the parent is not the root
- cursor
- boundary count and the four boundary bit words
- active piece code
- hold piece code and hold availability

Varying per child are only the board occupancy words, the policy state
doubles and scalars, and, for root parents, the per-child first move
fingerprint from played piece, candidate, and source.

### Direct fingerprint

A trial hash routine mixes the same logical fields in the same order with
the same FNV-1a word mixing as `transposition_hash`: depth, cursor,
boundary count, root child, occupancy logical words, state death, combo,
under attack, map rise, b2b, t2, t3, normalized acc, like, value doubles,
boundary words, active piece, hold piece, hold availability. It reads board
words from the staged child board and scalars from the staged child and
the source context. It never constructs a `TranspositionKey`. Because the
input word sequence is identical, the fingerprint is identical to today for
every child, so slot mapping, probe walks, and table fill behavior are
unchanged by construction.

### Direct equality

On fingerprint match the probe compares the stored arena node against the
staged view field by field, using the same predicates as
`TranspositionKey::operator==`: depth, cursor, boundary count, root child,
occupancy words, each state scalar with the same signed-zero
normalization, boundary words, active and hold piece codes, hold
availability. Node-side context values (parent depth, parent root child)
are read from the arena parent exactly as `key_from_node` does. No key
object is built on either side. Merge outcomes are identical because every
compared value equals the value the corresponding key field would hold.

### Context lifetime

Contexts live in a small engine scratch array sized for the at most three
sources one parent expansion stages (current, held piece, next piece
hold). The array is reset per `expand_parent`. At materialize time the
child selects its context by parent, cursor, hold piece, and hold lock,
which are exactly the inputs the context was built from, so selection is
unambiguous. Contexts are never stored in the arena, nodes, children, or
the transposition table. Retained memory is unchanged.

## Exact interfaces

New header `src/direct_key_trial.h`, included only under
`TETRIS_DIRECT_KEY_TRIAL`, with mutual exclusion errors against
`TETRIS_CHILD_SOA_TRIAL`, `TETRIS_EVAL_INDEX_TRIAL`, and
`TETRIS_EVAL_REUSE_TRACE`, following the pattern in
`src/child_soa_trial.h`:

- `struct DirectKeySourceContext` with depth, root child fixed flag, fixed
  root child value, cursor, boundary count, four boundary words, active
  piece code, hold piece code, hold availability flag
- `DirectKeySourceContext direct_key_begin_source(Node const &parent,
  HoldState hold, std::size_t cursor, Queue const &queue, Piece played,
  BranchSource source)` built once per `expand_source` call
- `std::uint64_t direct_key_hash(DirectKeySourceContext const &context,
  Board const &board, PolicyState const &state, Piece played,
  Candidate const &candidate, BranchSource source)` returning the identical
  fingerprint `transposition_hash` would return for the same child
- `bool direct_key_node_matches(Node const &node, Node const &parent_node,
  DirectKeySourceContext const &context, Board const &board,
  PolicyState const &state, Piece played, Candidate const &candidate,
  BranchSource source)` returning the identical verdict
  `key_from_node(node) == build_key(child)` would return
- `Engine::TranspositionProbe transposition_probe_direct(std::uint64_t fp,
  <staged child view>)` mirroring `transposition_probe_prehashed` with the
  same slot walk, same telemetry increments, same stop and exhaustion
  semantics, calling direct equality on fingerprint match
- `Engine::MaterializeOutcome search_materialize_direct(<staged child>,
  DirectKeySourceContext const &context)` and an inner variant mirroring
  `search_materialize_inner`, writing fp, node, and epoch identically and
  keeping registered and materialized node increments identical
- Batch loop variant computing eight fingerprints directly into a
  `std::array<std::uint64_t, 8>` with no key array

Cold paths keep materialized keys: the rehash loop at
src/tetris_engine.cpp 440 to 460, `transposition_reinsert`, and existing
test helpers such as `build_key_for_test` and `key_from_node_for_test`.
Public `Child`, `Node`, `expand`, `TranspositionKey`,
`transposition_hash`, and `TranspositionEntry` are unchanged. Merge
decisions, insertion order, NodeId assignment, sibling links, heap order,
and all work-vector counts are unchanged.

## Normal build isolation

`TETRIS_DIRECT_KEY_TRIAL` is defined only on dedicated targets. Normal
`tetris_profile_value` compiles zero trial code and must remain
byte-identical to SHA-256
`1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.
New CMake targets: `tetris_profile_direct_key` and
`direct_key_trial_tests`. A symbol isolation test asserts no direct key
symbols in the normal binary. A profile equivalence test compares every
non-timing field between normal and trial rows. Expected retained memory
is identical on both sides: 266338276.

## Identity argument

Fingerprint identity holds because the mixed word sequence is defined to
equal the sequence `transposition_hash` mixes for the same child: same
fields, same order, same zero extension, same signed-zero normalization
through `normalize_zero` before bit copy, same FNV offset basis and prime.
Equality identity holds because each compared pair equals the
corresponding key field pair: node depth equals rebuilt key depth, staged
cursor equals rebuilt key cursor, and so on through occupancy words,
state scalars, boundary words, piece codes, and hold availability. Probe
and insertion behavior then follow deterministically: same fingerprint
gives the same slot walk, same equality verdicts give the same merges,
same merges give the same NodeIds, and the same downstream sequence
follows.

## Testing

Differential tests, all under the trial macro, comparing trial routines
against the materialized key routines:

- fingerprint equality over randomized boards, states with positive and
  negative zero doubles, queue lengths and boundary patterns, root and
  deep parents, hold variants, and all branch sources
- equality verdict agreement over randomized child and node pairs
  including near misses on every field
- probe outcome agreement (merged node or empty slot) over scripted table
  states with collisions, epoch mixes, and exhaustion edges
- context selection tests proving each staged child of a multi-source
  parent resolves to the context built from its own source inputs
- macro exclusion compile tests proving the trial macro fails closed with
  each of the other three trial macros
- profile equivalence test between normal and trial binaries over the
  smoke workload with the mapped memory expectation of exact equality

The mandatory full gate before any timing is the 80-move telemetry on and
timers off count ABBA requiring exact non-timing identity on every field,
followed by the frozen telemetry off selector. No legacy evidence is used
unless the selector shows a clear current-candidate win.
