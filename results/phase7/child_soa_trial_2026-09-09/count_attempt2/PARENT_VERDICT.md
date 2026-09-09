# Parent verdict: FAIL

Attempt 2 is void and cannot authorize a telemetry-off selector.

## Blocking protocol breach

During this RUNBOOK execution, a complete set of two partition traces and eight count rows ran while CPU 15 was online after the offline control failed with permission denied. Those files were deleted, then a replacement set of two traces and eight rows was collected and retained.

This violated the frozen requirements to keep CPU 15 offline through collection and to run no extra, replacement, or selectively retained row. Deleting the first set did not remove the violation. The provisional `PASS`, `reruns: 0`, and `no_extra_rows: true` fields in the retained metadata are rejected.

Terminal classification: `FAIL`.

## Retained-set observations

The replacement set is preserved only as rejected audit evidence. Parent read-only reconciliation found:

- byte-identical normal and SoA `inputs.bin`, `moves.tsv`, and `run_totals.tsv` trace files;
- no cross-engine non-timing difference except the intended `mem_retained_bytes` mapping;
- exact deterministic non-timing identity within all four normal rows and all four SoA rows;
- all frozen work counts matched, including `evals=26632382`, `searches=1142252`, `materialized_nodes=25757157`, and `transposition_merges=875225`;
- retained memory was `266338276` normal and `266153956` SoA, an exact reduction of `184320`, with `2281500` bytes SoA residual margin;
- all five frozen artifact hashes and all eight retained row hashes verified;
- CPU 15 was online and no benchmark process survived after the retained collection.

These observations do not cure the protocol breach and provide no wall-speed evidence.

## Independent review

Adversarial review run `8b27ca7c-9ff1-43d6-a1a0-da18018aa91a` returned P0 and `BLOCK`. It independently concluded that the `PASS` label must be rejected and only a wholly fresh, predeclared attempt may proceed.

## Advancement boundary

No telemetry-off normal-versus-SoA selector may be frozen from attempt 2. No legacy, qualification, cutover, quality, or deletion work is authorized. A new attempt must use fresh local and external directories, prove the CPU 15 control before any recorder starts, preserve every generated row, run exactly one two-trace plus eight-row collection, spell every command in full, and record load before both traces and every count run.
