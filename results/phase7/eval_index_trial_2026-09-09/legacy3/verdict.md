# Legacy3 verdict, 2026-09-09

Raw per run inputs in execution order (total_s, p95_ms):
run1 1.1-a legacy total 17.465 p95 116.264.
run2 1.1-b B total 20.114 p95 132.785.
run3 1.2-a B total 19.933 p95 131.793.
run4 1.2-b legacy total 17.628 p95 117.308.
run5 1.3-a legacy total 17.563 p95 116.693.
run6 1.3-b B total 20.054 p95 132.604.

B divided by legacy ratios per pair:
pair 1.1 total 1.15167 p95 1.14210.
pair 1.2 total 1.13076 p95 1.12348.
pair 1.3 total 1.14183 p95 1.13635.

Summary, median is the middle sorted value:
median total 1.14183, spread 0.02092.
median p95 1.13635, spread 0.01862.

Classification check:
primary screen needs both medians at most 0.90: no.
fallback review needs both medians at most 0.95: no.
engineering target needs both medians at most 1.02: no, total median 1.14183 and p95 median 1.13635 both exceed 1.02.
Both spreads are at most 0.04, but spread alone does not advance.

Controls: CPU 7 pinned, governor performance, EPP performance, boost 1, CPU 15 continuously offline 2026-09-09T08:36:38Z to 2026-09-09T08:38:44Z, telemetry off, frozen hashes verified, load gates passed, no abort trigger.

Anomalies: none. No nonzero exit, no empty row, no load excursion, no extra or replacement pair.

Advancement boundary: this three pair block is not migration qualification. The result stops the index trial. No production cutover, full qualification, gate revision, or legacy deletion follows.

NO-ADVANCE
