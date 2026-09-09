# Parent verdict: NO-ADVANCE

The row export fusion telemetry-off selector is accepted and, per the owner's sequential-attempts directive, closes the executed optimization program.

## Validity

- Execution HEAD was `6dd157aa9bf78e22e7af69e3c3280cc31a7df83d` with a clean tracked tree.
- Collector SHA-256 was `02273adaa2a1c5b957051d3efb96707685f1a9e923afbb9bf4a90d4bb78cea04`.
- The collector ran exactly once with two prewarm and eight timed commands, all exit zero, zero reruns.
- Timed order was normal/fusion, fusion/normal, fusion/normal, normal/fusion with telemetry and timers off, 200 moves, seed 1, depth 6, 1000 iterations.
- One continuous CPU 15 offline window covered `2026-09-09T19:42:51Z` to `19:45:33Z` with state `0` at every boundary and restoration to `1`.
- Precheck load5 was `0.93`; pair-gate load5 values `0.93`, `0.98`, `1.14`, `1.21`.
- The template `soa` labels map to the row fusion variant in all metadata, verified against the hash-verified commands.

## Result

Row fusion divided by normal ratios:

- pair 1: total `0.98896`, p95 `0.98763`;
- pair 2: total `0.99185`, p95 `0.99604`;
- pair 3: total `0.98933`, p95 `0.98859`;
- pair 4: total `0.98431`, p95 `0.98397`.

Summaries at parent-reproduced precision: total median `0.98915`, spread `0.00754`; p95 median `0.98811`, spread `0.01207`.

Both spreads pass, but both medians exceed the `0.95` fallback limit and the `0.90` primary target. The only valid classification is `NO-ADVANCE`.

## Interpretation

Unlike the index, staging, and direct-key trials, the fusion trial is faster than the normal candidate in all four pairs on both metrics, about 1.09 percent total and 1.19 percent at p95, matching the design lane's predicted 0.76 to 1.17 percent removable-cost band. The mechanism works exactly as modeled; the removable work was simply too small to matter against a roughly 4 percent gap to the frozen legacy baseline.

## Independent validation

The parent independently verified all hashes and modes, command count and order, row eligibility, the exact `rows.txt` mapping, the Decimal arithmetic and classification at 60-digit precision, controls, CPU 15 online, zero surviving processes, and clean tracked state.

Read-only review run `9880d983-1ba0-4ac8-8801-7db7f96765e5` found no P0, P1, or P2 issue and returned `ACCEPT`.

## Boundary

No legacy evidence is consumed. No qualification, cutover, quality campaign, gate revision, or deletion is authorized. The count gate at `2c75c94` remains semantic evidence only.

Per the owner directive, the executed optimization program ends here and the performance gate is reported as still failing.
