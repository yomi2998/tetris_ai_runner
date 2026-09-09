# Parent verdict: ABORTED

The first frozen Child SoA telemetry-off selector attempt is accepted as `ABORTED` with no performance result.

The copied profile artifacts were frozen at mode `444`, which removed their execute bits. The first normal prewarm command therefore returned exit `126` before benchmark logic ran. The collector stopped immediately, ran no SoA prewarm or timed row, preserved its log, and restored CPU 15 online.

Parent checks confirmed:

- collector SHA-256 `0b2708056843ee08b5816d8448c263ac95ff8f742020a2572c688383a80f667b`;
- all four frozen hashes matched;
- one `START`, one `COMMAND`, one `END`, and one `TERMINAL=ABORTED` record;
- zero timed rows and no `rows.txt` or `rows.sha256`;
- CPU 15 restored to `1`;
- no surviving benchmark process;
- clean tracked state.

Read-only review run `92539955-d861-4e11-8ad5-efeb9285c782` accepted the abort evidence with no P0, P1, or P2 finding.

This attempt does not select, reject, or rank the Child SoA trial. It provides no ratio, median, spread, or speed evidence. The accepted semantic and count gate at `b1ab3bc` remains valid.

No command may be appended or retried under this RUNBOOK. A wholly fresh selector attempt may use new artifact and evidence paths with read-only executable mode `555`, a revised collector hash, and the same frozen order and classification boundaries. Legacy remains unauthorized.
