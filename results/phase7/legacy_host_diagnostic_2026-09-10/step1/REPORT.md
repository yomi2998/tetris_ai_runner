# Step 1 report: stage-cost table from input preparation through canonical candidates

Classification: `DIAGNOSTIC NON-BINDING`. Step 1 exists to produce the cost budget for interpreting the Step 3 binding block. It has no pass or fail threshold.

**Explicit non-inference statement: tree performance is not inferred from any raw BFS ratio in this report.** Per-call stage costs, call volumes, and end-to-end engine throughput are different quantities, and the value engine and the legacy engine do not make the same number of calls on the same boards. Nothing here may be read as an engine speed claim, a qualification result, or a superiority claim over legacy.

## 1. Collected cells

Collected once at HEAD `fd614b9`, exit 0, empty stderr, 3 warmup plus 7 timed batches per cell, 200000 drives for S1 and S1x and 50000 drives for the rest, median batch nanoseconds divided by drives. Per-drive case rotation defeats hoisting, the sink is checked nonzero per cell, and the poisoned-board check passed. All values are nanoseconds per call.

| corpus | piece | cases | S1 raw BFS | S1x prep | S2 arrival+extract | S3 full enum | S4 legacy prepared | S4i legacy import | S5 adapter | ordered digest |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| c1 | T | 33 | 93.3 | 0.7 | 177.5 | 728.0 | 3138.0 | 2576.8 | 881.8 | `93f2ec9fc1f6e64c` |
| c1 | Z | 33 | 98.7 | 0.7 | 216.6 | 797.0 | 620.4 | 692.4 | 839.8 | `6c30c09397bf6b87` |
| c1 | S | 33 | 93.6 | 0.8 | 203.9 | 742.9 | 809.4 | 864.8 | 787.0 | `1580c224c420993a` |
| c1 | J | 33 | 97.8 | 0.7 | 211.5 | 846.2 | 697.6 | 758.9 | 969.1 | `553c932559f6257f` |
| c1 | L | 33 | 90.8 | 0.7 | 186.5 | 750.9 | 742.7 | 800.8 | 845.4 | `8574485920579ba6` |
| c1 | O | 33 | 34.6 | 0.7 | 42.5 | 170.1 | 70.9 | 197.8 | 190.2 | `403986c1ba717fe4` |
| c1 | I | 33 | 116.5 | 0.7 | 273.1 | 844.5 | 1084.3 | 1131.5 | 902.1 | `6caeaa8c14cd969f` |
| c2 | T | 105122 | 160.0 | 3.1 | 283.2 | 1126.0 | 8120.7 | 6592.0 | 1440.4 | `4b593052cd6d75fd` |
| c2 | Z | 105122 | 163.3 | 2.9 | 319.1 | 1103.6 | 424.5 | 512.7 | 1161.4 | `83febf532ec0659f` |
| c2 | S | 105122 | 163.4 | 3.1 | 317.9 | 1093.2 | 413.9 | 522.0 | 1161.6 | `a966b74a647fcdb2` |
| c2 | J | 105122 | 154.8 | 2.9 | 304.6 | 1123.4 | 531.6 | 632.9 | 1315.5 | `0f1920259378bb45` |
| c2 | L | 105122 | 158.1 | 3.0 | 316.6 | 1136.3 | 507.0 | 608.9 | 1290.2 | `b795bb390c13a5b1` |
| c2 | O | 105122 | 94.9 | 2.9 | 100.2 | 211.8 | 132.0 | 222.5 | 244.1 | `8589cc26ddda06b4` |
| c2 | I | 105122 | 174.9 | 2.8 | 370.0 | 1069.6 | 529.5 | 638.0 | 1147.5 | `16e02c38003ca92a` |

## 2. Per-corpus medians

| Corpus | Cases | S1 raw BFS | S1x prep | S2 arrival+extract | S3 full enum | S4 legacy prepared | S4i legacy import | S5 adapter |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C1 legacy subcorpus | 33 | 93.6 | 0.7 | 203.9 | 750.9 | 742.7 | 800.8 | 845.4 |
| C2 real-workload replay | 105122 | 160.0 | 2.9 | 316.6 | 1103.6 | 507.0 | 608.9 | 1161.6 |

C1 is the curated 33-board legacy-domain corpus and cannot establish 48-row target compatibility. C2 is the frozen parent-input replay corpus from the real workload and is the relevant budget. C1 remains only as the cross-check against the gate89 corpus ratios.

## 3. What the enumeration wrapper is made of, on C2

Using the T, S, L, and J medians as the reference cells:

| Component | Median ns | Share of S3 |
|---|---:|---:|
| S1 prepacked raw BFS | 160.0 | 14.5 percent |
| S2 workspace plus arrival search plus landing extraction | 316.6 | 28.7 percent |
| S3 full production enumeration, ordered canonicalization included | 1103.6 | 100 percent |
| Normalization cost implied by S3 minus S2 | 787.0 | 71.3 percent |

The raw BFS is a small minority of the production enumeration. Most of the enumeration wrapper is not reachability at all; it is bucketing, sorting, keying, and first-survivor deduplication. This is the same asymmetry the port recorded for Reference A, where the wrapper was hundreds of times its own BFS kernel, only here at a much smaller multiple.

## 4. The two figures the decision request asked about

The owner's premise paired a sub-50 ns `binary_bfs` against a roughly 1500 ns legacy `search_tspin::Search`. Under the identical boards, identical flags, and batch timing this step required, neither figure matches this measurement:

