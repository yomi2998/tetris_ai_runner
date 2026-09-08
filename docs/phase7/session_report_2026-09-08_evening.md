# Session report, 2026-09-08 evening: compliant campaign run, PGO rejected, campaign paused

The owner's instruction for this session: keep both 1.02 limits; prepare an auditable candidate; run a compliant Gate 1/2 campaign exactly once; on failure pursue one bounded GCC PGO effort with the numeric requirement preserved; if PGO fails, pause rather than continue open-ended source experiments. Re-freezing or recalibrating the baseline is not the move.

## Preparation (all committed)

- `58e2739`: the seven modified source/test files, the full retained optimization stack through the pending-heap entry array. The tracked tree is clean afterwards; no comments or stray code entered the diff (checked).
- `1b046db` (after an intermediate docs commit): fused-boundary tests in `rule_differential.cpp` (911,655 checks now, up 7,633) covering bad-rotation rejection, out-of-bounds and overlap nullopt, floating versus supported landing against a cell-based oracle, lowest-cell pinning, exact pose masks, lockout at lowest cell 19 versus 20 through apply, clear-to-empty perfect_clear, and the non-clearing non-perfect apply; plus public layout tests in `tetris_engine_tests.cpp` (59,203 checks) pinning board 64, node 192 with all documented offsets including the parked dead pending-link fields, child 192, and the pending entry (16 bytes, offsets 0/8/12).
- Same commit: `tests/audit_phase7_campaign.py` repaired. Known superseded freezes now report as historical INFO instead of errors; current gate-1/2 statuses come from the newest `gate12_*` campaign while superseded results carry HISTORICAL- tokens; gate 3 recognizes the validated partition bundle; gates 8/9 parse the recorded `results/phase7/gate89/` evidence at `ea6bf7f` (SUPPORTED, verdicts quoted as harness-printed) instead of declaring UNPROVEN from harness text alone.
- Validation: all four configurations (GCC and Clang, debug and self-release) pass all four suites with zero failures: 59,203, 71,906, 911,655, and 159 checks.
- Freeze: clean reconfigure and rebuild of `linux-gcc-self-release` from HEAD `1b046db` reproduced the retained-session candidate byte for byte (`1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`). Frozen read-only as `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08`. The baseline (`84cb7a30...`), legacy comparator (`08e91644...`), and parameter file (`ea95ba58...`) verified unchanged; nothing frozen was overwritten. One mishap recorded honestly: the first freeze attempt used mode 444, which strips the execute bit; the aborted partial campaign block was fully discarded, the mode corrected to 555 matching the frozen convention, and the complete five-pair block rerun from scratch.
- Machine record: dated `docs/phase7/campaign_machine_record_2026-09-08.md`, no rewrite of the historical powersave-era record. Governor and EPP pinned to performance on CPU 7, boost fixed at 1, SMT sibling CPU 15 taken offline for the measurement window and restored after, compiler and kernel recorded, temperatures and frequency samples recorded.

## Gate 1/2 campaign: NOT QUALIFIED at gate 1

Five ABBA+A pairs at 200 moves, seed 1, telemetry off, first-engine order baseline, candidate, candidate, baseline, baseline. Raw rows and MANIFEST preserved unaggregated in `results/phase7/gate12_2026-09-08/`; verdict in `verdict.md`.

- Gate 1 (total): pair ratios 1.05275, 1.02564, 0.98159, 1.01410, 1.02674. Median 1.02564: FAIL.
- Gate 2 (p95): pair ratios 1.04563, 1.01597, 0.94282, 1.00023, 0.98981. Median 1.00023: PASS on median.
- The pair spreads (0.071 total, 0.103 p95) block any pass claim under the standing noise rule, and equally mean the gate-1 fail sits inside the measurement band. The first baseline block (17.516 s) ran 5.4 to 7.6 percent faster than the other four baseline blocks; substituting the baseline steady state for that one block would put the total median near 1.014. The residual noise driver is background desktop load (about 1.8), which no operator control in this session can remove. No favorable pairs were appended; the sanctioned one-time control-corrected rerun remains available to the owner.
- The audit script now sources gate 1/2 from this campaign (gate1=FAIL, gate2=PASS) and fires its uncertainty error as required; its exit code 1 is the honest state, not a tooling bug.

## Bounded PGO attempt 1: rejected

Full record in `results/phase7/pgo_attempt1/` (CORPUS.md frozen before building, corpus rows, gcda and hashes, build commands, report). Counters stayed bit-identical on the count twin, but held-out seed-8 screening regressed 5.98 percent total and 7.19 percent p95 with tight within-kind blocks. The mechanism is visible in perf: PGO inlined materialize, build_key, probe, and scan_safe_rows into their callers and applied isra clones; promote rose from 5.5 to 19.5 percent self-time, the same forced-inline sensitivity the source experiments showed. The second authorized corpus attempt is deliberately not spent and is preserved for a future owner decision.

## Standing after this session

- Current standing versus frozen production under pinned controls: total median 1.02564, p95 median 1.00023, with measurement spread exceeding the verdict margin in both directions.
- Source-level surface: exhausted (ten controlled rejections plus three retained wins this phase; every remaining profile surface already at a retained win or rejected in a controlled experiment).
- PGO under the preset flags: rejected on evidence; a flag amendment (for example disabling hot-cold partitioning or value profiling under PGO) is a protocol question for the owner, not a unilateral change.
- Nothing is cut over, no legacy code is removed, and the frozen artifacts are untouched.

## Options that remain for the owner

1. The sanctioned one-time control-corrected rerun of the complete five-pair block, with the machine as quiet as the owner can make it (desktop session closed), which given the anomaly analysis is the likeliest way the current candidate clears gate 1 legitimately.
2. Spend the second PGO corpus attempt (my judgment: unlikely to flip the direction given the mechanism evidence).
3. A narrow PGO flag amendment (the transformation class is the lever, not the corpus).
4. Revise the gate with a product-latency justification (the reviewer's standard: a waiver, not fulfillment).
5. Pause as instructed, which is the state this session ends in.
