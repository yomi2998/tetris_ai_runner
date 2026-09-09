# Direct-key telemetry-off selector verdict, 2026-09-09

Raw per-run inputs in execution order (total_s, p95_ms):
run1 pair=1-normal normal total 18.604 p95 120.713.
run2 pair=1-direct-key direct-key total 20.973 p95 136.871.
run3 pair=2-direct-key direct-key total 20.753 p95 135.384.
run4 pair=2-normal normal total 19.144 p95 125.426.
run5 pair=3-direct-key direct-key total 20.410 p95 134.179.
run6 pair=3-normal normal total 18.947 p95 123.794.
run7 pair=4-normal normal total 18.713 p95 121.768.
run8 pair=4-direct-key direct-key total 20.158 p95 130.371.

Direct-key divided by normal ratios per pair:
pair1 total 20.973/18.604 = 1.127338206837239303375618147 displayed 1.12734.
pair1 p95 136.871/120.713 = 1.133854680109018912627471772 displayed 1.13385.
pair2 total 20.753/19.144 = 1.084047221061429168407856247 displayed 1.08405.
pair2 p95 135.384/125.426 = 1.079393427200102052206081674 displayed 1.07939.
pair3 total 20.410/18.947 = 1.077215390299255818863144561 displayed 1.07722.
pair3 p95 134.179/123.794 = 1.083889364589560075609480266 displayed 1.08389.
pair4 total 20.158/18.713 = 1.077219045583284347779618447 displayed 1.07722.
pair4 p95 130.371/121.768 = 1.070650745680310097891071546 displayed 1.07065.

Direct-key/normal summary, exact full-precision Decimal arithmetic, median over four values as mean of two middle sorted values, spread as maximum minus minimum:
exact total median 1.080633133322356758093737347 displayed 1.08063, exact total spread 0.050122816537983484512473586 displayed 0.05012.
exact p95 median 1.081641395894831063907780970 displayed 1.08164, exact p95 spread 0.063203934428708814736400226 displayed 0.06320.

Classification checks:
PRIMARY-SELECT-DIRECT-KEY requires both exact medians at most 0.90 and both exact spreads at most 0.04: total median no, p95 median no, total spread fail, p95 spread fail.
FALLBACK-OWNER-REVIEW requires both exact medians at most 0.95 and both exact spreads at most 0.04: total median no, p95 median no, both spreads fail.
No prior completed classification applies.

Controls: CPU 7 pinned, governor performance, EPP performance, boost 1, CPU 15 offline continuously 2026-09-09T17:40:49Z to 2026-09-09T17:43:44Z with state 0 at every boundary and restored online, telemetry off, timers off, frozen hashes and executable modes verified, precheck load5 0.73 below 1.5, no pair wait trigger, all ten commands exit 0, zero reruns, no surviving process.

Anomalies: none.

Advancement boundary: the direct-key line stops. No legacy execution, qualification, production change, or appended-pair continuation is authorized. Count gate 368a332 remains accepted semantic evidence only.

NO-ADVANCE
