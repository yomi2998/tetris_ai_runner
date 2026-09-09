# Non-binding evaluation-index variant selector

## Scope

This selector chooses at most one count-gated exact evaluation-index variant for the separately authorized three-pair comparison against the frozen legacy baseline. It is not migration qualification and makes no candidate-versus-legacy performance claim.

Source commit: `a716223`.

Variant A: 262144 direct 4-byte NodeId slots, fmix-finalized occupancy fingerprint, exact arena Board verification.

Variant B: 65536 direct 8-byte tag-plus-NodeId slots, fmix-finalized occupancy fingerprint, exact arena Board verification.

## Frozen artifacts

Create these new dated artifacts without modifying any existing artifact:

- `/home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_a`
- `/home/icly/Documents/tetris_ai_runner_results/phase7/eval_index_trial_2026-09-09/tetris_profile_index_b`

Required hashes:

- A: `35b6c5bd095ab02450de9004cc44e066a2e7f26d5af2eaa010dd2a6f5ab698f2`
- B: `e5c55131f9313ee9f7d5a1f3a508fc2efbbddfe225be56076f84b6ec095c41ce`
- Parameters: `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`

Copy the built binaries once, verify hashes, make the new copies read-only, and execute only the copies. Any mismatch blocks collection.

## Preconditions

- Tracked tree clean at the execution HEAD, with source ancestry containing `a716223`.
- CPU 7 governor `performance`.
- CPU 7 energy performance preference `performance`.
- Global boost `1`, unchanged.
- Five-minute load average below 1.5 before prewarm. If it is at least 1.5, recheck once per minute for at most 10 minutes. If it remains at least 1.5, record `BLOCKED` and run nothing.
- CPU 15 is the SMT sibling of CPU 7. Take CPU 15 offline before prewarm and restore it online on success and every abort path.

Record all controls, timestamps, load samples, hashes, source commit, execution HEAD, kernel, compiler, and CPU model in `MACHINE.md`.

## Prewarm

After CPU 15 is offline, run A then B once with outputs discarded:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 40 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 200 --quiet --quiet-version 3 --telemetry off
```

Record only timestamps and successful exit status. Prewarm rows never enter timed evidence.

## Timed command

Every timed run uses:

```text
env -u TETRIS_AI_PARAM_FILE taskset --cpu-list 7 <artifact> --seed 1 --warmup-moves 20 --moves 200 --maxdepth 6 --param-file /home/icly/Documents/tetris_ai_runner/artifacts/frozen_29d.bin --iters 1000 --quiet --quiet-version 3 --telemetry off
```

Run exactly four pairs in this order:

1. A then B.
2. B then A.
3. B then A.
4. A then B.

Start the second run of each pair immediately after the first. Record the load average before every run. If the five-minute load is at least 2.0 before a not-yet-started pair, recheck once per minute for at most 5 minutes. Resume only if it falls below 1.5; otherwise abort the entire block.

Any nonzero exit aborts the block. Preserve partial evidence, mark it `ABORTED`, run no replacement pair in this block, and restore CPU 15.

## Raw evidence

`rows.txt` contains exactly eight unedited PROFILE_V3 rows in execution order. `MANIFEST.txt` maps each row to pair/position, artifact path and hash, full command, UTC start, exit status, load1, load5, and machine controls. No aggregation occurs during collection.

## Selector calculation

For each pair calculate B divided by A for `total_s` and `p95_ms`, regardless of execution order. Report all four ratios. Median for four values is the arithmetic mean of the two middle sorted values. Spread is maximum minus minimum.

Select B only if all conditions hold:

- Median B/A total ratio at most 0.99.
- Median B/A p95 ratio at most 1.02.
- Total ratio spread at most 0.04.
- p95 ratio spread at most 0.04.

Select A only if all conditions hold after taking reciprocal orientation A/B:

- Median A/B total ratio at most 0.99.
- Median A/B p95 ratio at most 1.02.
- Total ratio spread at most 0.04.
- p95 ratio spread at most 0.04.

Otherwise the verdict is `NO-SELECTION`. Do not append pairs or choose from a favorable subset.

Write `verdict.md` with raw inputs, all ratios, medians, spreads, anomalies, controls, and exactly one terminal verdict: `SELECT-A`, `SELECT-B`, `NO-SELECTION`, `ABORTED`, or `BLOCKED`.

## Advancement boundary

A selector winner is not qualified. It may enter one fresh three-pair telemetry-off comparison against the frozen pre-migration legacy baseline. That comparison uses the owner's primary 0.90 total and p95 target. A result in the 0.90 to 0.95 band stops for owner review of the fallback; it does not advance automatically. No production cutover, full qualification, quality campaign, gate revision, or legacy deletion is authorized.
