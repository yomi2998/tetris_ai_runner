# Qualification campaign protocol (7.2A preparation)

Design and evidence-preparation only. No baseline-versus-candidate claims,
no gate verdicts, and no timed-throughput claims are made here. The
execution slice computes verdicts from this protocol after review.

## 1. Frozen inputs

| Artifact | SHA-256 |
|---|---|
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline` | 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376 |
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate` | 816bcd7d33a7996a207d1c8bbba8bcf838baccfc6246ecb084d445f3b49938fe |

The candidate row above is the 7.2D re-freeze (262,144-entry table); the 7.2B hash (`c4579a87`) identified the superseded freeze. Re-verification rows for the current freeze live in `results/phase7/resize_verify/`.
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp` | 08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450 |
| `artifacts/frozen_29d.bin` (29 doubles, absolute path passed via `--param-file`) | ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 |

The candidate and comparator rows above are the 7.2B re-freeze, produced
by a clean `linux-gcc-self-release` preset build; the 7.2A hashes
(`bf7b9f98`/`7cadfed6`) identified the superseded freeze. The baseline row
is unchanged since 7.2A. Re-verification rows for the current freeze live
in `results/phase7/refreeze_verify/`: candidate determinism (two identical
rows modulo timers), comparator-vs-baseline work parity
(40546/199806/5521 with matching dead, game, and pool columns), and
rejection behavior (exit 1, no row).

- The parameter file bytes equal the compiled-in production defaults in all
  three binaries (verified field by field before freezing).
- Both profile artifacts compile the same combo-table bytes
  (`0,0,0,1,1,2,2,3,3,4`, table max 10); content hash `948edb40...` recorded
  in `docs/phase0/artifact_hashes.txt`.
- Frozen copies are read-only. Any re-freeze (for example after the timer
  remediation in Section 6) records new hashes and re-verifies parity; it
  never overwrites these rows in place.
- `TETRIS_AI_PARAM_FILE` is explicitly unset on every run; it is read only
  by `ai.cpp`, never by the profiles.

## 2. Run matrix

Fixed-work mode (binding) and timed mode (throughput gate), each seed run
with identical warmup, depth, parameter file, pinned core, and machine
controls from `machine_record.md`:

```bash
taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.ENGINE \
  --warmup-moves 20 --moves 200 --iters 1000 --seed SEED \
  --maxdepth 6 --param-file /absolute/path/frozen_29d.bin \
  --quiet --quiet-version 2
```

```bash
taskset --cpu-list 7 /home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.ENGINE \
  --warmup-moves 20 --moves 200 --ms 20 --seed SEED \
  --maxdepth 6 --param-file /absolute/path/frozen_29d.bin \
  --quiet --quiet-version 2
