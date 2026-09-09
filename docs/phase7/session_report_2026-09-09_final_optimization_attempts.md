# Session report, 2026-09-09: final optimization attempts

## Authorization and directive

The owner authorized executing the two remaining ranked optimization attempts sequentially: direct-view transposition keys first, then evaluate-and-safe row export fusion if the first failed. The directive: stop and report if the performance gate still fails after all remaining attempts; continue with the migration plan if one succeeds.

## Attempt 1: direct-view transposition keys

Architecture verdict experiment 4. The trial replaced the two 152-byte `TranspositionKey` materializations per candidate with direct-view fingerprint hashing over the staged child and a once-per-source context, and direct field-by-field equality against the stored arena node. Fingerprints were bit-identical by construction, and merge decisions, insertion order, and all work vectors were unchanged.

- Implementation committed at `e0da804` after a 75722-check suite, three-compiler and sanitizer coverage, and an ACCEPT review.
- Count gate accepted at `368a332`: byte-identical partition traces, all 37 frozen counts exact, `mem_retained_bytes=266338276` equal on every row, one continuous CPU 15 offline window, zero reruns.
- Selector accepted at `0b30dd8`: `NO-ADVANCE`. The trial was about 8.1 percent slower than the normal candidate on both total (median 1.08063) and p95 (median 1.08164), with both spreads above 0.04.

The removed key construction was real, but the added context machinery, probe mirror, and codegen changes cost more than the two stack key builds they replaced.

## Attempt 2: evaluate-and-safe row export fusion

Architecture verdict experiment 5. The trial fused the safe-margin computation into `Policy::evaluate`, exporting clean rows once per computed board and computing the safe margin strictly before `init_t_value`'s overlay mutation, with bit-identical results and no persistent storage.

- Implementation committed at `abf870d` after a 396598-check suite including the overlay hazard witness, bit-cast evaluation identity, and export-event accounting, with ACCEPT review.
- Count gate accepted at `2c75c94`: byte-identical partition traces, all 37 frozen counts exact, equal retained memory, clean controls.
- Selector accepted at `1e71abf`: `NO-ADVANCE`. Unlike every prior trial, the fusion was faster than the normal candidate in all four pairs on both metrics: total median 0.98915 (1.09 percent faster), p95 median 0.98811 (1.19 percent faster), spreads 0.00754 and 0.01207.

The result confirms the design lane's prediction precisely: the removable double-export cost was 0.76 to 1.17 percent, the measured win was 1.09 to 1.19 percent, and a roughly 1 percent gain cannot close a roughly 4 percent gap to the frozen legacy baseline. Both medians miss the 0.95 fallback band, so no fallback review is implicated.

## Terminal report: the performance gate still fails

All four ranked mechanisms have now been executed under the frozen fail-closed discipline:

| Mechanism | Verdict | Measured effect |
|---|---|---|
| Exact live-node evaluation index | `NO-ADVANCE` | About 14 percent slower than legacy |
| Child SoA staging | `NO-ADVANCE` | About 10.8 percent slower than normal |
| Direct-view transposition keys | `NO-ADVANCE` | About 8.1 percent slower than normal |
| Row export fusion | `NO-ADVANCE` | About 1.1 percent faster than normal, insufficient |

The candidate remains about 4 percent slower than the frozen legacy baseline. The binding migration gates 1 and 2 still require at most 1.02; the engineering target is 0.90. No executed mechanism reached either.

## What this evidence establishes

The four trials, each count-identical and selector-measured, now bound the removable implementation cost. The three structural mechanisms (index, staging, keys) were actively harmful; the one genuinely beneficial mechanism (row fusion) was an order of magnitude too small. This constitutes strong ceiling evidence for the current architecture: the residual gap is dominated not by these removable costs but by the candidate's fundamentally higher evaluation volume (4.653x legacy computations, from single-parent reuse scope) and its 40 percent higher canonical candidate volume, both inherent to the fast-reachability design finding more placements.

## State and boundaries

- HEAD `1e71abf` on `fast-reachability-migration`; tracked tree clean; unrelated untracked research documents untouched.
- Normal candidate remains byte-identical to `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`; all trial mechanisms are compile-time isolated and absent from production binaries.
- No legacy evidence was consumed by any of these trials. No qualification, cutover, quality campaign, gate revision, or deletion occurred.
- CPU 15 online, no surviving processes.

## Options requiring an owner decision

1. **Adopt row fusion as a candidate improvement.** It is a genuine, tightly measured 1.1 percent win. Adopting it means folding the fusion into the candidate and re-freezing artifacts; it does not change qualification status.
2. **Reopen the strategy.** The architecture review's remaining unexplored ideas (canonical-rank bitmap ordering, frontier replacement, Node hot/cold split) target smaller or riskier surfaces than the four failed trials; the evidence says they are unlikely to reach 4 percent either.
3. **Accept the gap and renegotiate the plan's performance item** (plan requirement 9: at most 2 percent slower than baseline), which would unblock qualification, quality, and the rest of the migration with the current candidate plus optionally row fusion.
4. **Stop the migration** and keep the legacy engine in production.

This report does not authorize any of these options.
