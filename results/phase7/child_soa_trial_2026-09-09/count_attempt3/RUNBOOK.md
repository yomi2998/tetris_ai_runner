# Child SoA count gate, fresh attempt 3

## Scope

This is a wholly fresh execution of the mandatory semantic and work-vector gate for the trial-only two-array Child staging layout.

The provider failure with no execution, the archived load-blocked attempt, and rejected attempt 2 contribute no row. Attempt 2 is terminal `FAIL` because it deleted a CPU-15-online collection and ran replacements. Nothing from its local or external directories may be reused, changed, or deleted.

This gate does not establish wall performance and does not authorize production cutover, legacy comparison, qualification, gate revision, quality work, or deletion.

Required ancestry:

- trial source `e02f476`;
- partition target `369d447`;
- blocked-attempt archive `19ea6a1`;
- rejected-attempt verdict `bffa256`.

## Frozen artifacts

Normal profile:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_value`
SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`

Child SoA profile:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_child_soa`
SHA-256 `721394d9bec61fce9654266fa0fdc9b41d921d4bba1673b8b931fbc1fb0c9c91`

Normal partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition`
SHA-256 `5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1`

Child SoA partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition_child_soa`
SHA-256 `6b5b520fdf253def718b3743f8fd53d54eb7636329c83f9c39b2510a65fa582a`

Parameters:
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
SHA-256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

Prerequisite validation:
`/home/icly/Documents/tetris_ai_runner/results/phase7/child_soa_trial_2026-09-09/validation/prerequisite_tests_2026-09-09.txt`
SHA-256 `82a22a0cfd7afc288b2255007aca0c82bb3278b49544f9bbd0092e6e3ee797d0`

The validation transcript records 267 checks with zero failures, normal symbol isolation, GCC debug equivalence, GCC self-release equivalence, Clang self-release equivalence, both partition selftests, the five artifact hashes, and CPU 15 online after tests. No source, CMake, or binary change is permitted after that evidence.

## Frozen collector

Execute exactly once:

```text
bash /home/icly/Documents/tetris_ai_runner/results/phase7/child_soa_trial_2026-09-09/count_attempt3/collect.sh
```

Collector SHA-256: `b3e6e686bc190b7676a4bc0c536dbfa1686be57bc6c529f4b9ff6056f2ee314f`.

Verify and record the collector hash in `MANIFEST.txt`. No recorder or profile command may be invoked outside this collector. No command in the collector may be repeated. Do not prewarm.

The collector must begin from a clean tracked tree whose HEAD contains `bffa256`. It verifies all six hashes, CPU 7 governor `performance`, CPU 7 energy preference `performance`, global boost `1`, CPU 15 initially online, and noninteractive write authority for the CPU 15 control before creating evidence.

The five-minute load average must be below 1.5 before collection. The collector permits the initial read and at most ten one-minute rechecks. Failure is `BLOCKED` with no recorder run.

The collector takes CPU 15 offline once, verifies state `0`, and retains one restoration trap. It records load and CPU 15 state before the normal trace, SoA trace, and every count run. CPU 15 must remain `0` until all eight rows finish, then return to `1` on success and every exit path.

At runs 1 and 5, load5 at least 2.0 permits at most five one-minute rechecks and continuation only below 1.5. Load5 at least 2.0 during an already-started half is `ABORTED`. There is no replacement.

The collector and every generated file must be preserved on every terminal path. Never delete, truncate, rename over, or replace evidence. If an infrastructure error occurs after `TERMINAL=COLLECTED`, resume metadata finalization only and execute no recorder, profile, test, or prewarm command.

## Fresh paths

Local evidence:
`/home/icly/Documents/tetris_ai_runner/results/phase7/child_soa_trial_2026-09-09/count_attempt3/`

External partition evidence:
`/home/icly/Documents/tetris_ai_runner_results/phase7/child_soa_trial_2026-09-09/trace_attempt3/`

Before the collector, `collect.log`, the external directory, and all local `runN` stdout and stderr files must not exist. Their presence is `BLOCKED`; do not remove or overwrite them.

## Frozen collection

The collector runs each partition recorder once with seed 1, warmup 0, 20 moves, depth 6, 1000 iterations, the absolute parameter path, CPU 7 pinning, and `env -u TETRIS_AI_PARAM_FILE`.

Before ABBA, normal and SoA `.inputs.bin`, `.moves.tsv`, and `.run_totals.tsv` must be byte-identical. Both stderr files must be empty. The two stdout rows must match on every non-timing field except exact mapped retained memory. Any mismatch is `FAIL` and no count row may run.

The collector then runs exactly:

1. normal
2. SoA
3. SoA
4. normal
5. normal
6. SoA
7. SoA
8. normal

Each run uses seed 1, warmup 20, 80 moves, depth 6, 1000 iterations, quiet version 3, telemetry on, timers off, the absolute parameter path, CPU 7 pinning, and `env -u TETRIS_AI_PARAM_FILE`.

Exactly eight single-line `PROFILE_V3` stdout rows and eight empty stderr files are required. Any nonzero exit, malformed row, nonempty stderr, CPU-state failure, or mid-half load failure is `ABORTED`. No extra, replacement, or selectively retained row is permitted.

## Identity checks

Timing fields are `total_s`, every key ending `_ms`, every key ending `_ns`, and every key ending `_per_s`. They are excluded and cannot support a speed claim.

All non-timing fields must be identical within each engine across its four rows. Every non-timing field except `mem_retained_bytes` must be identical across all eight rows.

Required common values are:

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
- `budget_ms=0.000`
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

Required mapped memory values are normal `266338276`, SoA `266153956`, exact reduction `184320`, both below `268435456`, and SoA residual margin at least `65536`.

## Durable evidence

Preserve `collect.sh`, `collect.log`, every external trace file, all eight `runN` stdout files, and all eight stderr files. Add `MACHINE.md`, `MANIFEST.txt`, `commands.txt`, `binaries.sha256`, `rows.sha256`, `trace_summary.json`, and `summary.json` without changing raw files.

`commands.txt` must contain all ten executed commands in full with absolute artifact, parameter, and output paths. Shorthand is forbidden. `MACHINE.md` and `MANIFEST.txt` must reproduce every collector timestamp, load, CPU state, command, exit, terminal state, restoration, machine control, and anomaly. Hash every retained local row and every external file. Make external files read-only.

`summary.json` must list every check and observed value. Parent must independently run both hash manifests, compare all trace hashes and sizes, parse every field in all rows, verify CPU 15 online and no surviving process, and obtain a fresh read-only review before acceptance.

## Verdict

`PASS` requires collector `TERMINAL=COLLECTED`, exactly one two-trace and eight-row collection, every frozen control and hash, complete raw preservation, partition identity, deterministic repeats, cross-engine identity, exact expected counts, mapped memory, cap, margin, prerequisite transcript, parent reconciliation, and fresh review.

Any failure is terminal `BLOCKED`, `FAIL`, or `ABORTED` according to this runbook. Attempt 3 may never be retried or repaired with new collection commands.

Only accepted `PASS` permits freezing a telemetry-off normal-versus-SoA selector. Count evidence never permits a legacy run or wall-speed inference.
