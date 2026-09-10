# Final campaign verdict, 2026-09-10

Terminal decision: NON-INFERIORITY-FAILED.

## Evidence

2000 seat-swapped games (1000 pairs, seed 999, 20 ms budgets per seat per move, threads 8), collected in one fail-closed collector invocation at HEAD `b6357e1` with CPU 15 continuously offline and restored online. Raw rows in `matches.csv`, driver summary in `collect.log`.

## Frozen criteria applied at full precision

1. WR: the Wilson one-sided 95 percent lower bound (z = 1.6449) is `0.384110`, which does not exceed `0.47`. FAIL. Point estimate 0.4020 (804.0 of 2000 win-equivalence); two-sided 95 percent CI `[0.380720, 0.423656]`.
2. APP: value/legacy ratio `1.011053` (per-game means 1.027320 versus 1.016087; totals cross-check 1.027203). PASS.
3. APL: value/legacy ratio `1.077518` (per-game means 1.590098 versus 1.475673; totals cross-check 1.081175). PASS.

Superiority context (non-gating): the two-sided CI lies entirely below 0.50, so the value engine is statistically significantly worse at winning than legacy at equal 20 ms budgets, not merely non-superior.

## Health and consistency

- Zero replay failures, zero arena exhaustions, zero capped games across all 2000 games.
- Deaths: value 1196 (627 reason-1 class containing the value seat's own 620 spawn plus 7 lockout; 569 reason-2), legacy 804 (431, 373).
- Value per-move wall: median 20.759 ms, p95 22.848 ms, max 30.125 ms; the budget contract held.
- The result is consistent with the independent smoke (WR 0.422, 64 games) and screen (WR 0.4336, 256 games) samples; the campaign tightens the interval, not the story.
- CPU 15 verified online at end; no surviving process.

## Meaning

The quality premium hypothesis is answered with evidence: at equal per-move budgets, the fast-reachability engine's extra aggression (higher attack per piece and per line) does not convert into wins. It dies more often (1196 versus 804) while generating more attack, so the additional placements produce a more aggressive but less survivable style on this workload. Under the frozen pre-declared mapping, non-inferiority failure on any criterion stops the migration per option D: the legacy engine stays in production, and no qualification, cutover, production change, or legacy deletion follows.

NON-INFERIORITY-FAILED
