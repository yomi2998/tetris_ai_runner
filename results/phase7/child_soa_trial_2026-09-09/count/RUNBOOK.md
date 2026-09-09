# Child SoA count gate

## Scope

This is the mandatory semantic and work-vector gate for the trial-only two-array Child staging layout. It does not establish wall performance and does not authorize production cutover, legacy comparison, qualification, gate revision, quality work, or deletion.

Trial source commit: `e02f476`.
Partition-trace target commit: `369d447`.

## Frozen artifacts

Normal:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_value`
SHA-256: `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`

Child SoA trial:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_child_soa`
SHA-256: `721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91`

Normal partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition`
SHA-256: `5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1`

Child SoA partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition_child_soa`
SHA-256: `6b5b520fdf253def718b3743f8fd53d54eb7636329c83f9c39b2510a65fa582a`

Parameters:
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
SHA-256: `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

Verify all hashes before collection. A mismatch is `BLOCKED`.

## Preconditions and controls

- Execution HEAD contains `e02f476` and `369d447` and has no tracked or staged changes.
- CPU 7 governor `performance`.
- CPU 7 energy performance preference `performance`.
- Global boost `1`, unchanged.
- Five-minute load average below 1.5 before collection. Recheck once per minute for at most 10 minutes. If it never passes, write `BLOCKED` evidence and run nothing.
- Take CPU 15 offline once before the partition trace, keep it continuously offline through both trace records and runs 1 through 8, and restore it online after run 8 and on every abort path.
- Read load1 and load5 before each run. If load5 is at least 2.0 before a not-yet-started ABBA half, recheck once per minute for at most 5 minutes and resume only after it falls below 1.5. Otherwise abort the entire gate without replacement runs.

Record HEAD, ancestry, clean state, hashes, machine details, controls, UTC timestamps, loads, the continuous CPU 15 offline window, exits, and restoration in `MACHINE.md` and `MANIFEST.txt`.

## Partition trace identity

Before ABBA, run the normal and SoA partition recorders once each with distinct external output prefixes:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <partition-recorder> record --seed 1 --warmup-moves 0 --moves 20 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --record-out <external-prefix>
```

Store the raw files under `/home/icly/Documents/tetris_ai_runner_results/phase7/child_soa_trial_2026-09-09/trace/`, make them read-only after collection, and record their hashes and sizes in `trace_summary.json`.

The normal and SoA `.inputs.bin`, `.moves.tsv`, and `.run_totals.tsv` files must be byte-identical. This establishes identical ordered first-observed expansion inputs, exact candidate/application/outcome/survivor records, repeated-input multiplicities, and per-move work vectors. Compare the two stdout `PROFILE_V3` rows with the same non-timing rule as ABBA, allowing only `mem_retained_bytes` to differ. Any mismatch is `FAIL`; run no ABBA or timing.

## Commands and order

After trace identity passes, every ABBA run uses telemetry on and component timers off:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 80 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version 3 --telemetry on --timers off
```

Run exactly:

1. normal
2. SoA
3. SoA
4. normal
5. normal
6. SoA
7. SoA
8. normal

No extra, replacement, or selectively retained row is permitted. A nonzero exit or non-`PROFILE_V3` row is `ABORTED`; preserve partial evidence and stop.

## Raw evidence

Write each unedited row to `runN_<normal|soa>.txt`. Write `rows.sha256`, `binaries.sha256`, `commands.txt`, `MACHINE.md`, `trace_summary.json`, and `summary.json`. `summary.json` must list every check with its observed values.

## Required identities

For determinism, compare all non-timing fields within each engine across repeats. For cross-engine identity, compare every non-timing field except the single mapped layout field `mem_retained_bytes`.

Timing fields are `total_s`, every key ending `_ms`, every key ending `_ns`, and every key ending `_per_s`. They are excluded from this count gate and cannot support a speed claim.

Required exact common values include:

- `moves=80`
- `evals=26632382`
- `transitions=26632382`
- `searches=1142252`
- `dead_moves=0`
- `games=0`
- `node_live_delta_bytes=-2314176`
- `warmup_moves=20`
- `seed=1`
- `iters=1000`
- `maxdepth=6`
- `mode=iters`
- `telemetry=on`
- `parents=599166`
- `widening_iters=80000`
- `raw_landings=65566529`
- `unique_candidates=32446394`
- `rule_transitions=32446394`
- `eval_memo_hits=684163`
- `eval_computed=25948219`
- `cache_requests=0`
- `cache_hits=0`
- `cache_misses=0`
- `cache_replacements=0`
- `materialized_nodes=25757157`
- `transposition_merges=875225`
- `promotions_refused=27680`
- `pending_end_max=425068`
- `texhaust_moves=0`
- `path_calls=80`
- `path_states=76513`
- `replay_failures=0`
- `arena_reserved_bytes=208720320`
- `idmap_reserved_bytes=4348340`
- `raw_unique_ratio_x1000=2020`
- `timers=off`

Required mapped memory values:

- normal `mem_retained_bytes=266338276`
- SoA `mem_retained_bytes=266153956`
- exact reduction `184320`
- both below `268435456`
- SoA residual margin at least `65536`

The short normal-versus-trial profile CTest, 267-check trial suite, normal symbol-isolation test, GCC debug comparison, GCC self-release comparison, and Clang self-release comparison must pass before acceptance.

## Verdict

`PASS` requires every hash, control, byte-identical partition trace, deterministic repeat, cross-engine identity, exact expected value, mapped memory value, cap, residual margin, and prerequisite test to pass. Any failure is `FAIL`, `BLOCKED`, or `ABORTED` according to the conditions above.

Only `PASS` permits freezing a telemetry-off normal-versus-SoA selector. Count evidence never permits a legacy run or wall-speed inference.
