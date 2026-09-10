# Step 1 machine record: stage-cost table

Classification: `DIAGNOSTIC NON-BINDING`. Step 1 has no pass or fail threshold. It produces the cost budget used to interpret the Step 3 binding block. No engine speed claim and no end-to-end inference is made here, and none may be drawn from these rows.

## Provenance

- Execution HEAD: `fd614b97b113381e363008d0f46d263cf67f8fd1`, tracked tree clean, nothing staged.
- Required ancestry present: `578db3b` frozen runbook, `fd614b9` diagnostic implementation.
- Runbook: `results/phase7/legacy_host_diagnostic_2026-09-10/RUNBOOK.md` sections 6 and 6.3.
- Binary: `out/build/linux-gcc-self-release/legacy_stage_bench`.
- Binary SHA-256: `d59989c0adfaacc764a1adf9074f543fd1eaa548ad8e84fdbf8fc827ce03a699`, verified before and after the single invocation.
- Normal value candidate unchanged at `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`, with zero `legacy_host_diag` symbols by `nm -C`.
- Build: preset `build-linux-gcc-self-release`, g++ 16.2.1 20260810, kernel 7.2.3-1-cachyos, CPU AMD Ryzen 7 7700 8-Core Processor.

## Invocation

Executed exactly once, never twice, with the frozen default batch counts (no `--drives`, `--warmup`, or `--timed` override).

```text
taskset --cpu-list 7 out/build/linux-gcc-self-release/legacy_stage_bench --out results/phase7/legacy_host_diagnostic_2026-09-10/step1 --corpus both --inputs /home/icly/Documents/tetris_ai_runner_results/phase7/row_export_fusion_trial_2026-09-09/trace/trace_normal.inputs.bin
```

- Start `2026-09-10T05:18:38Z`, end `2026-09-10T05:19:14Z`, exit 0, wall 36 seconds.
- Load before: `1.21 1.39 1.72`, five minute value 1.39, below the 1.5 gate, so no recheck wait was needed.
- Load after: `2.35 1.65 1.79`, which is this bench's own contribution to load1 and is recorded for transparency.
- CPU 15 was online before and after and stayed online throughout. The frozen continuous offline window requirement applies to the Step 3 binding block; this step is a non-binding microbenchmark.
- `stderr.log` is empty, 0 bytes. `stdout.log` is the unedited collection output.
- No process survived the run.

## Controls

- CPU 7 pinned, governor `performance`, energy performance preference `performance`, global boost `1` unchanged.
- Corpora: C1 the 33-board legacy-compatible subcorpus from `tests/reach_corpus.h`, C2 the frozen parent-input replay corpus.
- C2 inputs verified before use: `/home/icly/Documents/tetris_ai_runner_results/phase7/row_export_fusion_trial_2026-09-09/trace/trace_normal.inputs.bin`, 64365916 bytes, SHA-256 `c6b5ecf6c2b7e956b7770ea957820d77fefc4fcaad0a9e1be22a55d1d1473da5`, read-only, the corpus accepted as semantic evidence at `2c75c94`.
- Sidecars unchanged: `trace_normal.moves.tsv` `723c2c83ef6de336ef8750d76fec7198cdea2b94545f8d10ad4badfc4d7c674b`, `trace_normal.run_totals.tsv` `46934e5f13682b02e1df729c26c86d0f2c74f599f4e6136b3610ce5b3c374785`.

## Hoisting and digest controls

- Poisoned-board check passed: a fully occupied board yields zero landings.
- Nonzero sink enforced per cell; a zero sink is a hard failure and none occurred.
- Every drive rotates to the next case, so a compiler cannot hoist a single repeated call.
- Ordered candidate digests are printed per cell and carried into `stage_table.tsv`.

## Stage scope definitions

- S1: prepacked `binary_bfs` on the value board occupancy, spawn start, results reduced by popcount into a sink. Boards prepared outside the span.
- S1x: the preparation-only pass reported separately so nothing is hidden in S1 and nothing is double counted.
- S2: workspace construction plus arrival-aware search plus landing bit extraction, matching `src/arrival_candidates.cpp` scope, without candidate normalization.
- S3: complete production candidate enumeration through `tetris::toj::enumerate_candidates_into`, including ordered canonicalization and deduplication. This is the scope behind the recorded wrapper figure.
- S4: legacy `search_tspin::Search::search` with the `TetrisMap` import and metadata rebuild prepared outside the timed span.
- S4i: the same legacy search with map rebuild inside the span, import-inclusive. The recorded gate89 legacy timings correspond to this scope, not S4.
- S5: production enumeration plus coordinate conversion through `ExternalPoseTransform::to_legacy` plus pose-node resolution through `TetrisContext::get`, counting only mapped poses.

## Disclosed limitations from review

- The runbook 6.3 sink-equality clause is not literally implemented. There is no untimed-digest-equals-timed-sink assertion. Per-drive case rotation makes hoisting structurally impossible, and the nonzero sink plus poisoned-board checks remain. Accepted with this disclosure.
- Board-class medians are not produced. The bench emits per-piece cells only and voids the unmappable and spawn-blocked counters, so class counts are reported at summary level in `summary.json` instead of as per-class medians. Class (d) blocked spawn is not counted at all in this collection.
- S5 silently skips poses that `to_legacy` cannot represent rather than counting them, so the unmappable-pose subline required by runbook 6.2 is absent. For C2 this cannot hide an upper-row effect because zero records have occupancy at or above row 40, but it does mean the unmappable-pose count is unmeasured rather than zero. This must not be read as proof that all candidates map.
- Every row printed by the bench carries the `DIAGNOSTIC NON-BINDING` label, and the labels in `stage_table.tsv` and `summary.json` inherit it.
