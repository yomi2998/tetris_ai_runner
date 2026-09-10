# Smoke verdict, 2026-09-10

Sanity gate: PASS. Zero replay failures across 64 games, no memory kill, no arena exhaustion, no crash, collector exit 0 terminal COLLECTED, CPU 15 restored online.

Diagnostic observations (not criteria): the value engine lost this smoke 27.0 to 37.0 win-equivalents (WR point 0.421875, one-sided 95 percent lower bound 0.325531, two-sided [0.308696, 0.543902]) while attacking more per piece (APP ratio 1.0479) and per line (APL ratio 1.1050), with 20 of 64 games ending in spawn death under incoming garbage. Value per-move wall median 20.648 ms against the nominal 20 ms budget; legacy-seat wall sampling is not implemented in this driver build and is recorded as a limitation.

Advancement: the smoke sanity gate permits the 128-pair screen. The screen's go/no-go thresholds (WR not below 0.40, APP and APL not below 0.90 relative) apply there. No campaign, claim, or conclusion is authorized by this stage beyond advancing to the screen.
