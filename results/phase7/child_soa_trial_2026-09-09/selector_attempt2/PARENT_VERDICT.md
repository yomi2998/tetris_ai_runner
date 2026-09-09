# Parent verdict: NO-ADVANCE

The fresh Child SoA telemetry-off selector is accepted and stops the Child SoA line.

## Validity

- Execution HEAD was `5c6740f695dbe7c91d7eefe4db27afdf4e8f50f4` with a clean tracked tree.
- Collector SHA-256 was `6527addc46a3ebd84b0c64196d040e9251363145cbf6509f2b0a5262d8850740`.
- Fresh normal and SoA artifacts had mode `555`, were executable, and matched the frozen hashes.
- The collector was invoked once and logged exactly two prewarm and eight timed commands, all with exit zero.
- The timed order was normal/SoA, SoA/normal, SoA/normal, normal/SoA.
- Every timed row used seed 1, warmup 20, 200 moves, depth 6, 1000 iterations, telemetry off, and timers off.
- All eight stdout files contained one eligible `PROFILE_V3` row, all stderr files were empty, and `rows.txt` was their exact execution-order concatenation.
- CPU 15 remained offline from `2026-09-09T15:52:49Z` through `2026-09-09T15:55:43Z`, read `0` at every command boundary, and was restored to `1`.
- Precheck load5 was `0.67`; pair-gate load5 values were `0.69`, `0.82`, `0.94`, and `1.09`.
- No command, pair, or row was added, repeated, replaced, or selectively retained.

## Result

SoA divided by normal ratios were:

- pair 1: total `1.118860244233378561736770692`, p95 `1.131178929765886287625418060`;
- pair 2: total `1.129280450801907238838318162`, p95 `1.123637452087369357534235186`;
- pair 3: total `1.097361237488626023657870792`, p95 `1.064981539335416074978699233`;
- pair 4: total `1.096128452965721227486399408`, p95 `1.091713687844516638576092886`.

Exact four-pair summaries were:

- total median `1.108110740861002292697320742`;
- total spread `0.033151997836186011351918754`;
- p95 median `1.107675569965942998055164036`;
- p95 spread `0.066197390430470212646718827`.

Both medians exceed the `0.90` primary and `0.95` fallback limits. The p95 spread also exceeds `0.04`. The only valid classification is `NO-ADVANCE`.

## Independent validation

The parent independently verified the collector and artifact hashes, modes, command count and order, row hashes, exact `rows.txt` mapping, row eligibility, CPU and load controls, all Decimal ratios and summaries, terminal classification, CPU 15 restoration, zero surviving processes, and clean tracked state.

Read-only review run `f56c9336-bef5-46a5-a6fd-c6f4b328ab8a` found no P0, P1, or P2 issue and accepted `NO-ADVANCE`.

## Boundary

No legacy evidence may be consumed. No qualification, production cutover, quality campaign, gate revision, appended selector pair, or trial deletion is authorized.

The accepted count gate at `b1ab3bc` remains semantic evidence only. Direct-view transposition keys remain a separate, uncombined fallback that requires its own explicit gate before work begins.
