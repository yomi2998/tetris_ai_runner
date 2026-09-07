# Coordinator verdicts on the pre/post diagnostics (2026-09-07)

Verdicts by the coordinating agent on the measurements in `summary.md`.
These are diagnostic judgments, not binding gate outcomes; the binding
campaign requires re-frozen artifacts and the full protocol, plus the
gates 8/9 legacy comparators (design:
`docs/phase7/legacy_corpus_comparators_design.md`).

1. Semantics preservation: PASS at production scale. All 20 count fields
   bit-identical across A/B/D/D2, including `texhaust_moves=174`,
   `unique_candidates=62,283,081`, `transitions=53,598,323` — the latter
   two matching the recorded 7.2E verdict numbers exactly, independently
   re-confirming the frozen binary on this machine. The epoch change
   meets its design count criterion (deltas of exactly zero). The epoch
   change is accepted; no revert.
2. End-to-end: paired off-mode totals D/A median 0.7487 (five pairs,
   spread 0.7373-0.7541; p95 0.7023) and D/B median 0.8716 (three pairs,
   0.8527-0.8765; p95 0.8313). The four combined changes cut about 25
   percent off the candidate's fixed-work time; the epoch change alone is
   worth about 13 percent end-to-end, far beyond its direct clear-traffic
   share (~0.4 percent of a run). The plausible mechanism is eliminated
   per-root L3 pollution (roughly 80 MiB of clear traffic per reuse root
   before the change); stated as consistent-with, not proven.
   Cross-session projection: against the recorded 7.2E gate-1 ratio
   (6.81284 vs legacy), the post-epoch candidate sits near roughly 5.1x
   legacy — approximate arithmetic across sessions, not a gate claim.
3. Component attribution (full-timer singles, unpaired, low resolution):
   materialize/nodes D/A 0.6318 and D/B 0.8133 — large and consistent
   with the halved entry size. Honest flag: the expected `is_landing`
   benefit in the rule leg is NOT demonstrated by this data (rule/unique
   B/A 1.0757, D/A 1.0603); either the per-candidate workspace
   construction was not the dominant rule cost or the effect is below
   single-run attribution resolution. No regression signal in the paired
   totals. The enum leg (D/A 1.0451) has no mechanism behind it and sits
   at the edge of the single-run noise band. Both legs are forwarded to
   the binding campaign for adjudication; no component verdict is taken
   from this campaign.
4. Comparator: the exact-identity repair is verified collision-free at
   production scale — old and new comparator report identical
   `unique_candidates` (55,838,330) and `transitions` (64,334,998) on the
   full fixed-work seed-1 workload. Normalization cost dropped about 12.5
   percent (`norm_ns` median 0.8746). New/old full-timer total median
   1.0121. The counters-only bound mirror reproduces the recorded 1.02528
   to 8e-05 (strong determinism of the recorded evidence), and the fresh
   full-timer total median 1.0121 sits under the 1.02 bar in a three-pair
   diagnostic — promising for re-establishing the gate-6 prerequisite
   under the proper five-pair protocol, not a claim.
5. Microbench (five alternating reps, pinned core): cold root 2,005,617
   ns -> 280 ns per root; reroot 2,986,203 ns -> 451,307 ns. Matches the
   design expectation exactly: O(1) reset; reroot retains its O(N) scan,
   which now dominates that path.

Next steps in order: legacy corpus comparator implementation (gates 8/9),
re-freeze of the changed candidate and comparator artifacts, then the
binding re-campaign under the full protocol.
