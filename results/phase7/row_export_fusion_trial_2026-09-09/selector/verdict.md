# Row export fusion telemetry-off selector verdict, 2026-09-09

Raw per-run inputs in execution order (total_s, p95_ms):
run1 pair1-normal normal total 18.391 p95 120.352.
run2 pair1-fusion fusion total 18.188 p95 118.863.
run3 pair2-fusion fusion total 18.262 p95 119.203.
run4 pair2-normal normal total 18.412 p95 119.677.
run5 pair3-fusion fusion total 18.172 p95 118.102.
run6 pair3-normal normal total 18.368 p95 119.465.
run7 pair4-normal normal total 18.420 p95 119.759.
run8 pair4-fusion fusion total 18.131 p95 117.839.

Fusion divided by normal ratios per pair, full-precision Decimal:
pair1 total 18.188/18.391 = 0.9889619922788320374096025229731934098200 displayed 0.98896.
pair1 p95 118.863/120.352 = 0.9876279579898963041744216963573517681468 displayed 0.98763.
pair2 total 18.262/18.412 = 0.9918531392570063002389745817944818596567 displayed 0.99185.
pair2 p95 119.203/119.677 = 0.9960393392214042798532717230545551776866 displayed 0.99604.
pair3 total 18.172/18.368 = 0.9893292682926829268292682926829268292683 displayed 0.98933.
pair3 p95 118.102/119.465 = 0.9885908006529108944042188088561503369188 displayed 0.98859.
pair4 total 18.131/18.420 = 0.9843105320304017372421281216069489685125 displayed 0.98431.
pair4 p95 117.839/119.759 = 0.9839678020023547290809041491662422030912 displayed 0.98397.

Fusion/normal summary, median over four values as mean of two middle sorted values, spread as maximum minus minimum:
exact total median 0.989145630285757482119435407828060119544 displayed 0.98915, exact total spread 0.0075426072266045629968464601875328911442 displayed 0.00754.
exact p95 median 0.988109379321403599289320252606751052533 displayed 0.98811, exact p95 spread 0.0120715372190495507723675738883129745954 displayed 0.01207.

Classification checks:
PRIMARY-SELECT-ROW-FUSION requires both exact medians at most 0.90 and both exact spreads at most 0.04: total median 0.98915 exceeds 0.90, p95 median 0.98811 exceeds 0.90. Primary does not pass.
FALLBACK-OWNER-REVIEW requires both exact medians at most 0.95 and both exact spreads at most 0.04: total median 0.98915 exceeds 0.95, p95 median 0.98811 exceeds 0.95. Fallback does not pass. Both spreads pass.
No prior completed classification applies, so the verdict is NO-ADVANCE.

Controls: CPU 7 pinned, governor performance, EPP performance, boost 1, CPU 15 offline continuously 2026-09-09T19:42:51Z to 2026-09-09T19:45:33Z with state 0 at every boundary and restored online, telemetry off, timers off, frozen hashes and executable modes verified, precheck load5 0.93 below 1.5, no pair wait trigger, all ten commands exit 0.

Anomalies: none affecting validity. The fusion trial is faster than the normal candidate in all four pairs on both metrics, with a total-median improvement of about 1.09 percent and a p95-median improvement of about 1.19 percent, inside the 0.76 to 1.17 percent removable-cost band predicted by the design lane. The improvement is real but below the 0.95 fallback band, so no owner fallback review is implicated by the frozen thresholds. The template `soa` log labels and filenames map to the row fusion variant; raw files were not renamed.

Advancement boundary: this is the second and final authorized remaining optimization attempt. The row export fusion line stops under the frozen thresholds. No legacy execution, qualification, production change, or appended-pair continuation is authorized. Count gate 2c75c94 remains semantic evidence only. The candidate-versus-legacy performance gap is uncorrected by the executed attempts: the exact live-node index, Child SoA staging, direct-view transposition keys, and row export fusion all returned NO-ADVANCE, and the migration performance gate therefore still fails at approximately 4 percent behind the frozen legacy baseline.

NO-ADVANCE
