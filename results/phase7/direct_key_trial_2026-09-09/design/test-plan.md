# Direct-view transposition key trial test plan

## Scope

This plan covers the trial-only direct-view transposition key mechanism
described in
`.../outputs/5569f8cc-bc9b-4fd0-816a-63f485983125/direct-key/design.md`.
The trial replaces the 152-byte `TranspositionKey` materialization on the
hot path with a once-per-source context, a direct fingerprint routine, and
a direct node-versus-staged-view equality routine. Merge decisions, probe
walks, insertion order, NodeId assignment, sibling links, heap order, and
all work-vector counts must remain bit-identical to the normal engine.
Cold paths keep materialized keys: the shrink rehash loop at
src/tetris_engine.cpp 440 to 460, `transposition_reinsert`, and the test
helpers `build_key_for_test` and `key_from_node_for_test`.

All tests below compile under `TETRIS_DIRECT_KEY_TRIAL` except the
isolation and macro exclusion tests, which compile the normal target with
and without forbidden macro combinations. The fast suites run in
milliseconds. The corpus differentials run in seconds. The 80-move count
ABBA is the mandatory gate before any timing.

## 1. Fingerprint identity versus transposition_hash

### 1.1 Differential corpus over staged children

Build a corpus of at least 20000 staged children drawn from live engine
runs on the frozen workload seeds plus synthetic edge children. For each
child, compute `direct_key_hash` with its source context and
`transposition_hash(build_key(child, no_node))`. Require exact equality
of every fingerprint. Any mismatch is a P0 failure and blocks the gate.

### 1.2 Signed-zero normalization equivalence

`transposition_hash` mixes `normalize_zero` of acc, like, and value
before bit copy, while `build_key` stores normalized values and the
mixer normalizes again. The differential corpus must contain PolicyState
doubles in all four sign combinations of positive zero and negative zero
for each of acc, like, and value, including NaN-free nonzero values
adjacent to zero. For every case require
`direct_key_hash == transposition_hash(build_key(child))` and require the
staged values used by the direct path to equal the normalized stored
key values. Cases: acc in {0.0, -0.0, 1.5, -1.5}, like in {0.0, -0.0},
value in {0.0, -0.0, 42.0}, crossed at least pairwise.

### 1.3 Queue boundary pattern coverage

`build_key` derives boundary_count and the four boundary bit words from
the queue suffix at the child cursor. Cover remaining piece counts at 0,
1, 63, 64, 65, 127, 128, and the maximum queue length; boundary patterns
all clear, all set, alternating, single bit at word edges 63 and 64, and
cursor at queue end where remaining is zero and active piece is
no_piece_code. Each pattern runs with at least two distinct parents.
Every case requires fingerprint equality with the materialized key.

### 1.4 Hold, cursor, depth, and source coverage

Hold piece present, hold piece absent, hold locked, hold available. Cursor
at 0, at mid queue, and at queue end. Parent depth zero with per-child
first move fingerprints from every BranchSource, and parent depth nonzero
with shared root child values. Children whose parent is the root and
whose parent is deep must both appear. Occupancy words cover empty
boards, full rows, sparse minos, and boards differing in exactly one bit
of each occupancy word position.

## 2. Direct equality versus key_from_node equality

### 2.1 Verdict agreement over randomized pairs

Generate at least 50000 child and node pairs, half exact matches and half
near misses. For each pair require
`direct_key_node_matches(...) == (key_from_node(node) == build_key(child))`.
Near misses must cover every compared field individually: depth, cursor,
boundary count, root child, each occupancy word, each state scalar
including acc, like, value with signed-zero pairs, each boundary word,
active piece, hold piece, hold availability. A single missed field per
pair, plus a dedicated run flipping two fields at once to catch
short-circuit order assumptions.

### 2.2 Adversarial equality cases

Boards that are equal except one occupancy bit at the first and last
logical word. States equal except death, combo, under attack, map rise,
b2b, t2, t3 each in isolation. Boundary bit arrays differing only in bit
64 and bit 0 of word 1. Root child fingerprints differing in one bit.
Hold locked versus available with otherwise identical children. Nodes
whose stored parent depth is zero versus nonzero to exercise both root
child derivation branches. All cases run through both the direct verdict
and the materialized equality and must agree.

### 2.3 Fingerprint collision behavior

Construct pairs with distinct keys whose fingerprints are forced equal by
testing through a reduced table capacity test hook or by direct probe
calls with an injected fp. Require the direct path to return the same
merge versus insert verdict as the materialized path for colliding
distinct keys and for colliding identical keys.

## 3. Probe slot mapping and walk identity

### 3.1 Slot mapping agreement

Over the section 1 corpus, feed both the direct fingerprint and the
materialized fingerprint into the same slot computation and require the
same start slot and the same walk length for every child. Fingerprints
are asserted identical first, so this test guards the slot mask path
against accidental divergence in the trial probe.

### 3.2 Trial probe outcome agreement

Drive `transposition_probe_direct` and `transposition_probe_prehashed`
against scripted table states and require identical outcomes: merged with
the same node, empty slot at the same position, and identical probe
lengths. Table states: empty table, single entry, colliding entries
requiring multi-step walks, mixed epochs with stale entries skipped,
entries whose fp matches but key differs, a full table forcing the
exhaustion edge with `search_stopped_` and `transposition_exhausted_`
set identically on both sides.

