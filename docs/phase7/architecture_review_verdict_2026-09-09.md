# Architecture review verdict, 2026-09-09

## Status

This is the post-verdict record. The pre-verdict request lives in
`docs/phase7/architecture_review_request.md`; several of its premises are
corrected below. Raw evidence is preserved byte-for-byte in
`results/phase7/architecture_review_2026-09-09/raw/` with `SHA256SUMS`.
No source or frozen artifact was modified to produce this verdict.

## Verdict sentence

Near parity is not a demonstrated architectural ceiling. The residual
failure is real, but the evidence does not show that unavoidable
fast-reachability work causes it. The strongest current explanation is:

1. The candidate computes far more evaluations because reuse is scoped to
   one parent.
2. It stages and materializes large duplicate records around every
   surviving child.
3. It processes substantially more canonical candidates before converging
   to almost the same number of policy transitions.
4. It repeatedly converts packed boards into row representations.
5. The actual dynamic reachability and final path kernels remain
   comparatively small.

Only the first point is a measured cross-engine asymmetry large enough to
dominate. The exact contribution of each item to the binding 4.155 percent
ratio has not yet been causally isolated.

## Corrections to the review premises

Several premises in the request and reviewer reports are stale:

- The current eval memo is SoA, not an 86-byte AoS. Board values live in a
  separately aligned vector. Current logical storage is 90 bytes per entry:
  fingerprint 8, board 64, evaluation 16, next link 2.
- Production EvalCache is disabled. Current rows report
  `cache_requests=0`. The relevant hit rate is the parent-local exact memo
  rate.
- The final candidate does not expand about 20 percent more parents. It
  expands 2.75 percent fewer.
- Sort comparisons no longer reconstruct cells. CandidateSortEntry stores a
  packed 64-bit key and the comparator compares that key.
- The old 0.4 percent arrival-routing figure does not transfer to the final
  binary. A fresh telemetry-off cycle sample found arrival_routing at 1.00
  percent and run_fixpoint at 1.15 percent. They are still minor, but not
  zero.
- The 23 percent parent-scaffold number is a timer residual, not a heap
  measurement. It includes loop and control work and substantial nested
  timer-read overhead.
- There is no per-child double tail traversal in the current fresh-child
  path. promote carries the tail through append_fresh_child_link.
- The old claim of an approximately 90 percent materialization-rate win
  does not apply cleanly to the final stack. Current candidate and legacy
  timer scopes are not equivalent.

## Current compiled layouts

| Type | Size |
|---|---:|
| Board | 64 |
| Node | 192 |
| Child | 192 |
| PolicyState | 40 |
| Evaluation | 16 |
| Candidate | 4 |
| CandidateSortEntry | 16 |
| TranspositionKey | 152 |
| TranspositionEntry | 16 |
| PendingEntry | 16 |

Removing only the now-dead pending-link fields from Node would not reduce
its 192-byte rounded size.

## Current fixed-work counts

Fresh 80-move, seed-1, 1000-iteration rows with telemetry on and component
timers off. The comparator reproduces the legacy baseline evaluation,
transition, and search totals.

| Counter | Candidate | Legacy comparator | Candidate delta |
|---|---:|---:|---:|
| Widening iterations | 80,000 | 80,000 | 0 |
| Expanded parents | 599,166 | 616,097 | -2.75% |
| Enumeration/search calls | 1,142,252 | 1,006,672 | +13.47% |
| Canonical outputs / legacy search results | 32,446,394 | 23,089,552 | +40.52% |
| Policy transitions | 26,632,382 | 26,546,434 | +0.32% |
| Materialized nodes | 25,757,157 | 22,770,022 | +13.12% |
| Eval requests | 26,632,382 | 22,770,022 | +16.96% |
| Actual eval computations | 25,948,219 | 5,576,912 | 4.653x |

Candidate filtering is also measurable:

- 65,566,529 raw kernel landings become 32,446,394 canonical candidates, a
  50.51 percent reduction.
- Those become 26,632,382 eval requests after source-local result dedup,
  another 17.92 percent reduction.
