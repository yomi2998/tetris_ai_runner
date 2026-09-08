# Session report 2026-09-08 (afternoon): optimization continued, gap narrowed, micro surface exhausted

## Retained this session

| Step | Binding off-mode ratio | Count ABBA | Notes |
|---|---:|---:|---|
| Eval-memo chain bucketing | 0.98363 | 0.98127, exact counters | 64 buckets, Fibonacci hash, per-slot uint16 chain, sentinel 0xFFFF, capacity fits 7680 entries; `clear_eval_memo()` replaces three inline clear sites; move ctor carries chain and heads; slot bytes 88 to 90, heads 128 bytes accounted in `engine_buffer_reservation`; capacity was 1175826 |
| Evaluate micro-opts | 0.99139 | 0.96788, exact counters | `init_t_value` reads `rows[y+k]` directly (window copy removed, `shape_rows` deleted); `wide[31]` to `wide[10]` (wide_count provably at most 9); 0-ULP frozen parity held |
| Pending-heap entry array | 0.98771 powersave, 0.99427 performance-pinned | 0.98374, exact counters | Owner-approved swap of the intrusive pairing-heap links and cached values into a fixed id-indexed `PendingHeap::PendingEntry` array (16 bytes: value, child, sibling), reserved to arena capacity in `init`, released on teardown, carried by the move ctor; the meld and pop algorithm is unchanged so the pop sequence is identical; `arena_bytes_per_node` constant (192+4+16=212) now drives the capacity derivation at all sites, capacity 1087085, test budget expectations updated; the dead `pending_child` and `pending_sibling` Node fields remain in place |

Binaries: `/tmp/tetris_profile_value.memo-chain` sha256 `39c378173e6d29ab042539574014cd73b8c73834e71a65ac664fc9f1c662cd78`, `/tmp/tetris_profile_value.eval-micro` sha256 `24ad42399e28e47a02e6e78725401be06fac1f1ce400bd98e802d9f94f5872fb`, `/tmp/tetris_profile_value.heap-entries` sha256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765` (current retained tree).

## Rejected and fully reverted this session

All six reverted to byte-identical retained binaries (sha verified after each revert).

| Experiment | Binding off-mode | Verdict |
|---|---:|---|
| `[[gnu::always_inline]]` `Engine::materialize` + size caching | 1.09573 | Forced inline bloated the off-mode hot path; count mode was 0.99740 but off mode regressed hard |
| Queue boundary storage `vector<bool>` to `vector<uint8_t>` | count 1.00698 | Bit extraction was already cheaper than the wider storage loads |
| `build_key` skeleton split (cursor-keyed boundary/active-piece skeleton, in-place fill) | count 1.00812 | 130-byte skeleton copy costs more than the at most 7-iteration boundary loop |
| Dedup compare order swap (outcome before board) | count 1.03785 | Sibling outcomes are usually equal, so the branchy 3-field compare added work before an already early-exiting board compare |
| `[[gnu::always_inline]]` `Engine::evaluate_once` | 1.01002 | Count mode gained 0.97917, binding off mode regressed; telemetry-only gain, same pattern as the earlier trusted-apply rejection |
| Dedup fingerprint prefilter + shared `evaluate_once` fingerprint (parallel `child_fingerprints_` vector, accounting +61,440 bytes, capacity 1175513) | 1.00181 | Count mode gained 0.96871 with exact counters, but the board compare already early-exits on the first occupancy word in off mode, so the fingerprint cost cancelled the scan savings |

## Measured standing versus frozen production

Fresh 100-move off-mode ABBA versus `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline` (sha256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`), telemetry off, `--quiet-version 2` for the baseline and 3 for the candidate.

Under the default powersave governor (amd-pstate-epp, frequency floating between 3.02 and 5.39 GHz) the interleaved blocks were too noisy to trust: the frozen baseline itself spread 9.04 to 9.54 s across blocks. After pinning CPU 7 to the performance governor (sudo write to `scaling_governor`, EPP `performance`), back-to-back runs agree within 0.7 percent and the standing is reproducible:

- Total-time ratio: 1.0324 (gate: at most 1.02)
- Per-move p95 ratio: 1.0422 (gate: at most 1.02)

Remaining gap: about 1.2 percent on total and 2.2 percent on p95. The p95 penalty exceeds the total penalty, so the heaviest moves are relatively slower; the profile under the pinned governor shows the same closed surfaces as before (evaluate 15.3 percent, materialize 12.4, expand clones about 17.6, promote down from 7.1 to 5.5 after the heap swap, transition 6.75, evaluate_once 4.28, probe 4.06, scan_safe_rows 3.68, build_key 4.8). Every one of these surfaces has already been optimized to a retained win or rejected in a controlled experiment; no further exact source-level lever with meaningful expected value is identified.

## Full matrix validation of the retained tree

All four suites pass on all four configurations with zero failures:

- GCC debug, GCC self-release, Clang debug, Clang self-release
- `tetris_engine_tests` 59181, `toj_policy_tests` 71906 (0-ULP frozen parity), `rule_differential` 904022, `profile_value_tests` 159

## Blocker

Eight consecutive exact-optimization attempts were rejected on binding measurements after the retained wins, and the owner-approved pairing-heap entry-array swap (lever 2 below) has now been implemented and retained, delivering 1.23 percent off-mode under powersave (0.57 percent under the pinned performance governor). The remaining self-times are dominated by work the compiler already schedules well: fused rule geometry (enumerate and apply, about 30 percent combined across clones), evaluate arithmetic (about 15 percent), Child/Node construction (materialize about 12.5 percent), transition row scans (about 10 percent combined), and table probes. The following levers remain and each needs an owner decision before they are attempted:

1. Profile-guided optimization for the `linux-gcc-self-release` preset. The phase 7 gate compares the candidate against a frozen production binary, so a candidate-side flag change is measurable, but it changes the production build pipeline (two-stage builds, training profiles) and would need Clang parity. The plan text pins the preset, not the flag string, for the phase 7 gate, but this is a protocol question, not a source edit. Training on the gate workload itself would be methodologically circular; a differently-seeded training corpus would be needed.
2. ~~Replacing the intrusive pairing heap~~ Done and retained this session.
3. Relaxing the gate or revising scope. Not permitted without the owner.

## Next

- Owner decision on the three levers above.
- The audit script repair (`tests/audit_phase7_campaign.py`), the fused-rule boundary and public layout tests, and the fresh qualification campaign remain pending and depend on the perf gate path chosen.
