# Child SoA telemetry-off selector verdict, fresh attempt 2, 2026-09-09

Raw per-run inputs in execution order (total_s, p95_ms):
run1 pair1-normal normal total 18.425 p95 119.600.
run2 pair1-soa soa total 20.615 p95 135.289.
run3 pair2-soa soa total 20.842 p95 135.141.
run4 pair2-normal normal total 18.456 p95 120.271.
run5 pair3-soa soa total 20.502 p95 131.243.
run6 pair3-normal normal total 18.683 p95 123.235.
run7 pair4-normal normal total 18.933 p95 122.817.
run8 pair4-soa soa total 20.753 p95 134.081.

SoA divided by normal ratios per pair:
pair1 total 20.615/18.425 = 1.118860244233378561736770692 displayed 1.11886.
pair1 p95 135.289/119.600 = 1.131178929765886287625418060 displayed 1.13118.
pair2 total 20.842/18.456 = 1.129280450801907238838318162 displayed 1.12928.
pair2 p95 135.141/120.271 = 1.123637452087369357534235186 displayed 1.12364.
pair3 total 20.502/18.683 = 1.097361237488626023657870792 displayed 1.09736.
pair3 p95 131.243/123.235 = 1.064981539335416074978699233 displayed 1.06498.
pair4 total 20.753/18.933 = 1.096128452965721227486399408 displayed 1.09613.
pair4 p95 134.081/122.817 = 1.091713687844516638576092886 displayed 1.09171.

SoA/normal summary, exact full-precision Decimal arithmetic, median over four values as mean of two middle sorted values, spread as maximum minus minimum:
exact total median 1.108110740861002292697320742 displayed 1.10811, exact total spread 0.033151997836186011351918754 displayed 0.03315.
exact p95 median 1.107675569965942998055164036 displayed 1.10768, exact p95 spread 0.066197390430470212646718827 displayed 0.06620.

Classification checks:
PRIMARY-SELECT-SOA requires both exact medians at most 0.90 and both exact spreads at most 0.04: total median 1.10811 no, p95 median 1.10768 no, total spread pass, p95 spread fail.
FALLBACK-OWNER-REVIEW requires both exact medians at most 0.95 and both exact spreads at most 0.04: total median no, p95 median no, p95 spread fail.
No prior completed classification applies.

Controls: CPU 7 pinned, governor performance, EPP performance, boost 1, CPU 15 offline continuously 2026-09-09T15:52:49Z to 2026-09-09T15:55:43Z with state 0 at every boundary and restored online, telemetry off, timers off, frozen hashes and executable modes verified, precheck load5 0.67 below 1.5, no pair wait trigger, all ten commands exit 0.

Anomalies: none affecting validity. Attempt 1 ABORTED evidence is untouched and contributes no row.

Advancement boundary: the Child SoA line stops. No legacy execution, qualification, production change, or appended-pair continuation is authorized. Count gate b1ab3bc remains accepted semantic evidence only.

NO-ADVANCE
