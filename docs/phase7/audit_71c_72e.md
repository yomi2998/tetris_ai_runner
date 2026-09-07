# Independent audit verdict: slices 7.1C through 7.2E (2026-09-07)

Scope: everything executed from slice 7.1C onward, per the migration plan's
requirement that weaker-model work in that range be independently audited
before it is accepted. Work before 7.1C was previously validated and is not
re-audited here. Three Luna workers audited comparator code, campaign
evidence, and transposition memory respectively; this document records the
coordinating model's own verification of their findings and of the
resulting source changes, plus the campaign-claim repairs.

Nothing here is migration completion, production cutover approval, or
acceptance of slices 7.1C–7.2E as qualified. The 7.2E campaign verdict
remains NOT QUALIFIED.

## 1. Source-change validation (six modified files, unstaged at audit time)

### 1.1 Comparator exact-identity repair (`src/legacy_cmp_normalize.h`,
`tests/legacy_cmp_tests.cpp`)

Luna comparator-audit finding: matched-candidate equality keyed only on a
64-bit occupancy digest, violating the documented exact occupied-cell
identity contract. The finding is real, and the repair is correct:

- `NormalizedKey` now retains the sorted `candfmt::Cells`; equality and
  ordering compare cells plus channel for matched keys.
- The digest is retained only as the probe-table hash accelerator
  (`hash_key` unchanged), so probe collisions resolve by exact comparison.
- Ordering remains a strict weak ordering (cells, then channel; unmatched
  keys ordered by opaque bits, never equal to matched keys). No consumer
  sorts normalized keys, so the ordering-basis change is inert.
- The added regression forces a same-digest/different-cells pair and proves
  the probe table retains both as distinct (`distinct() == 2`).

Verdict: repair accepted. Caveats recorded, not waived: no natural
digest collision was demonstrated in any historical row (the defect was
latent for the recorded campaigns, so their numbers are not invalidated by
it), and the changed comparator binary requires fresh instrumentation and
binding measurements before any future campaign uses it.

### 1.2 Compact transposition keys (`src/tetris_engine.h`,
`tests/tetris_engine_tests.cpp`)

Luna transposition-audit change: replace the over-aligned kernel
`board_t` in `TranspositionKey` with eight logical 64-bit words.
Verified:

- `TranspositionOccupancy` copies `logical_word(i)` for all
  `word_count()` words; equality is exact word-wise; the words are the
  same logical payload the kernel hash uses.
- `transposition_hash` mixes the same fields in the same order with the
  same byte-wise FNV loop as before, reading the compact words; hash
  inputs are preserved, signed-zero normalization is preserved, and the
  equality field set is unchanged. Reordering members does not affect
  either because no whole-struct bytes are hashed.
- `static_assert` pins key = 152 bytes and entry = 160 bytes; `build_key`
  assigns occupancy through the converting assignment operator.
- Default table remains 262,144 entries; memory accounting is recomputed
  from the smaller entries, so `default_arena_capacity` grows inside the
  256 MiB budget by construction. The 524,288-entry experiment was
  correctly reverted (an existing table-exhaustion fixture hit arena
  exhaustion first; capacity design needs both table and arena demand
  evidence).

Verdict: representation change accepted as semantics-preserving.
Performance is unmeasured; the near-full-table probe-chain cliff is
untouched by this change.

### 1.3 Per-candidate landing validation (`src/toj_rule.h`,
`tests/rule_differential.cpp`)

Change: `apply` and `classify_spin` previously constructed a full
board-wide `search_workspace` (usable-position bitboards for every shape)
per candidate; both now use `is_landing`, which checks the candidate's
four cells and the pose immediately below. Verified:

- For every in-board pose, `is_landing` is equivalent to the old
  `checker.is_valid(r,x,y) && !checker.is_valid(r,x,y-1)` predicate: the
  added exhaustive differential compares all pieces, rotations, x, y
  against the kernel checker plus rule bounds on 32 deterministic boards
  including high boards (about 904,022 checks, zero failures after the
  oracle fix). The initial oracle failure (68 cases) was the kernel's open
  ceiling: `checker.is_valid` accepts poses whose cells extend above row
  47, which the corrected oracle excludes via `in_bounds`. The floor
  boundary matches because below-floor cells fail both predicates.
- Out-of-board poses are now rejected at the guard. Old `apply` already
  rejected them later via `occupancy_mask`; old `classify_spin` could
  over-classify them, so the new behavior is strictly more compliant with
  plan Section 9.4 (non-landable input rejected rather than reclassified).
  Production candidates come from in-board enumeration either way.
