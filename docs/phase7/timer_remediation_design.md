# Slice 7.2B design note: timer remediation (P1, P2, P3)

Pre-gate result: median on/off whole-run ratio 1.165 against the 1.005 bar,
with bit-identical work counts. The delta is pure measurement cost. No
baseline capture has occurred.

## 1. Read-site quantification (200-move fixed-work evidence rows)

Value engine, timers on, per run. Every span costs two `timer_now` reads at
roughly 25 ns each (vDSO clock):

| Site | Reads per item | Items per run | Reads per run |
|---|---|---|---|
| `evaluate_once` entry plus hit/miss exit | 2 | 6,732,608 requests | 13.5M |
| Rule apply plus dedup per candidate | 2 | 7,760,873 applications | 15.5M |
| Policy transition per accepted child | 2 | 6,732,608 transitions | 13.5M |
| Enumeration per `expand_source` call | 2 | 304,796 calls | 0.6M |
| Materialization per child | 2 | 6,553,600 nodes | 13.1M |
| Parent expansion per promote/root pass | 2 | 159,205 parents | 0.3M |
| Path find plus replay per finalization | 4 | 200 calls | 800 |

Total about 56M reads, about 1.1 to 1.4 s on a 7 s run: 15 to 19 percent,
matching the measured 16.5 percent. Counters alone (increments at roughly
1 ns each over about 25M sites) project to well under the bar; the rerun
pre-gate proves or refutes that projection.

Legacy comparator, same workload: transition wrapper 2 reads over 64M
completions (about 3.2 s), eval hooks about 40M reads (about 1 s), search
wrapper 2 reads over 2.5M calls, plus per-land-point normalization
(`to_placement`, cell-set construction, sort, roughly a million land
points). Together these explain the disclosed 1.74x total with bit-identical
work vectors. The `std::map` per-invocation dedup additionally churns the
allocator between spans.

## 2. Mode model (no binding re-scope)

Three modes, same binary, same monotonic clock discipline, same scopes:

- `--telemetry off`: everything disabled, byte-identical behavior to today.
- `--telemetry on --timers off`: counters enabled, all component and engine
  timer reads skipped, boundary wall-time reads stay. Work vectors
  bit-identical to full-timer runs. This is the qualification mode for
  totals, counts, and every timed-mode row.
- `--telemetry on --timers on` (default, current behavior): full per-item
  component spans for binding rates, usable only in fixed-work mode where
  extra wall time cannot change decisions or counts.

No binding numerator or denominator changes scope: every component stays
per-item timed in full-timer runs. The schema gains one appended field
named `timers` with values `on`, `off`,
or `na` (when telemetry is off), placed after the last current field so no
existing order shifts. Timed-mode qualification rows on both engines use
counters-only mode; timer spans always come from fixed-work full-timer
runs. Injected `timer_nanos` test hooks, cache-stamp preservation, deadline
reads, verification, and telemetry-off behavior are unchanged.

## 3. P2: comparator allocation-span timer

Two reads around the fresh and recycled branches of the node allocator,
accumulated into a new `alloc_ns` counter reported per move. Expected cost
is about 8M reads on the reference workload, confined to full-timer runs.
It feeds the gate 4 materialized leg with the same nesting rules (allocation
contains no sub-spans, so non-overlap holds trivially).

## 4. P3: comparator cost reduction

- Replace the per-invocation `std::map` dedup with a linear-probe table
  over the full key (cells hash, channel, matched flag, opaque bits) with
  exact comparison on probe hits, no silent collision merging, and buffers
  reused across invocations so steady-state allocation is zero. Identity
  contract unchanged: per-invocation scope, O collapse, T channels,
  unmatched classification, all count identities.
- Time the base search call and the normalization phase as two separate
  spans (`search_ns` for the call only, new diagnostic `norm_ns`), so
  normalization sits outside every binding span by construction and the
  fixed-work leakage into binding rates is structurally zero; any residual
  is measured and bounded with the absolute evidence.
- Counters-only comparator mode additionally skips normalization entirely
  (unique and unmatched report unavailable there; deterministic full-timer
  runs supply them), keeping the timed-mode rows representative:
  counters-only comparator rows may feed gate 6 only while the measured
  counters-only-vs-frozen total delta stays at or below 1.02, otherwise the
  legs return for re-review before execution.

## 5. Timed-mode representativeness (Finding A.2)

Gate 6 legs on both engines use counters-only timed rows. The candidate
side carries no timer cost by construction. The comparator side carries
counting plus `make_path` (about 1 percent over frozen per the independent
1.011x measurement); the execution slice re-measures this delta on the
frozen workload and the legs proceed only under the 1.02 bound above.

## 6. What the re-gate proves

Re-run pre-gate (counters-only vs off, five pairs, 1.005 bar), parity
re-verification against frozen totals, determinism re-verification,
rejection-behavior re-verification, fresh candidate freeze with new hashes,
and a fresh comparator freeze with the on/off instrumentation ratio and the
within-span leakage bound. Baseline capture stays blocked until all of it
is green.