- Candidate memo hits are 684,163 of 26,632,382, or 2.569 percent.
- Legacy eval-table hits are 17,193,110 of 22,770,022, or 75.508 percent.

The legacy table is depth-indexed, direct-mapped, and hash-only. Its hit
rate cannot simply be copied because the candidate requires exact-board
verification. It nevertheless proves that the search order presents
substantial broader reuse.

## Ranked explanation

### 1. Evaluation reuse is the largest verified asymmetry

`Engine::clear_eval_memo()` runs at every parent expansion. Within one
parent the memo has sufficient capacity, uses exact-board checks, and does
not evict entries. Therefore its 2.57 percent hit rate is not a local
capacity problem, and hash collisions cannot turn matches into misses. It
is primarily a scope and reuse-distance problem.

Diagnostic timer rows measured candidate miss computation at 78.00 ns and
legacy miss computation at 73.88 ns, ratio 1.056. Those timings are
perturbed and diagnostic, but they show candidate evaluation arithmetic is
only modestly slower per miss. The decisive difference is computing it 4.65
times as often.

The rejected 64 MiB cache does not close this question. It raised hit rate
from 4.812 to 17.712 percent and reduced computations by 13.55 percent, but
its copied-board footprint made wall time about 2.5 percent worse. That
rejects that cache representation, not exact cross-parent reuse.

The best remaining experiment is a compact index containing fingerprint,
node ID, and epoch, with equality verified against the arena node board. It
would reuse boards and evaluations already stored in nodes rather than
copying another 64-byte board into every cache slot.

### 2. Child, node, key, and frontier dataflow is unnecessarily bulky

Every surviving candidate is first written as a 192-byte Child, then most
are copied into a 192-byte Node. Each also goes through a 152-byte logical
transposition key plus separate 16-byte transposition and pending records.
For the 80-move candidate row, record-size products are: Child staging
5.113 GB, Node records 4.945 GB, pending records 0.412 GB, transposition
records 0.412 GB. These are logical record-size products, not measured DRAM
traffic. They nevertheless expose a candidate-only duplicate staging path.

Fresh candidate diagnostics support the ranking: `Engine::materialize` at
10.26 percent of self cycles, `Engine::promote` at 6.80 percent, the
timer-instrumented search_materialize scope at 26.20 percent of run_ms, and
the residual inside the parent scope at 23.23 percent.

The removable part is not exact state storage itself. It is storing the same
board, state, and evaluation first in Child and then in Node; mixing
expansion-cold state with heap and link-hot state; constructing full keys
rather than hashing and comparing direct views; and rebuilding
queue-boundary suffix data for siblings sharing cursor and source context.

The smallest structural trial is Child-only SoA staging: aligned boards in
one array and compact metadata in another, preserving indices and order. A
broader Node hot/cold split comes later.

### 3. Fast enumeration is cheap per call, but its wrapper handles excess volume

Timer diagnostics found candidate enumeration at about 0.621 times legacy
search time per call, with acknowledged scope differences. The replacement
kernel itself is not slow. The cost sits around it: 65.57 million emitted
landings packed, bucketed, sorted, and reduced; 32.45 million candidates
applied; 5.81 million applied results discarded by source-local
board/outcome dedup before evaluation.

Redundancies include enumeration building cell geometry to form the
canonical key while application reconstructs the same geometry and landing
facts; kernel-produced landings validated again by landing data; and
source-result dedup linearly comparing already constructed result boards.

Two proposed fixes must not be repeated unchanged: outcome-first comparison
already regressed count mode to 1.03785, and trusted apply improved count
mode but regressed binding off mode to about 1.00520.

A fixed-domain canonical-rank bitmap or radix structure could avoid general
sorting while emitting exactly the current packed-key order. It must
preserve the same minimum-rotation representative and first-survivor
behavior. This is distinct from the rejected outcome-first experiment and
does not require a pose graph.

### 4. Policy representation conversion is real but secondary