- Existing geometry, arrival, T-spin, clear, lockout, and replay fixtures
  still pass (re-run in this audit's build matrix, Section 3).

Verdict: change accepted. It removes a measured-hotspot candidate (full
workspace construction per candidate) but no timing benefit has been
measured yet; that measurement is a required follow-up, not an assumed
result.

## 2. Campaign-evidence audit

All numeric claims in `results/phase7/recampaign_verdicts.md` were
independently recomputed from the raw rows and manifest with the repaired
`tests/audit_phase7_campaign.py` and match exactly:

- Gate 1 median 6.81284 (FAIL); gate 2 median 7.66833 (FAIL).
- Gate 3 seed-1 deltas unique +0.11542 / transitions -0.16689 (BLOCKED:
  no 100-percent-accounted partition exists).
- Gate 4: enumeration, unique, eval-hit, eval-miss, transition,
  materialized all over the 1.02 bar; only the path leg passes.
- Gate 5 max 0.00011 (PASS).
- Gate 6 medians 0.23624 / 0.24699 / 0.26125 (diagnostic FAIL, binding
  determination additionally blocked by gate 3 and the unmet comparator
  bound).
- Comparator counters-only bound median 1.02528 (UNMET vs 1.02).
- Gate-3 deterministic twins agree on all count fields (new check added
  during this audit).

Gates 8 and 9: the recorded PASS claims are unsupported and have been
corrected to UNPROVEN in both `results/phase7/verdicts.md` and
`results/phase7/recampaign_verdicts.md`. `results/phase7/corpus_gate.txt`
proves the Phase-2 kernel-regression gate (current raw BFS vs frozen
kernel, 1.020 bar) and the Reference A T gate (0.0928, about 10.8x
faster); it contains no comparison against legacy placement enumeration,
which plan Section 17.3 items 8 (1.00 bar, per parent) and 9 (1.02 per
normalized semantic candidate) require. No legacy enumerator comparator
binary exists in `tests/run_perf_gate.py` or the CMake target set. The
Phase-2 evidence itself stands and is preserved.

Provenance: 57 manifest rows of the 7.2C campaign reference the
superseded `c4579a87...` candidate binary through a path that now holds
the re-frozen `816bcd7d...` build, and no immutable copy of the old
binary exists under the results tree. Old-binary reproducibility for
7.2C cannot be claimed; the raw rows and manifest remain preserved as
collected, and the binding 7.2E campaign used the re-frozen binary with
full provenance.

Audit tooling: the worker's draft `tests/audit_phase7_campaign.py` was
reviewed and repaired during this audit. Removed: the invalid
`gate9-observed` estimate (a raw kernel-vs-kernel T ratio multiplied by
Reference A candidate counts — meaningless mixture). Replaced: fragile
prose checks on the harness with a structural comparator inventory.
Added: gate-3 twin-consistency verification. The script's recomputed
medians are the numbers quoted above; treat it as evidence-grade for
row-to-verdict recomputation, with the standing caveat that it reads
recorded rows and cannot substitute for the missing legacy comparators.

## 3. Build and test validation of the combined change

Full configure/build/CTest matrix over the combined six-file change with a
stable tree (no concurrent source edits), 2026-09-07:

| Preset | Build | CTest |
|---|---|---|
| linux-gcc-self-release | ok | 29/29 pass, 0 fail, 0 not-run |
| linux-gcc-debug | ok | 29/29 pass, 0 fail, 0 not-run |
| linux-clang-debug | ok | 29/29 pass, 0 fail, 0 not-run |
| linux-clang-self-release | ok | 29/29 pass, 0 fail, 0 not-run |

- The Clang debug crash recorded in the paused session (malformed
  UTF-8/preprocessor text plus a compiler segfault on
  `tests/tetris_engine_tests.cpp`) did NOT reproduce with stable sources;
  the file compiled and linked cleanly under clang 22.1.8. This supports
  the concurrent-source-edit explanation; no compiler workaround was
  applied and none is needed.
- No test was "Not Run" in any preset: the earlier missing-executable
  failure mode did not recur once all targets were built.
- Fresh check-count runs from the self-release tree: `tetris_engine_tests`
  16,238 checks, 0 failures; `rule_differential` 904,022 checks, 0
  failures (matching the paused session's reports).
- `git diff --check` clean at the end of the matrix.
- Self-release binaries of the changed tree (diagnostic anchors for the
  pre/post measurements): `tetris_profile_value` 58f4674ddf3f...,
  `tetris_profile_legacy_cmp` fa830879ab9a...; the untouched default
  `tetris_profile` rebuilt byte-identical to the frozen baseline
  (84cb7a30...), corroborating that phase-7 commits left the legacy
  default target's sources behaviorally unchanged.
- Scope: this matrix validates the combined change at unit and
  integration level on all four presets. It is not a timing campaign and
  requalifies no performance gate; the 7.2E campaign timings belong to
  the pre-repair binaries and must be regenerated (Section 5).

## 4. Verdict summary

| Item | Verdict |
|---|---|
| Comparator identity defect (Luna finding) | Confirmed real; repair accepted |
| Compact transposition key representation | Accepted, semantics-preserving; performance unmeasured |
| `is_landing` rule validation | Accepted; equivalence exhaustively pinned; stricter invalid-input rejection |
| 7.2E numeric verdicts | Independently reproduced from raw rows |
| Gates 8/9 recorded PASS | Unsupported; corrected to UNPROVEN in both verdict documents |
| 7.2C old-binary reproducibility | Not claimable (binary absent); rows preserved |
| 7.2E overall campaign | NOT QUALIFIED (unchanged; now with honest gates 8/9) |
| Production cutover / legacy deletion | Not authorized; unresolved gates remain |

## 5. Required follow-up before qualification can be re-attempted

1. Re-freeze the changed candidate and comparator binaries (identity
   repair changes the comparator binary; compact keys and `is_landing`
   change the candidate binary) and regenerate the binding measurements;
   the recorded 7.2E timings belong to the pre-repair binaries.
2. Build the two missing frozen legacy comparators required by plan items
   8 and 9: a legacy placement-enumeration comparator (non-T, per-parent,
   1.00 bar) and a legacy T enumerator comparator with per-normalized-
   semantic-candidate normalization (1.02 bar).
3. Measure bounded before/after diagnostics for the three accepted source
   changes against the immutable 7.2E binaries on a pinned core, with no
   concurrent build or test load.
4. Continue measured hotspot remediation in plan order. The confirmed
   unmeasured candidate is `reset_run_state` clearing all 262,144 full
   entries per root although `transposition_probe` tests `used` before
   reading `key`; the accepted epoch-stamp design and its measurement
   gates are recorded in `docs/phase7/transposition_reset_design.md`.
5. Resolve the count partition (new instrumentation) and the comparator
   overhead bound, then rerun the binding profile gates. Do not proceed
   to tuner/match/DLL cutover or legacy deletion while gates fail.
