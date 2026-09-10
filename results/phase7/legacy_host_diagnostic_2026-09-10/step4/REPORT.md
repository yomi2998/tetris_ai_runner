# Step 4 report: legacy-host diagnostic outcome

Date: 2026-09-10.
Terminal conclusion: the binding Step 3 block is `ABORTED` by an operating-system OOM kill inside arm `L1`, and the diagnostic ends here per the runbook's stop rule. Report to owner follows.

## What was measured (Step 1, diagnostic non-binding)

Stage cost table over identical boards, flags, and frozen parameters, 7 timed batches per cell after 3 discarded, batch timing only (`step1/stage_table.tsv`):

| Stage | C1 legacy subcorpus (33 boards) | C2 real-workload replay (105122 distinct inputs) |
|---|---:|---:|
| S1 raw `binary_bfs` | 93.6 ns | 160.0 ns |
| S1x board prep | 0.7 ns | 2.9 ns |
| S2 workspace + arrival search + landing extraction | 203.9 ns | 316.6 ns |
| S3 full production enumeration (canonicalization included) | 750.9 ns | 1103.6 ns |
| S4 legacy search, import prepared | 742.7 ns | 507.0 ns |
| S4i legacy search, import-inclusive | 800.8 ns | 608.9 ns |
| S5 adapter conversion + pose resolution | 845.4 ns | 1161.6 ns |

Findings that correct the decision premises:

1. The raw kernel is 93.6 to 160.0 ns per call on matched inputs, not under 50 ns.
2. Legacy `search_tspin::Search` is 507.0 to 608.9 ns per call at prepared import on the real workload, not roughly 1500 ns. The kernel-to-legacy advantage is roughly 3 to 6 times, not roughly 30 times.
3. On the real workload the candidate's complete enumeration is MORE expensive per call than a legacy search call (S3/S4 = 2.18): normalization, bucketing, sorting, and dedup are 71.3 percent of the wrapper, and they outweigh the kernel saving at equal call counts.
4. Adapter conversion into the legacy domain is cheap (S5 minus S3 about 58 ns on C2, about 5 percent at this scope), and 100 percent of recorded T inputs carry at least one `TerminalRotation` candidate, so arrival-class preservation is exercised everywhere.

## What the binding block showed before it died (Step 3)

- `run1_L0` (legacy engine, legacy search, full 145544-parent replay) completed: `total_searches=692749398`, wall about 93 minutes, one `DIAG_V1` row preserved.
- `run1_L1` (legacy engine, fast adapter) was killed by the operating system OOM killer (exit 137) at 2026-09-10T08:05:08Z after driving RAM and swap to 100 percent (directly observed by the owner).
- The collector restored CPU 15 online, preserved partial evidence, and ran no replacement.

## Why L1 exhausted memory

The legacy engine's tree is unbounded by design and was built for fresh engines per process; the frozen legacy profile protocol runs exactly one move per engine for this reason, and in-process fresh legacy engines are recorded to diverge (`docs/phase7/comparator_schema.md`, slice 7.1C). Streaming 145544 parents through one legacy engine instance accumulates the full tree. The adapter arm returns the value engine's fuller candidate set (about 1.8 times legacy's placements per parent), so its legacy tree grows correspondingly faster than `L0`'s and hit the memory wall first. The value engine bounds this with its arena and table caps; the legacy engine has no such contract.

This is decision-relevant evidence, not just an infrastructure failure: **a legacy-host fast adapter cannot run the real workload inside any reasonable memory bound without per-parent engine recycling**, and per-parent recycling in one process is the exact regime the migration's own parity evidence (7.1C) shows to be unreliable for legacy. A chunked multi-process redesign is possible but is a new runbook, a new budget, and an owner decision.

## Conclusion

Under the runbook section 9 taxonomy this is closest to **C4 (feasibility failure, no binding timing)**: the three-configuration comparison never produced a complete round, so no E1/E2 screen result exists and no adapter win or loss can be claimed. The memory feasibility gate (7.2 gate 5) is failed by the legacy-host arms at real-workload scale for any arm carrying the legacy tree.

Combined with Step 1, the evidence now says:

1. The original `<50 ns` versus `~1500 ns` premises are both wrong at matched scope; the true per-call gap is 3 to 6 times, and the candidate spends more per call than legacy by the time canonicalization finishes.
2. The legacy host cannot stream the production workload within memory bounds; a chunked redesign would be required to even complete the comparison.
3. The candidate's structural costs (normalization 71.3 percent of the wrapper; single-parent evaluation reuse) remain the dominant, still-unfixed costs.

## Options now (owner decision; nothing automatic)

- **B-conditional:** abandon the legacy-host route (memory evidence above) and instead authorize the small candidate-side bundle: adopt row fusion (measured 1.09 percent faster) plus at most one more equal-size mechanism, then re-run the gate. This reaches about 1.030 total median, still short of 1.02, so it does not unblock migration alone.
- **A (unchanged):** measure quality first; the binding comparison harness must be built (`src/tuner.cpp` has no `compare-engines`).
- **C:** amend the performance contract with the combined evidence (Step 1 stage table, four NO-ADVANCE trials, this memory finding).
- **D:** stop the migration.

No cutover, deletion, production change, or further collection is authorized by this report.
