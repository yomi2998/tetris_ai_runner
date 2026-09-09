# Parent verdict: PASS

The direct-view transposition key count gate is accepted.

## Collection validity

- Execution HEAD was `ef4772ede2af20f152163f79a62e6dd2ddd5fd69` with a clean tracked tree.
- Collector SHA-256 was `86d99434a7fadcf62061238844c93346fc37de0f9db8158ca8a47a742b0a38c3`.
- The collector was invoked once, logged exactly ten `START`, `COMMAND`, and `END` records, and reached the sole terminal `TERMINAL=COLLECTED`.
- One continuous CPU 15 offline window covered both traces and all eight rows, `2026-09-09T17:27:29Z` to `17:28:57Z`, with state `0` at every boundary and restoration to `1`.
- Precheck load5 was `0.96`; per-run load5 stayed at or below `1.05`.
- Zero reruns, zero extra commands, all exits zero.

## Semantic identity

- Normal and direct-key partition traces were byte-identical on `.inputs.bin`, `.moves.tsv`, and `.run_totals.tsv`, and the stdout rows matched on every non-timing field.
- All eight count rows had 67 fields with every non-timing field identical across engines, including `mem_retained_bytes=266338276` on every row, exactly as required for a no-storage-change trial.
- All 37 frozen common values matched in every row, including `evals=26632382`, `materialized_nodes=25757157`, and `transposition_merges=875225`.
- The collector's template `soa` labels map to the direct-key variant in all metadata; the mapping was verified against the hash-verified commands.

## Parent verification

The parent independently verified the collector hash, all six artifact hashes, both hash manifests, the external trace byte identity and read-only modes, all row fields, deterministic repeats, full cross-engine identity, CPU 15 online, zero surviving processes, and clean tracked state.

Read-only review run `1aa73470-b8d1-42c8-8d7c-9b1cd8b5eaf5` found no P0, P1, or P2 issue and returned `ACCEPT`.

## Advancement boundary

A telemetry-off normal-versus-direct-key selector may now be frozen. Count evidence provides no wall-speed result and does not authorize legacy work, qualification, cutover, quality, or deletion.
