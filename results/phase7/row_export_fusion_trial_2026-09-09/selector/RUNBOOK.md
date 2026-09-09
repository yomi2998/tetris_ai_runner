# Row export fusion telemetry-off selector

## Scope

This is one balanced, non-qualification telemetry-off selector of the count-gated row export fusion trial against the unchanged normal candidate.

Accepted semantic and count gate: `2c75c94` and `results/phase7/row_export_fusion_trial_2026-09-09/count/PARENT_VERDICT.md`.

This selector does not use the legacy baseline and does not authorize production cutover, qualification, quality work, gate revision, or deletion.

## Frozen artifacts

Normal:
`/home/icly/Documents/tetris_ai_runner_results/phase7/row_export_fusion_trial_2026-09-09/selector_artifacts/tetris_profile_normal`
SHA-256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`

Row fusion trial:
`/home/icly/Documents/tetris_ai_runner_results/phase7/row_export_fusion_trial_2026-09-09/selector_artifacts/tetris_profile_row_fusion`
SHA-256 `ce2bd5a65967772bdbb89ac25b94a009c4d189cb1396a16a38ce96c27594ee7c`

Parameters:
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
SHA-256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

Accepted count verdict:
`/home/icly/Documents/tetris_ai_runner/results/phase7/row_export_fusion_trial_2026-09-09/count/PARENT_VERDICT.md`
SHA-256 `6211dd00e6b554a3338520fb4285a2c4d4c098a4db81131d11a03da3bc790a19`

Both executable artifacts have read-only executable mode `555`. Verify every hash and mode before collection. Any mismatch is `BLOCKED`.

## Frozen collector

Collector SHA-256: `02273adaa2a1c5b957051d3efb96707685f1a9e923afbb9bf4a90d4bb78cea04`.

Verify and record the collector hash in `MANIFEST.txt` at execution. Execute exactly once:

```text
bash /home/icly/Documents/tetris_ai_runner/results/phase7/row_export_fusion_trial_2026-09-09/selector/collect.sh
```

No profile or prewarm command may be invoked outside the collector. Never invoke the collector twice, including after an error or provider failure.

Before execution, `collect.log`, `rows.txt`, and all local `runN` stdout and stderr files must not exist. Their presence is `BLOCKED`; do not remove or overwrite them.

The collector must begin from a clean tracked tree whose HEAD contains `2c75c94`. It verifies all four frozen hashes, artifact modes, CPU 7 governor `performance`, CPU 7 energy performance preference `performance`, global boost `1`, CPU 15 initially online, and noninteractive CPU-control authority.

The five-minute load average must be below 1.5 before collection. The initial read and at most ten one-minute rechecks are allowed. Failure is `BLOCKED` with no prewarm or timed run.

The collector takes CPU 15 offline once before prewarm and keeps it continuously offline through both prewarm commands and all eight timed runs. It records load and CPU state before and CPU state after every command. CPU 15 must read `0` at every boundary and return to `1` on success and every exit path.

The collector and every generated file must be preserved on every terminal path. Never delete, truncate, replace, or selectively retain a row. If an infrastructure failure occurs after `TERMINAL=COLLECTED`, resume metadata finalization only and execute no profile, prewarm, or replacement command.

## Label mapping disclosure

The collector retains template labels that spell the trial variant `soa` in log labels and filenames such as `prewarm_soa` and `run2_soa`. Every such command provably invoked the hash-verified row fusion artifact. Metadata must map these labels to variant `row-fusion` explicitly, and no raw file may be renamed.

## Prewarm

Run normal then row fusion once each. Discard both stdout and stderr. Record full commands, timestamps, loads, CPU states, and exits.

Each prewarm command uses:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 200 --quiet --quiet-version 3 --telemetry off --timers off
```

Prewarm output never enters timed evidence. A nonzero exit or control failure is `ABORTED` with no replacement.

## Timed commands and order

Every timed command uses:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version 3 --telemetry off --timers off
```

Run exactly four pairs:

1. normal then row fusion;
2. row fusion then normal;
3. row fusion then normal;
4. normal then row fusion.

Start the second run of each pair immediately after the first, apart from the required load and CPU-state read.

Before each pair, load5 at least 2.0 permits at most five one-minute rechecks and continuation only after load5 falls below 1.5. Load5 at least 2.0 immediately before any run is `ABORTED`. Any nonzero exit, malformed row, nonempty stderr, or CPU-state failure is `ABORTED`. Preserve partial evidence and stop. No extra or replacement pair is permitted.

## Raw evidence

Preserve `collect.sh`, `collect.log`, all eight single-line `PROFILE_V3` stdout files, and all eight empty stderr files. After collection, concatenate the eight stdout files in execution order into `rows.txt` without changing them.

Add `MACHINE.md`, `MANIFEST.txt`, `commands.txt`, `binaries.sha256`, `rows.sha256`, `summary.json`, and `verdict.md`. `commands.txt` must contain all ten commands in full with absolute paths and no shorthand. `MANIFEST.txt` must map every prewarm and timed command to its variant, pair and position where applicable, artifact path and hash, full command, UTC start, exit, load1, load5, and controls, with the label mapping applied.

Record the clean HEAD, ancestry, machine details, frozen hashes, artifact modes, precheck, all timestamps and loads, one continuous CPU 15 window, restoration, exits, exact command count, and anomalies. Parent must independently verify manifests, rows, calculations, CPU 15 online, no surviving process, and clean tracked state before acceptance.

## Row eligibility

Each timed row must have exactly one `PROFILE_V3` line and these exact workload fields:

- `moves=200`
- `warmup_moves=20`
- `seed=1`
- `iters=1000`
- `maxdepth=6`
- `budget_ms=0.000`
- `mode=iters`
- `telemetry=off`
- `dead_moves=0`
- `games=0`

No telemetry counter is used for selector calculation. Count identity was established separately at `2c75c94`.

## Selector calculation

For each pair calculate row fusion divided by normal for `total_s` and `p95_ms`, regardless of execution order. Keep full decimal precision for threshold comparisons. Display ratios to at least five decimal places.

For four ratios, sort ascending and calculate the median as the arithmetic mean of the two middle values. Spread is maximum minus minimum. Do not append pairs, replace a row, or select a favorable subset.

Classify exactly once:

- `PRIMARY-SELECT-ROW-FUSION`: both exact medians are at most `0.90` and both exact spreads are at most `0.04`.
- `FALLBACK-OWNER-REVIEW`: primary does not pass, both exact medians are at most `0.95`, and both exact spreads are at most `0.04`.
- `NO-ADVANCE`: neither prior completed classification applies.
- `BLOCKED` or `ABORTED`: only under the collection conditions above.

Write `verdict.md` with every raw input, all four paired ratios, exact and displayed medians and spreads, every threshold check, controls, anomalies, and one terminal classification.

## Advancement boundary

`PRIMARY-SELECT-ROW-FUSION` establishes a clear current-candidate win and permits only freezing a fresh controlled row-fusion-versus-legacy protocol. It does not itself authorize a legacy execution or any qualification or production change.

`FALLBACK-OWNER-REVIEW` stops for the owner's explicit fallback decision before any legacy evidence is consumed.

`NO-ADVANCE`, `BLOCKED`, or `ABORTED` stops this trial. No classification permits appending selector pairs or inferring performance from the earlier telemetry-on count rows.