`Policy::evaluate` exports 40 rows from every computed board, so the
measured row executes exactly 25,948,219 x 40 = 1,037,928,760 search-side
`Board::row()` calls. `transition_known_lockout` then exports 0, 21, or 40
rows depending on lockout and queue state; the exact branch totals are not
currently counted.

A transient evaluate-and-safe path could export rows once, calculate the
safe margin before `init_t_value` mutates its temporary rows, and retain the
existing arithmetic order. It would not add top or persistent board
metadata. Simple row-reuse, bulk-row, and unrolled-row variants have already
performed poorly, so this should be instrumented before another rewrite.

Feature scans over genuinely different result boards are not inherently
redundant. Feature scans over an exactly repeated board are, which again
makes broader exact evaluation reuse the first target.

### 5. Reachability, final path, and schedule setup are not leading targets

The fresh telemetry-off profile found approximately 2.15 percent self
cycles in the two named dynamic reachability primitives. The complete
enumeration wrapper is larger because it includes emission,
canonicalization, and sorting.

Final pathfinding took 5.97 ms over an 80-move, 7.44-second candidate run.
It expanded many more states than legacy (76,513 versus 3,235) but remains
too small to explain the result.

Widening-width calculation and similar pass setup fit outside almost all of
the measured parent scope. They are not plausible multi-percent levers.

## Inherent versus removable costs

| Inherent under the constraints | Removable implementation cost |
|---|---|
| Dynamic reachability once per expanded source | Recomputing geometry already derived during enumeration |
| Two arrival channels and deterministic canonical output | General sorting if an exact ordered bounded representation replaces it |
| Exact board/state verification for transposition matches | Materializing a 152-byte key and rebuilding sibling-common suffixes |
| Equivalent TOJ feature computation for each genuinely new board | Limiting reuse to one parent |
| Exact maximum selection and tie behavior | Pairing-heap representation, if another structure reproduces the pop trace |
| Storage of live board and policy state | Duplicate 192-byte Child-to-Node staging |
| One final verified path | Re-exporting rows separately for evaluation and safe-margin calculation |

The important distinction is that exactness is inherent, while the current
way exactness is represented is not.

## Frontier answer

The widening policy requires exact maximum selection. It does not require
this pairing heap. A binary or d-ary heap using the current total
comparator (value descending, then Node ID ascending) should return the
same node sequence if insertion and node-ID order remain unchanged. It
might still be slower because the current pairing heap has constant-time
meld and was itself a retained optimization.

Before replacing it, split the parent residual into heap_push_ns,
heap_pop_ns, heap_pushes, heap_pops, heap_melds, first-pass links and
second-pass melds per pop, build_key_context_ns, build_key_dynamic_ns,
hash_ns, probe_walk_ns, probe_verify_ns, node_construct_ns, fresh_link_ns,
and merged_link_ns. Require hashes of the full heap-pop sequence,
expanded-parent sequence, selected root child, and final path. The current
23.23 percent residual alone does not justify a frontier rewrite.

## Experiments, classified

### Search-count-safe

These preserve candidate, order, and search vectors. Implementation counters
such as cache hits or comparisons may intentionally change.

1. Exact reuse-distance trace and offline cache simulation.
2. Node-backed exact evaluation index.
3. Child SoA staging, then Node hot/cold storage.
4. Direct-view key hashing and equality with a once-per-source key context.
5. Evaluation and safe-row export fusion with bit-identical results.
6. Ordered canonical bitmap/radix output.
7. Alternative exact frontier with identical pop trace.

For every cache experiment, require unchanged eval-request sequence and
evaluation-result hashes. Report changed eval_computed and cache counters
rather than treating them as semantic drift.

### Work-volume-changing or prohibited without adjudication

- Dropping or reordering candidates.
- Moving outcome-based filtering ahead of current canonical ordering.
- Pruning before policy transition or transposition.
- Matching legacy by imposing an artificial candidate, parent, or node
  quota.
- Approximate or hash-only evaluation reuse.
- Changing frontier comparison or tie behavior.
- Any change to widening order.

Partition evidence does not excuse observable TOJ, candidate-order, or path
changes.

