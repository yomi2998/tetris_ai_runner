# Direct-view transposition key count gate

## Scope

This is one fresh execution of the mandatory semantic and work-vector gate for the trial-only direct-view transposition key mechanism.

Trial source commit: `e0da804`.

This gate does not establish wall performance and does not authorize production cutover, legacy comparison, qualification, gate revision, quality work, or deletion.

## Frozen artifacts

Normal profile:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_value`
SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`

Direct-key trial profile:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/tetris_profile_direct_key`
SHA-256 `420002a847f6fea6cb9e87b868f82ad1497bc0b12b7e817ebbe612c9dce6ee89`

Normal partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition`
SHA-256 `5986bdeebf9ff34fe4c3de0845881991fe463f6dc84dcbfb8d1b4bd7944642a1`

Direct-key partition recorder:
`/home/icly/Documents/tetris_ai_runner/out/build/linux-gcc-self-release/candidate_partition_direct_key`
SHA-256 `4642f85a76e6f04635cca482c9654f3e465d266ae6e43441d0c539776da83fd8`

Parameters:
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
SHA-256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

Prerequisite validation:
`/home/icly/Documents/tetris_ai_runner/results/phase7/direct_key_trial_2026-09-09/validation/prerequisite_tests_2026-09-09.txt`
SHA-256 `5c40bf19b78b435396719857c24a471310618c52b87281414a7eadc4cc04a27f`

The validation transcript records the 75722-check trial suite with zero failures under GCC debug, GCC self-release, Clang self-release, and the sanitizer build, all exclusion compile tests, normal symbol isolation with zero `direct_key` symbols, the profile equivalence test, both partition selftests, the five artifact hashes, and CPU 15 online after tests. No source, CMake, or binary change is permitted after that evidence.

## Frozen collector

Collector SHA-256: `86d99434a7fadcf62061238844c93346fc37de0f9db8158ca8a47a742b0a38c3`.

Verify and record the collector hash in `MANIFEST.txt` at execution. Execute exactly once:

```text
bash /home/icly/Documents/tetris_ai_runner/results/phase7/direct_key_trial_2026-09-09/count/collect.sh
```

No recorder or profile command may be invoked outside this collector. No command in the collector may be repeated. Do not prewarm.

Before execution, `collect.log`, the external trace directory, and all local `runN` stdout and stderr files must not exist. Their presence is `BLOCKED`; do not remove or overwrite them.

The collector must begin from a clean tracked tree whose HEAD contains `e0da804`. It verifies all six hashes, CPU 7 governor `performance`, CPU 7 energy performance preference `performance`, global boost `1`, CPU 15 initially online, and noninteractive write authority for the CPU 15 control before creating evidence.

The five-minute load average must be below 1.5 before collection. The collector permits the initial read and at most ten one-minute rechecks. Failure is `BLOCKED` with no recorder run.

The collector takes CPU 15 offline once, verifies state `0`, and retains one restoration trap. It records load and CPU 15 state before the normal trace, the trial trace, and every count run. CPU 15 must remain `0` until all eight rows finish, then return to `1` on success and every exit path.

At runs 1 and 5, load5 at least 2.0 permits at most five one-minute rechecks and continuation only after load5 falls below 1.5. Load5 at least 2.0 immediately before any run is `ABORTED`. There is no replacement.

The collector and every generated file must be preserved on every terminal path. Never delete, truncate, rename over, or replace evidence. If an infrastructure error occurs after `TERMINAL=COLLECTED`, resume metadata finalization only and execute no recorder, profile, test, or prewarm command.

## Fresh paths

Local evidence:
`/home/icly/Documents/tetris_ai_runner/results/phase7/direct_key_trial_2026-09-09/count/`

External partition evidence:
`/home/icly/Documents/tetris_ai_runner_results/phase7/direct_key_trial_2026-09-09/trace/`

## Frozen collection

The collector runs each partition recorder once with seed 1, warmup 0, 20 moves, depth 6, 1000 iterations, the absolute parameter path, CPU 7 pinning, and `env -u TETRIS_AI_PARAM_FILE`.

Before ABBA, normal and trial `.inputs.bin`, `.moves.tsv`, and `.run_totals.tsv` must be byte-identical. Both stderr files must be empty. The two stdout rows must match on every non-timing field including exact equal `mem_retained_bytes`. Any mismatch is `FAIL` and no count row may run.

The collector then runs exactly:

1. normal
2. direct-key
3. direct-key
4. normal
5. normal
6. direct-key
7. direct-key
8. normal

Each run uses seed 1, warmup 20, 80 moves, depth 6, 1000 iterations, quiet version 3, telemetry on, timers off, the absolute parameter path, CPU 7 pinning, and `env -u TETRIS_AI_PARAM_FILE`.

Exactly eight single-line `PROFILE_V3` stdout rows and eight empty stderr files are required. Any nonzero exit, malformed row, nonempty stderr, CPU-state failure, or mid-half load failure is `ABORTED`. No extra, replacement, or selectively retained row is permitted.

## Identity checks

Timing fields are `total_s`, every key ending `_ms`, every key ending `_ns`, and every key ending `_per_s`. They are excluded and cannot support a speed claim.

All non-timing fields must be identical within each engine across its four rows. Every non-timing field including `mem_retained_bytes` must be identical across all eight rows, because the direct-key trial adds no storage.

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
- `path_calls=80`
- `path_states=76513`
- `replay_failures=0`
- `arena_reserved_bytes=208720320`
- `idmap_reserved_bytes=4348340`
- `raw_unique_ratio_x1000=2020`
- `timers=off`
- `mem_retained_bytes=266338276` on all eight rows

## Durable evidence

Preserve `collect.sh`, `collect.log`, every external trace file, all eight `runN` stdout files, and all eight stderr files. Add `MACHINE.md`, `MANIFEST.txt`, `commands.txt`, `binaries.sha256`, `rows.sha256`, `trace_summary.json`, and `summary.json` without changing raw files.

`commands.txt` must contain all ten executed commands in full with absolute artifact, parameter, and output paths. Shorthand is forbidden. `MACHINE.md` and `MANIFEST.txt` must reproduce every collector timestamp, load, CPU state, command, exit, terminal state, restoration, machine control, and anomaly. Hash every retained local row and every external file. Make external files read-only.

`summary.json` must list every check and observed value. Parent must independently run both hash manifests, compare all trace hashes and sizes, parse every field in all rows, verify CPU 15 online and no surviving process, and obtain a fresh read-only review before acceptance.

## Verdict

`PASS` requires collector `TERMINAL=COLLECTED`, exactly one two-trace and eight-row collection, every frozen control and hash, complete raw preservation, partition identity, deterministic repeats, full cross-engine identity including memory, exact expected counts, prerequisite transcript, parent reconciliation, and fresh review.

Any failure is terminal `BLOCKED`, `FAIL`, or `ABORTED` according to this runbook. This gate may never be retried or repaired with new collection commands.

Only accepted `PASS` permits freezing a telemetry-off normal-versus-direct-key selector. Count evidence never permits a legacy run or wall-speed inference.
