# Three-pair exact evaluation-index comparison against legacy

## Scope

This is one non-qualification, telemetry-off, three-pair comparison of selected variant B against the frozen pre-migration legacy baseline. It measures whether the exact evaluation index reaches the owner's engineering target. It does not authorize production cutover, a full qualification campaign, gate revision, quality work, or legacy deletion.

Selected candidate source: `a716223`.
Selector verdict: `d60c5bd` and `results/phase7/eval_index_trial_2026-09-09/selector/verdict.md` (`SELECT-B`).

## Frozen artifacts

Legacy baseline:
`/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`
SHA-256: `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`

Selected B candidate:
`/home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_b`
SHA-256: `e5c55131f9313ee9f7d5a1f3a508fc2efbbddfe225be56076f84b6ec095c41ce`

Parameters:
`/home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin`
SHA-256: `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

All artifacts are read-only. Verify every hash before prewarm. Any mismatch blocks collection.

## Preconditions and machine controls

- Tracked tree clean at the execution HEAD, whose ancestry contains `a716223`, `7f94be7`, and `d60c5bd`.
- CPU 7 governor `performance`.
- CPU 7 energy performance preference `performance`.
- Global boost `1`, unchanged.
- Five-minute load average below 1.5 before prewarm. Recheck once per minute for at most 10 minutes if needed. If it never falls below 1.5, write `BLOCKED` evidence and run nothing.
- Take CPU 15 offline once before prewarm. Keep it continuously offline through both prewarm runs and all six timed runs. Restore it online after the final run and on every abort path.

Record controls, exact UTC timestamps, all load samples, hashes, HEAD, source commits, kernel, compiler, CPU model, and the continuous CPU 15 offline window in `MACHINE.md`.

## Prewarm

Run legacy then B once, with output discarded:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 200 --quiet --quiet-version <version> --telemetry off
```

Use quiet version 2 for legacy and 3 for B. Record timestamps and exit status only. Prewarm output never enters timed evidence.

## Timed command and order

Every timed run uses:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version <version> --telemetry off
```

Run exactly three pairs:

1. Legacy then B.
2. B then legacy.
3. Legacy then B.

Start the second run of each pair immediately after the first. Read load1/load5 before every run. If load5 is at least 2.0 before a not-yet-started pair, recheck once per minute for at most 5 minutes and resume only after it falls below 1.5. Otherwise abort the whole block.

Any nonzero run exit aborts the block. Preserve partial rows and manifest, mark `ABORTED`, run no replacement or extra pair, and restore CPU 15.

## Raw evidence

`rows.txt` contains exactly six unedited output rows in execution order. `MANIFEST.txt` maps each row to pair position, artifact path/hash, full absolute command, UTC start, exit, controls, load1, and load5. No prewarm output or aggregation enters `rows.txt`.

## Calculation

For each pair calculate B divided by legacy for `total_s` and `p95_ms`, regardless of execution order. Report all three ratios. The median is the middle sorted value. Spread is maximum minus minimum.

Classify exactly once:

- `PRIMARY-SCREEN-PASS`: both medians at most 0.90 and both spreads at most 0.04.
- `FALLBACK-REVIEW`: primary does not pass, both medians at most 0.95, and both spreads at most 0.04.
- `ENGINEERING-TARGET-FAIL`: neither prior class applies, but both medians are at most 1.02 and both spreads are at most 0.04.
- `NO-ADVANCE`: any median exceeds 1.02 or either spread exceeds 0.04.
- `BLOCKED` or `ABORTED` only under the conditions above.

Write `verdict.md` with raw inputs, all ratios, medians, spreads, controls, anomalies, and exactly one terminal classification. Do not append pairs or select a favorable subset.

## Advancement boundary

Even `PRIMARY-SCREEN-PASS` is not migration qualification because this block has only three pairs. It permits only an owner decision about a fresh full five-pair qualification campaign using the unchanged 1.02 migration bars. `FALLBACK-REVIEW` stops for the owner's explicit fallback decision. Every other completed verdict stops the index trial. No production behavior changes automatically.