### 3.3 Telemetry identity on the probe path

With telemetry on, require identical increments of probe_steps,
probe_histogram buckets, probe_rebuilds, transposition_merges, and
materialized_nodes between a trial engine and a normal engine over the
same scripted expansion sequence. Count ABBA later rechecks this at
production scale, but the unit test must pin it first.

## 4. Once-per-source context selection

### 4.1 Context content equivalence

For staged children of a multi-source parent expansion, build the source
context once per `expand_source` call and require every context field to
equal the corresponding field of `build_key` for each of its children
except the per-child varying fields: board occupancy words, policy state,
and root depth-zero first move fingerprints. Assert depth equals parent
depth plus one, fixed root child equals the parent root child for
non-root parents, cursor equals the source cursor, boundary count and
boundary words equal the queue suffix reduction, active piece equals the
queue piece at cursor, hold piece and availability equal the source hold.

### 4.2 Child to context resolution

Stage parents with current, held piece, and next piece hold sources so
that two or three contexts coexist. For every staged child require that
the context selected at materialize time by parent, cursor, hold piece,
and hold lock is the context built from its own source inputs, and that
the resulting fingerprint equals the materialized key fingerprint. Add a
regression case with two sources sharing a parent but differing only in
hold lock, and two sources differing only in cursor.

### 4.3 Context lifetime and reset

Expand two parents in sequence and require the scratch array to carry no
stale context from the first expansion into the second. Assert the array
is reset per `expand_parent`, holds at most the staged source count, and
that no context value is written to the arena, a node, a child, or the
transposition table. Memory accounting tests in section 7 confirm the
retained total.

## 5. Rehash and cold path preservation

### 5.1 Unchanged rehash loop

The shrink path at src/tetris_engine.cpp 440 to 460 keeps
`key_from_node` and `transposition_hash` under both builds. A shrink
regression test drives the rehash loop with a populated table and
requires identical surviving entries, identical reinserted fingerprints,
and identical arena contents with and without the trial macro. The trial
build must not route the rehash through the direct routines.

### 5.2 Reinsert helper unchanged

`transposition_reinsert` keeps its materialized key signature. A unit
test requires identical accept and reject outcomes with and without the
trial macro over probe hit, probe miss with slot, and exhausted table
cases.

### 5.3 Batch path equivalence

The batch loop over groups of eight children must produce eight
fingerprints identical to `transposition_hash_batch` over the eight
materialized keys for randomized and adversarial batches, including
partial tail batches that fall through to the single child path. Require
identical hash arrays and identical per-child accept outcomes.

## 6. Macro exclusivity and normal build isolation

### 6.1 Fail closed compile tests

Four compile tests, each expected to fail compilation: trial macro
combined with `TETRIS_CHILD_SOA_TRIAL`, with
`TETRIS_EVAL_INDEX_TRIAL`, with `TETRIS_EVAL_REUSE_TRACE`, and all four
macros together. A fifth test asserts the trial header refuses inclusion
when none of the trial policy macros is set, if the design adopts that
guard. Each test runs as a CTest that passes only when compilation
fails.

### 6.2 Normal binary symbol isolation

A test scans the normal `tetris_profile_value` binary for direct key
symbols and requires zero matches, mirroring the existing child SoA
absence check. It also requires the normal binary hash to remain
`1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`.

### 6.3 Normal profile equivalence smoke

A CTest runs the normal and trial binaries over the smoke workload with
telemetry on and timers off, compares every non-timing field, and
requires exact equality including `mem_retained_bytes`. Timing fields
are `total_s`, every key ending `_ms`, every key ending `_ns`, and
every key ending `_per_s`. This is a smoke precursor only. The binding
proof is the full count ABBA in section 8.

## 7. Retained memory

Require trial `mem_retained_bytes` exactly 266338276, equal to the
normal row. The trial adds a bounded scratch context array sized for at
most three sources, stack resident only, plus no arena, table, or
persistent allocation. Assert the engine below the 268435456 byte cap
with at least 65536 bytes residual margin on both builds. Any retained
difference is a P0 failure because the design allocates no persistent
storage.

## 8. Count ABBA compatibility gate

Before any timing or legacy work, run the full 80-move telemetry on and
timers off count ABBA in normal, trial, trial, normal order doubled to
eight rows. Require exact non-timing identity on every field across all
rows, including the frozen work counts `evals=26632382`,
`searches=1142252`, `materialized_nodes=25757157`, and
`transposition_merges=875225`, with `mem_retained_bytes=266338276` on
both sides. Only the intended zero retained difference is allowed. Any
other difference is FAIL and stops the trial. Timing fields are excluded
and support no speed claim.

## 9. Execution order and acceptance

Run sections 1 through 7 in the implementation branch test suite with a
zero failure requirement before freezing any count protocol. Section 8
is the frozen gate. Section 6 must also pass on the GCC debug, GCC
self-release, and Clang self-release builds named in the count
runbook. No section may be skipped, and no timing evidence may be
collected before section 8 passes.
