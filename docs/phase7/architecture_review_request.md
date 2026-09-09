# Architecture review request: why is the fast-reachability engine slower than legacy?

> Pre-verdict request, preserved as written except for this notice. The review
> verdict arrived on 2026-09-09 and is recorded in
> `docs/phase7/architecture_review_verdict_2026-09-09.md`. Several premises
> below were corrected by the verdict; consult it before citing any number
> from this file.

## What the reader is asked to do

Criticise the new architecture. Starting from the problem, the evidence, and the
commits below, identify what makes the value engine slower than the legacy
engine it replaces, which structural costs are inherent to the design, and
which could be removed without breaking the constraints. Concrete, measured
claims beat general advice. If a claim needs a measurement that does not
exist yet, name the exact command and counters that would settle it.

## Problem statement

The fast-reachability port was expected to be significantly faster than the
legacy pointer-network engine. It is not. On the binding fixed-work protocol,
the candidate is about 4.2 percent slower end to end and about 2.5 percent
slower on per-move p95 latency:

- Gate 1 total median 1.04155 against bar 1.02: FAIL.
- Gate 2 p95 median 1.02547 against bar 1.02: FAIL.

Both the compliant five-pair campaign (`results/phase7/gate12_2026-09-08/`)
and the sanctioned one-time control-corrected rerun
(`results/phase7/gate12_2026-09-09/`, verdict `NO-ADVANCE`) fail. The rerun
used strengthened controls (both binaries pre-warmed, CPU 15 offlined every
run, pinned CPU 7, per-run load recording, quiet machine with load5 0.93 to
1.12), and the failure is robust to noise: only one of five total pairs and
two of five p95 pairs sit at or below 1.02, so no reordering of pairs can
move either median under the bar. This is a genuine residual gap, not
measurement error.

The paradox to explain: the reachability kernel itself measures near zero
cost (arrival routing about 0.4 percent of run time, no allocation in the
hot loop, no deadline-polling cost, path BFS 0.03 percent), yet the engine
around it loses to a pointer-chasing legacy design.

## Architectural contrast under review

Legacy: precomputed pose graph and operation tables built once at startup,
per-move cost dominated by pointer traversal, cached 40-entry drop tables,
and persistent board metadata (`top`, `width`, `height`, `count`, `roof`).
Enumeration, T-spin arrival, classification, and path reconstruction fused
in one search object.

Current: no pose graph, no tables, no cached metadata. `Board` is 64 bytes
of packed occupancy with 64-byte alignment; `roof` and rows derive on
demand. Enumeration is bit-parallel `binary_bfs` with two-channel arrival
propagation and semantic reduction. Rule application rebuilds geometry per
candidate (cell masks, occupancy ops, clear, spin classification,
canonicalization, sort-based dedup). Eval extracts 40 rows per cache miss
at roughly 95 percent miss rate. Pathfinding is fully separated, one path
built after result selection.

The stage profile of the new engine shows the cost moved, not removed:
rule apply plus dedup about 17.7 percent, eval about 17.1 percent,
candidate geometry plus sort about 18 percent, parent scaffold about
23 percent, materialize about 19.6 percent. The kernel swap won its own
leg decisively (materialize per-node cost down about 90 percent versus
legacy) while losing the whole: per-candidate recomputation replaced
table reads nearly everywhere.

## What has been tried

1. The retained optimization stack through commit `58e2739`, ending with
   the pending-heap entry array. Every step was gated on bit-identical
   counters plus a binding off-mode ABBA win. Ten further candidates were
   measured and reverted byte-identically (controlled rejections).
2. Fingerprint-gated transposition entries (`deaf420`), which eliminated
   the truncation cliff and cut probe cost from the dominant symbol to
   about 4 percent.
3. A bounded remediation round (`40abf12`): rule split timer, in-parent
   dedup hash set, eval-cache rebalance to set-associative 64 MiB. Result:
   a consistent 2.5 percent wall-time regression, fully reverted. The
   split timer bounded the dedup-fix upside near 3 percent of run, and the
   larger cache footprint polluted L3 beyond what its hit-rate gain
   recovered. These two targets are now bounded as small-or-negative
   levers in this design space.
4. PGO attempt 1 (`795b2f1`): rejected on held-out seed-8 evidence (total
   median 1.05980, p95 1.07187). Mechanism: inlining and partial inlining
   destroyed the per-function layout the hot loop depends on.
5. Measurement controls: pre-warm of both binaries, CPU 15 offlined during
   timing windows, per-run load1/load5 in every manifest line, artifact
   hash verification per campaign.

