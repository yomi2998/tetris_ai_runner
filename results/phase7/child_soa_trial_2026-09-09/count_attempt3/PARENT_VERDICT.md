# Parent verdict: PASS

Attempt 3 satisfies the frozen Child SoA semantic and count gate.

## Collection validity

- Execution HEAD was `1884768fa927272b0436b944eeb7ea4630201fbb` with a clean tracked tree and all required ancestry.
- Collector SHA-256 was `b3e6e686bc190b7676a4bc0c536dbfa1686be57bc6c529f4b9ff6056f2ee314f`.
- The collector was invoked once and logged exactly ten `START`, ten `COMMAND`, and ten `END` records.
- The sole terminal line was `TERMINAL=COLLECTED`.
- No prewarm, extra command, replacement row, retained subset, or anomaly occurred.
- CPU 15 remained offline from `2026-09-09T15:20:46Z` through `2026-09-09T15:22:13Z`, read `0` before and after every collected command, and was restored to `1`.
- Load5 was `0.78` at the precheck and ranged from `0.78` to `0.92` during collection.
- Every command exited zero, every count stdout contained one `PROFILE_V3` row, and all trace and count stderr files were empty.

## Semantic identity

Normal and SoA trace files were byte-identical:

- `inputs.bin`: `c6b5ecf6c2b7e956b7770ea957820d77fefc4fcaad0a9e1be22a55d1d1473da5`;
- `moves.tsv`: `723c2c83ef6de336ef8750d76fec7198cdea2b94545f8d10ad4badfc4d7c674b`;
- `run_totals.tsv`: `46934e5f13682b02e1df729c26c86d0f2c74f599f4e6136b3610ce5b3c374785`.

Both trace stdout rows had 67 fields and matched on every non-timing field except the intended retained-memory mapping.

All eight count rows had 67 fields. Runs 1, 4, 5, and 8 were identical on all normal non-timing fields. Runs 2, 3, 6, and 7 were identical on all SoA non-timing fields. Across engines, every non-timing field except `mem_retained_bytes` was identical.

All 37 frozen common values matched in every row, including:

- `evals=26632382`;
- `transitions=26632382`;
- `searches=1142252`;
- `parents=599166`;
- `raw_landings=65566529`;
- `unique_candidates=32446394`;
- `eval_memo_hits=684163`;
- `eval_computed=25948219`;
- `materialized_nodes=25757157`;
- `transposition_merges=875225`;
- `path_states=76513`.

Retained memory was `266338276` normal and `266153956` SoA. The reduction was exactly `184320` bytes. SoA residual margin below the `268435456` cap was `2281500` bytes.

## Parent verification

The parent independently verified:

- the collector hash;
- all five frozen binary and parameter hashes;
- the prerequisite transcript hash;
- all eight row hashes;
- all ten external trace hashes, sizes, and read-only modes;
- exact command identity between `collect.log` and the ten fully spelled `commands.txt` entries;
- trace byte equality;
- all 67 fields in all trace and count rows;
- all deterministic, cross-engine, expected-count, and memory checks;
- CPU 15 online and zero surviving benchmark processes after collection;
- clean tracked state.

The parent corrected `summary.json` from `all_38_match` to `all_37_match` because the runbook enumerates 37 common exact values. This was a metadata label correction. No raw row, log, command, hash manifest, or external trace file changed.

Read-only review run `d1865c33-ce17-41a6-ba1d-0dfc8a260867` found no P0, P1, or P2 issue and returned `ACCEPT`.

## Advancement boundary

The count gate is accepted `PASS`. A telemetry-off normal-versus-SoA selector may now be frozen.

This count evidence establishes semantic and work-vector identity only. It provides no wall-speed result and does not authorize a legacy comparison, qualification, production cutover, quality campaign, gate revision, or deletion.