- Raw BFS measured 93.6 ns on C1 and 160.0 ns on C2 per call. It is a real order of magnitude below the legacy call, but it is not below 50 ns on either corpus.
- Legacy search measured 507.0 ns prepared-import and 608.9 ns import-inclusive on C2, and 742.7 ns and 800.8 ns on C1. It is far below 1500 ns at this scope.
- The comparison that matters for the premise, full production enumeration S3 against legacy prepared search S4 on C2, is 1103.6 versus 507.0, a ratio of 2.18 against the candidate. Import-inclusive it is 1.81 against the candidate.

So the premise that a roughly 30 times faster kernel should translate into a faster engine is not supported at the isolated stage level. The kernel advantage is roughly 3 to 6 times per call against legacy search, and it is consumed inside the candidate's own enumeration wrapper before any tree work begins.

### Scope caveat, reported rather than reconciled away

The recorded in-engine diagnostic rows put candidate enumeration near 1087 ns per call and legacy search near 1751 ns per call, ratio 0.621 in the candidate's favor (`results/phase7/architecture_review_2026-09-09/raw/architecture-review-2026-09-09-candidate-timers.txt` and the matching legacy file). The isolated S3 value here, 1103.6 ns, agrees closely with the in-engine candidate figure. The isolated S4 and S4i values, 507.0 and 608.9 ns, are far below the in-engine legacy 1751 ns. The two scopes are not equivalent: the in-engine legacy number includes warm-cache and depth-state effects, different boards at different call positions, and telemetry perturbation, while S4 measures one prepared search call per drive over a rotating low-density corpus. This step therefore does not reproduce the 0.621 ratio, and it does not refute it either. Step 3 must settle the question with matched-parent replay rather than with these stage rows.

## 5. Adapter conversion cost

On C2, S5 minus S3 is 58.0 ns per call median, so converting canonical candidates to legacy poses and resolving them through `TetrisContext::get` adds about 5 percent over the enumeration itself. On C1 the same difference is 94.5 ns, about 13 percent. Conversion is not the expensive part of a legacy-host design at this stage scope.

Two disclosures apply. First, S5 counts only successfully mapped poses and silently skips poses `to_legacy` cannot represent, so the required unmappable-pose subline is unmeasured rather than zero. Second, occupancy at or above row 40 appears in zero C2 records, so no C2 candidate can be rejected for upper-row reasons; this says nothing about full 48-row target compatibility and must not be read as such.

## 6. Reconciled count partition

The C2 corpus was independently reparsed after collection and reconciles exactly to the accepted trace totals for the 20-move workload accepted at `2c75c94`:

| Quantity | Computed from corpus | Accepted sidecar | Match |
|---|---:|---:|---|
| Input records | 111719 | distinct_inputs 111719 | yes |
| Distinct board and piece keys | 105122 | bench reported 105122 | yes |
| Out-of-domain records | 0 | distinct_ood 0 | yes |
| Sum of multiplicities | 276768 | enum_calls 276768 and recorder_calls 276768 | yes |
| Weighted apply-ok candidates | 7266627 | unique_candidates 7266627 and rule_transitions 7266627 | yes |
| Weighted survivors | 6132302 | policy_transitions 6132302 | yes |
| Distinct result boards among survivors | 6126843 | not directly recorded | informational |
| Materialized nodes | not derivable from candidate lists | 5978871 | carried unchanged |
| Transposition merges | not derivable from candidate lists | 153431 | carried unchanged |
| Expanded parents | not derivable from candidate lists | 145544 | carried unchanged |

The multiplicity-weighted candidate and survivor totals reproducing `enum_calls`, `unique_candidates`, `rule_transitions`, and `policy_transitions` exactly means the stage table is being read against the real per-call work distribution, not an unrepresentative board set.

## 7. Board-class distribution, reported at summary level

Per-class medians are not available because the bench emits per-piece cells only. Counts computed from the frozen corpus:

| Class | Count |
|---|---:|
| Fully legacy-domain mappable, distinct boards | 105122 |
| Records with occupancy at or above row 40 | 0 |
| T inputs with at least one terminal-rotation arrival, records | 18201 of 18201 |
| T inputs with terminal-rotation arrival, multiplicity weighted | 38959 |
| Blocked spawn | not counted, disclosed limitation |
| Density decile 0 below 44 occupied cells | 94703 distinct, 236821 weighted |
| Density decile 1, 44 to 87 occupied cells | 10419 distinct, 39947 weighted |

Every recorded T input carries a terminal-rotation arrival, so the arrival-class semantics a legacy-host adapter must preserve are exercised on 100 percent of the T population of this workload. The corpus is uniformly low-density, which is favorable to both engines and is a stated limit of this budget.

## 8. What Step 1 decides and what it does not

Step 1 establishes the budget: raw BFS is 14.5 percent of the production enumeration, normalization is the majority of it, conversion is cheap at about 5 percent, and the legacy search call is cheaper per call than the candidate's full enumeration on the real workload. It does not decide whether the adapter wins inside engine dataflow, because call volumes, cache state, tree work, and evaluation reuse dominate end-to-end time and are Step 3's matched-parent question.

Proceeding to Step 3 requires the Step 2 feasibility gates to pass first, including the digest parity, path and replay success, counted categories, complete memory accounting, and clean sanitizer run. No timing may be taken on an implementation that fails a feasibility gate.