Trajectory: roughly 3x slower at the 7.2E-era estimate, to about 4 percent
slower now. The stack recovered nearly the whole gap; what remains is
spread across four buckets with no single dominant lever.

## Constraints the critique must respect

- Bars are locked: total median at most 1.02, p95 median at most 1.02,
  both pair spreads at most 0.04. No gate revision without a declared
  product-latency justification.
- Count-gated: count ABBA must match exactly; any unique-candidate or
  policy-transition delta over 2 percent must be partitioned into
  fixture-identified classes covering 100 percent of the integer delta.
- Semantics-preserving: observable TOJ behavior, ABI, and path protocol
  unchanged. The plan forbids reintroducing the legacy pointer graph,
  `top`, `disable_d`, or per-candidate pathfinding as a performance fix.
- Frozen artifacts under `/home/icly/Documents/tetris_ai_runner_results/`
  are read-only; new evidence gets new dated names.
- No production cutover or legacy deletion until every binding gate
  passes. The migration is paused; this request is analysis, not an
  authorization to change code.

## Commits and artifacts to compare

Candidate source (all `src/` identical at HEAD):

- `58e2739925c034e7a83be501f5d7e5643b88826c` phase7: retain the measured
  optimization stack through the pending-heap entry array.
- HEAD `b3edf56ed03104b26c1b1a3dc907e1e1ec50dd82` adds only campaign
  evidence, docs, tests, and the audit update; engine source unchanged.

Baseline (the legacy side):

- Frozen binary `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`,
  sha256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`,
  unchanged since 7.2A. The audit records that rebuilding `tetris_profile`
  from committed legacy source reproduces this hash byte-identically, so
  the binary is a faithful stand-in for pre-migration source.

Reference analyses (staleness noted where applicable):

- `65ab235b68f23c35e9cfc8f0c5590ff503d84900` hotspot attribution #2 for
  the post-fingerprint candidate (`docs/phase7/hotspot_attribution_f.md`).
  Caveat: it attributes an older candidate (`b8c426ac`), not the current
  `1dccaa80`; rank orders, not absolute shares, are the transferable part.
- `40abf12abb33e815294a91d86fb66acfd6a6ea67` remediation round negative
  result, tree reverted.
- `795b2f1707bb8e12181d0a115ae6eb1f39fd92e7` PGO attempt 1, rejected.
- `5809409` rerun campaign evidence: `results/phase7/gate12_2026-09-09/`
  (`rows.txt`, `MANIFEST.txt`, `verdict.md`) plus
  `docs/phase7/campaign_machine_record_2026-09-09.md`.

Benchmark protocol for any new measurement (binding fixed-work mode):

```bash
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <binary> \
  --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 \
  --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin \
  --iters 1000 --quiet --quiet-version N --telemetry off
```

Baseline uses `--quiet-version 2`, candidate `--quiet-version 3`. Five
pairs in first-engine order baseline, candidate, candidate, baseline,
baseline. Ratios are computed within each pair, then the median of the
five ratios is the gate number. Independently aggregated medians are
never divided.

## Verified layout facts (compiled probe, not speculation)

- `sizeof(Board)` 64, `alignof(Board)` 64.
- `Node` and `Child`: size 192, alignment 64, `Board` at offset 0, locked
  by static asserts. In arena arrays each `Board` fills exactly the first
  cache line of its node.
- Eval memo slots stride 86 bytes with the `Board` at offset 8:
  systematically 64-byte-misaligned, so every memo `Board` touch is an
  unaligned 64-byte vector load. Small tax, but the one layout outlier.

## Questions for the critic

1. Which per-candidate recomputation (cell masks, sort comparisons,
   landing checks, row extraction, feature scans) is redundant across
   candidates of the same parent, and what shared structure would remove
   it without changing the enumerated set?
2. Is the 95 percent eval-cache miss rate a capacity problem, a hash
   problem, or a reuse-distance problem inherent to widening order? The
   64 MiB set-associative attempt says capacity is not the answer; what
   measurement distinguishes the remaining two?
3. The new engine runs full-budget searches where legacy truncated early
   (about 20 percent more parents and searches). How much of the 4.2
   percent is price of fuller search versus price of slower primitives,
   and which experiment separates them?
4. The parent-scaffold residual (heap, child links, about 23 percent of
   run) is now the largest bucket. Is that the fixed cost of the widening
   policy, or is there a cheaper frontier discipline with identical
   selection behavior?
5. Are there remaining wins that are count-safe (identical work vectors)
   versus wins that change work volumes and therefore need partition
   adjudication? Sort each proposal into one of the two bins.
6. If the verdict is that near-parity is the architectural ceiling for
   this design, say so explicitly, with the measurement that proves no
   lever above 1 percent remains.