```

The value artifact substitutes `--quiet-version 3`; the comparator runs the
same commands with `--quiet` and emits `PROFILE_CMP`. Binding seed is 1;
seeds 2 and 3 are required diagnostics run with the same matrix.

Both binaries expose `--timers on|off` (default on) alongside
`--telemetry on|off`. `--telemetry off` detaches everything, exactly as
before. `--telemetry on --timers off` counts without component timer reads
(the comparator additionally skips normalization, so `unique_candidates`,
`unmatched_candidates`, and all timer fields report unavailable while all
other counts stay numeric). All timed-mode rows on both engines, and every
binding total and count on the value side, use counters-only mode; timer
spans always come from fixed-work full-timer runs. A `timers` field
(`on`, `off`, `na`) trails each record version after the last previous
field, shifting no existing order.

## 3. Pair order, ratios, and preservation

- Five paired repetitions per mode with first-engine order baseline,
  candidate, candidate, baseline, baseline (ten runs per mode per seed).
  Within each pair the second engine runs immediately after the first.
- Every verdict ratio is computed candidate-to-baseline (or off-to-on for
  the telemetry gate) inside each pair first; the five pair ratios are then
  summarized by median. Independently aggregated medians are never divided.
- Raw preservation: `results/phase7/<campaign>/rows.txt` holds one output
  row per run in execution order, plus `MANIFEST.txt` mapping each line to
  artifact path and hash, full command, telemetry mode, seed, started
  timestamp, exit status, and pair assignment. No aggregation happens during
  collection.

## 4. Gate formulas and column mapping

All gates use seed 1 fixed-work mode unless stated. `V3(...)`, `V2(...)`,
and `CMP(...)` name fields of the three record versions.

1. Total time: median over pairs of `V3(total_s) / V2(total_s)` at most 1.02.
2. p95 latency: median over pairs of `V3(p95_ms) / V2(p95_ms)` at most 1.02.
   (A resampling diagnostic interval may be reported; it never overrides.)
3. Count partition: relative change `(candidate - baseline) / baseline` on
   unique semantic candidates (`V3(unique_candidates)` vs
   `CMP(unique_candidates)`) and policy transitions (`V3(transitions)` vs
   `CMP(transitions)`). Beyond 2 percent in magnitude, the delta partitions
   fully into fixture-identified new legal candidates, removed semantic
   duplicates, lockout-semantic changes, and defects; any unclassified
   remainder invalidates the campaign.
4. Binding component rates, each candidate-to-comparator ratio at most 1.02:
   - Per enumeration call: `V3(enum_ns) / V3(searches)` vs
     `CMP(search_ns) / CMP(searches)`.
   - Per unique candidate: `V3(rule_ns) / V3(unique_candidates)` vs
     `(CMP(eval_hit_ns) + CMP(eval_miss_ns) + CMP(transition_ns)) /
     CMP(unique_candidates)`, with both scopes stated.
   - Per eval request, hit path: `V3(eval_hit_ns)` over
     `V3(eval_memo_hits) + V3(cache_hits)` vs
     `CMP(eval_hit_ns) / CMP(eval_hits)`.
   - Per eval request, miss path: `V3(eval_miss_ns) / V3(eval_computed)` vs
     `CMP(eval_miss_ns) / CMP(eval_calls)`.
   - Per policy transition: `V3(policy_ns) / V3(transitions)` vs
     `CMP(transition_ns) / CMP(transitions)`.
   - Per materialized node: `V3(materialize_ns) / V3(materialized_nodes)`
     vs `CMP(alloc_ns) / CMP(materialized_nodes)`, both scopes stated.
   - Per selected-path state: `(V3(path_find_ns) + V3(path_replay_ns)) /
     V3(path_states)` vs `CMP(path_ms) * 1e6 / CMP(path_states)`, with
     differing early-exit behavior stated.
5. Path overhead: `V3(path_ms)` over the full move total
   (`V3(setup_ms) + V3(run_ms) + V3(path_ms) + V3(apply_ms)`) at most 0.02.
6. Timed mode, seed 1, after the item 3 partition: completed work per
   second must not regress more than 2 percent on widening iterations
   (`V3(widening_iters) / V3(total_s)` vs the `CMP` counterparts),
   expanded parents (`V3(parents)`), and retained transitions
   (`V3(transitions)`).
7. Seeds 2 and 3: rows must parse, `replay_failures` must be zero, and count
   deltas must classify; latency deltas are reported only.
8. Raw non-T enumeration and item 9 T-gates run through the existing corpus
   harness (`tests/run_perf_gate.py` over `arrival_candidates`,
   `reference_a_frozen`, and the `raw_bench` pair on the frozen corpus,
   same pinned core): median paired time-per-parent at most 1.00 (non-T),
   T enumerator at most 1.02 per normalized candidate vs frozen legacy and
   at least 2x vs the Reference A wrapper.
9. Covered by the same harness run as item 8.
10. Completeness checklist per campaign: warmup handling stated, artifact
    hashes recorded, candidate counts and cache hits present, completed
    work recorded, run order manifest present, machine record attached.
11. No retuning: the same absolute parameter file and combo bytes feed every
    run; any deviation invalidates the campaign.

## 5. Overhead evidence plan

- Candidate telemetry switch: same binary and fixed-work command, counters
  on versus off, same five-pair order, whole-run `total_s` ratio
  (counters-only over off) at or below 1.005. The failed full-timer
  pre-gate below is superseded by the counters-only re-gate.
- Comparator instrumentation: per-component absolute overhead bounds where
  measurable; normalization sits outside every binding span by construction
  (`search_ns` covers the base call only, `norm_ns` is diagnostic), and the
  reused probe table performs zero steady-state allocation, so fixed-work
  leakage into binding rates is structurally zero with any residual
  measured and bounded alongside. Timed-mode legs use counters-only rows on
  both sides and proceed only while the re-measured counters-only-vs-frozen
  total delta stays at or below 1.02 (single-sample evidence puts the
  counters-only comparator at or below frozen total time; the execution
  slice pairs this properly). Measured rates and overhead evidence are
  reported separately; no global percentage is subtracted from any timing.

## 6. Pre-execution prerequisites (blockers, not waivers)

- P1. Value timer remediation: the original pre-gate failed at a median
  on/off ratio of 1.165 against the 1.005 bar (Section 7), and the
  counters-only re-gate failed as stated at 1.01412 with a structural
  +1.4 percent cycle cost under plus-or-minus 2 percent wall noise
  (Section 8). P1 is resolved by the twin-run methodology amendment
  (Section 9), not by a passing re-gate: binding totals carry zero
  instrumentation by construction instead of clearing an unresolvable bar.
- P2. Comparator allocation-span timer for the item 4 materialized leg:
  resolved by the `alloc_ns` counter (Section 8 evidence).
- P3. Comparator normalization cost review: resolved by the reused probe
  table, the split `search_ns`/`norm_ns` spans, and the counters-only
  timed-mode rule with its 1.02 bound.

## 7. Telemetry-overhead pre-gate result: FAIL

- Command: candidate binary, `--warmup-moves 20 --moves 200 --iters 1000
  --seed 1 --maxdepth 6`, pinned core 7, five on/off pairs in ABBA+A order.
  Raw rows: `results/phase7/telemetry_pregate_seed1.txt`.
- Per-pair on/off total-time ratios: 1.16466, 1.11685, 1.17730, 1.21810,
  1.16436. Median 1.16466 against the 1.005 bar: FAIL.
- Work counts are bit-identical across all on-runs (6,732,608 evals and
  transitions, 304,796 searches, 19,549 widening passes, 159,205 parents,
  200 path calls), so decisions are unaffected; the delta is pure
  measurement cost. Scale analysis attributes it to per-evaluation and
  per-candidate `timer_now` clock reads (tens of millions per run at
  roughly 25 ns each), confirmed by an independent interleaved on/off mini
  run reproducing a 12 to 14 percent gap regardless of drift order.
- Consequence: P1 above. No baseline capture has occurred and none is
  claimed.

## 8. Counters-only re-gate result: FAIL as stated, with root cause

- Command: frozen candidate, `--telemetry on --timers off` versus
  `--telemetry off`, same five-pair order and workload as Section 7.
  Raw rows: `results/phase7/telemetry_pregate2_seed1.txt`.
- Per-pair counters-only/off total-time ratios: 1.55563 (cold-start first
  run), 0.98813, 1.01679, 1.01412, 0.99945. Median 1.01412 against the
  1.005 bar: FAIL as stated.
- Hardware-counter decomposition (same workload, `perf`: counters-only vs
  off): cycles +1.4 percent against instructions +0.2 percent. Cycle counts
  are frequency-invariant, so this is structural, not drift: roughly 50M
  counter increments and taken branches in the hot evaluation and
  transition loops. Same-mode wall-time repeats spread plus or minus 2
  percent on this machine (unpinned frequency), so a five-pair wall-time
  median cannot resolve the 0.5 percent bar here in either direction.
- Forwarded decision (no further code written in this slice): either (A)
  authorize a batched-counting design (per-move local tallies flushed at
  read points) and re-gate, or (B) amend the method so binding totals come
  from `--telemetry off` rows with counts and rates from deterministic
  twin runs (counters-only for counts, full-timer for rates), which carries
  zero instrumentation in the totals by construction. Both need explicit
  approval; baseline capture stays blocked meanwhile.

## 9. Twin-run methodology amendment (authorized option B)

This section records an explicit, justified deviation from plan Section
17.2: the 0.5 percent pass/fail pre-gate is replaced, not waived, as
follows.

- Binding latency totals (gates 1 and 2) come from `--telemetry off` rows
  on both engines and carry zero instrumentation by construction. This
  deviates from the frozen profile's on-mode recording convention; the
  baseline side of gates 1 and 2 therefore uses off-mode baseline rows,
  collected under the same pair order and controls.
- Binding counts come from deterministic twins of the same workload
  (bit-identical work vectors proven across modes): counters-only twins on
  the candidate side, full-timer twins on the comparator side (its
  normalization is skipped counters-only); binding rates
  come from full-timer fixed-work twins.
- Per-mode measured overhead is still disclosed: the value perf
  decomposition (+1.4 percent cycles structural, Section 8), the
  comparator on/off/disarmed evidence, and the paired
  counters-only-vs-frozen total delta with its 1.02 bound for gate 6 legs.
- Per-side measured overhead accompanies each gate 4 and gate 6 verdict:
  the measured overhead on each side is stated from measurement, the bias
  direction is stated from measurement (never assumed conservative), and
  worst-case bands are given. In particular no claim is made that both
  comparisons err against the candidate: the candidate side carries its
  measured counting cost while the comparator side carries counting plus
  `make_path` under its bound, so the net bias may favor either side by a
  bounded amount that the verdict must quantify.

### Per-gate row sourcing

| Gate | Rows per seed and mode | Pair order and count |
|---|---|---|
| 1, 2 (fixed-work totals) | Off-mode rows, both engines, seeds 1/2/3 | Five ABBA+A pairs per mode per seed |
| 3 (count partition) | Candidate counters-only rows plus comparator full-timer rows, seeds 1/2/3 (the comparator skips normalization in counters-only mode, so its gate 3 rows must be full-timer twins) | Deterministic twins, two runs per side per seed |
| 4 (component rates) | Full-timer fixed-work rows, both engines, seed 1 (seeds 2/3 diagnostics) | Same pairs |
| 5 (path overhead) | Value full-timer fixed-work rows, seed 1 (seeds 2/3 diagnostics) | Within-run share, no pairing |
| 6 (timed throughput) | Counters-only timed rows, both engines, seed 1 (seeds 2/3 diagnostics) | Five ABBA+A pairs |
| 7 (seed diagnostics) | All row kinds, seeds 2/3 | Correctness and classification only |
| 8, 9 (corpus rates) | `run_perf_gate.py` REP output, pinned core | Per existing harness pairing |
| Telemetry disclosure | On/counters-only/off triplets, fixed-work, seed 1 | Adjacent triplets, reported not gated |

### Noise rule

Gate verdicts report all five pair ratios. Measurement uncertainty that can
flip a 2 percent verdict blocks any pass claim; the execution report
carries per-gate spreads alongside medians. This is the standing 7.1A close
rule applied to the recorded plus-or-minus 2 percent unpinned-frequency
noise reality.
