# Qualification campaign protocol (7.2A preparation)

Design and evidence-preparation only. No baseline-versus-candidate claims,
no gate verdicts, and no timed-throughput claims are made here. The
execution slice computes verdicts from this protocol after review.

## 1. Frozen inputs

| Artifact | SHA-256 |
|---|---|
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline` | 84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376 |
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate` | bf7b9f98e58f0261075c2a0faedd8e51173ea4be9317a3e1751b359cc876fee3 |
| `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp` | 7cadfed6953ea0af6b21b7b77a70de80ad46f0566f1de2bf6394262f64fb2969 |
| `artifacts/frozen_29d.bin` (29 doubles, absolute path passed via `--param-file`) | ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037 |

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
     vs a comparator allocation span: BLOCKED prerequisite (Section 6).
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
  (on/off) at or below 1.005. Measured before baseline capture as the
  pre-gate (Section 7).
- Comparator instrumentation: per-component absolute overhead bounds where
  measurable (tiny-component rule); plus the disclosed
  comparator-vs-frozen total-time delta on one identical fixed-work
  workload (observed 32.164 s vs 18.509 s with bit-identical work vectors;
  rates are unaffected but absolute bounds must accompany them). Measured
  rates and overhead evidence are reported separately; no global percentage
  is subtracted from any timing.

## 6. Pre-execution prerequisites (blockers, not waivers)

- P1. Value timer remediation: the pre-gate failed at a median on/off
  ratio of 1.165 against the 1.005 bar (Section 7). Baseline capture must
  not proceed until a revised timer design holds the same component scopes
  within the bar; the revision needs design review, a fresh candidate
  freeze with re-verified determinism, and a passing pre-gate.
- P2. Comparator allocation-span timer for the item 4 materialized leg, or
  an approved alternative with the same scope; no verdict on that leg
  without it.
- P3. Comparator normalization cost review: the 1.74x total-time delta is
  disclosed; per-component absolute bounds are required before binding use.

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
