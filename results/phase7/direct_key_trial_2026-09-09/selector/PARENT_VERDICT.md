# Parent verdict: NO-ADVANCE

The direct-view transposition key telemetry-off selector is accepted and stops this trial.

## Validity

- Execution HEAD was `7b67fc910859ea6af8c34b48e94b2c95ee657102` with a clean tracked tree.
- Collector SHA-256 was `7a436c3a3d8d474d83c2957943727b5842c4cbeef0109c559f012ebf2ac901f5`.
- The collector ran exactly once with two prewarm and eight timed commands, all exit zero, zero reruns.
- Timed order was normal/direct-key, direct-key/normal, direct-key/normal, normal/direct-key with telemetry and timers off, 200 moves, seed 1, depth 6, 1000 iterations.
- One continuous CPU 15 offline window covered `2026-09-09T17:40:49Z` to `17:43:44Z` with state `0` at every boundary and restoration to `1`.
- Precheck load5 was `0.73`; pair-gate load5 values `0.73`, `0.93`, `1.01`, `1.07`.
- The template `soa` labels map to the direct-key variant in all metadata, verified against the hash-verified commands.

## Result

Direct-key divided by normal ratios:

- pair 1: total `1.127338206837239303375618147`, p95 `1.133854680109018912627471772`;
- pair 2: total `1.084047221061429168407856247`, p95 `1.079393427200102052206081674`;
- pair 3: total `1.077215390299255818863144561`, p95 `1.083889364589560075609480266`;
- pair 4: total `1.077219045583284347779618447`, p95 `1.070650745680310097891071546`.

Exact summaries: total median `1.080633133322356758093737347`, spread `0.050122816537983484512473586`; p95 median `1.081641395894831063907780970`, spread `0.063203934428708814736400226`.

Both medians exceed the `0.90` primary and `0.95` fallback limits, and both spreads exceed `0.04`. The only valid classification is `NO-ADVANCE`. The direct-key trial is about 8.1 percent slower than the normal candidate on both metrics.

## Independent validation

The parent independently verified all hashes and modes, command count and order, row eligibility, the exact `rows.txt` mapping, the Decimal arithmetic and classification, controls, CPU 15 online, zero surviving processes, and clean tracked state.

Read-only review run `be9f0dc4-e85b-4030-a2a2-a51e7eb51ffc` found no P0, P1, or P2 issue and returned `ACCEPT`.

## Boundary

No legacy evidence is consumed. No qualification, cutover, quality campaign, gate revision, or deletion is authorized. The count gate at `368a332` remains semantic evidence only.

Per the owner's sequential-attempts directive, the next and final remaining mechanism is evaluate-and-safe row export fusion, which requires its own design, count gate, and selector under the same fail-closed discipline.