## Measurement that settles the cache question

Add a test-only exact board-ID trace at evaluate_once, recording request
index, parent, depth, source, and whether a matching materialized node
already exists. Offline, report distinct boards and compulsory requests;
same-parent versus cross-parent repeats; request-distance and
parent-distance histograms; reuse matrices by source and depth; exact
fingerprint collisions; fully associative LRU hits at powers-of-two
capacities; direct-mapped and 2/4/8-way hits at the same capacities;
conflict misses versus fully associative capacity misses; and hits
attainable from currently live materialized nodes.

This directly distinguishes capacity (fully associative hit rate rises with
capacity), conflict/hash layout (associative simulation beats an
equal-capacity fully exact set layout), reuse distance (repeat distances
exceed all practical capacities), and scope (cross-parent repeats dominate
same-parent repeats). Run it under the exact fixed-work parameters with
telemetry on and timers off. It is diagnostic only.

## Reproduction commands

The fresh count rows came from these artifacts:

```bash
PARAM=/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin
CAND=/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08
LCMP=/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp

env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$CAND" \
  --seed 1 --warmup-moves 20 --moves 80 --maxdepth 6 \
  --param-file "$PARAM" --iters 1000 --quiet --quiet-version 3 \
  --telemetry on --timers off

env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 "$LCMP" \
  --seed 1 --warmup-moves 20 --moves 80 --maxdepth 6 \
  --param-file "$PARAM" --iters 1000 --quiet \
  --telemetry on --timers off
```

Saved outputs (now archived under
`results/phase7/architecture_review_2026-09-09/raw/`):

- architecture-review-2026-09-09-candidate-count.txt
- architecture-review-2026-09-09-legacy-cmp-count.txt
- architecture-review-2026-09-09-legacy-count.txt
- architecture-review-2026-09-09-candidate-timers.txt
- architecture-review-2026-09-09-legacy-cmp-timers.txt
- architecture-review-2026-09-09-candidate-perf-report.txt
- architecture-review-2026-09-09-legacy-perf-report.txt
- architecture-review-2026-09-09-candidate-perf-row.txt
- architecture-review-2026-09-09-legacy-perf-row.txt

For any optimization, use three separate campaigns:

1. Count ABBA, telemetry on, timers off, old/new/new/old.
2. Binding telemetry-off five pairs, first-engine order old/new/new/old/old.
3. Timer or perf diagnostics separately, never as verdict evidence.

The binding command remains:

```bash
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <binary> \
  --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 \
  --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin \
  --iters 1000 --quiet --quiet-version N --telemetry off
```

## Ceiling threshold

The total ratio needs only a 2.069 percent candidate-time reduction to move
from 1.04155 to 1.02. The p95 ratio needs about 0.533 percent. Current
evidence has not ruled out a gain of that size.

A defensible ceiling claim would require all of the following on the exact
final stack:

1. Exact reuse-distance evidence showing that a compact verified cache
   cannot save enough work.
2. Split materialization, key, heap, and link measurements, not a residual.
3. Controlled count-identical trials of compact eval reuse, staging
   compaction, and direct key views.
4. Candidate-order, parent-pop, selection, and path trace identity for every
   trial.
5. Five-pair telemetry-off negative results with the prescribed spreads.
6. A matched-input primitive replay separating count amplification from
   per-unit cost.
7. Evidence that the remaining unavoidable reachability and
   exact-verification work alone exceeds the gate budget.

That evidence does not exist. The prior negative cache, PGO, row, geometry,
and key experiments are implementation-specific and revision-specific. They
do not prove an architectural ceiling.

The three reviewer audits were directionally correct about poor reuse,
repeated representation work, and bulky staging. Their stale layout,
sorting, parent-count, and outcome-first assumptions must not be carried
forward.

## Provenance

- Verdict received from the owner review on 2026-09-09; transcribed here
  without source or frozen-artifact modification. The tracked tree remains
  unchanged by the review itself.
- Raw row hashes: see `raw/SHA256SUMS` in this directory.
