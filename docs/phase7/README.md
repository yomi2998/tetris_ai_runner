# Phase 7 record: profile migration

## Status: paused (2026-09-10)

The migration is paused by owner decision after the quality campaign
(`c676532`, report `docs/phase7/session_report_2026-09-10_quality_campaign.md`)
returned `NON-INFERIORITY-FAILED`: at equal 20 ms budgets the value engine won
40.2 percent of 2000 games against the legacy engine, below the frozen 0.47
non-inferiority floor. Performance had already failed (binding 1.04155 against
the 1.02 bar; four `NO-ADVANCE` optimization trials), and the legacy-host
alternative was shown memory-infeasible at real-workload scale. Production
remains on the legacy engine, byte-frozen at `84cb7a30...`. All migration work,
diagnostics, and evidence are preserved on this branch (`fast-reachability-migration`,
HEAD `7cfaec0`) and under `results/phase7/`; the legacy deletion and Phase 8
were never started. Revisit requires a new owner decision.

## Slice 7.1A: profile comparison contract (closed)

Design note `profile_71a_design.md` revision 3, amended at close with the
verdict decisions: root-only no-executed-hold meaning, comparator parity
bars as prerequisites with per-pair scopes, the verified legacy
depth-table evaluation model, `pending_end_max` naming, refill inside the
full-move denominator, and valid discovered states as the legacy path-state
denominator. No source implementation was authorized in 7.1A.

## Slice 7.1B: value profile and correctness gates (implementation)

Scope per authorization: value-only profile support and the
`tetris_profile_value` target; approved root, queue, hold, application,
failure, warmup, and timing contracts; observational engine counters,
timers, and telemetry switch; V3 output; contract and lifecycle tests.
Deferred: `tetris_profile_legacy_cmp` instrumentation, any timing campaign
or speed claim, search optimization, whole-tree hold restriction, parameter
retuning, production cutover, tuner/match/DLL changes.

### What was built

- `src/tetris_engine.h`, `src/tetris_engine.cpp`: `ComponentTimers`
  (enum, rule, eval hit/miss, policy, materialize, parent aggregate,
  path find, path replay) with per-root reset matching the `SearchStats`
  lifecycle; `EngineConfig::telemetry_enabled` gating all search, cache,
  timer, and path accumulators while preserving cache stamps, replacement
  behavior, budget/deadline reads, and verification; separate `timer_nanos`
  so injected test clocks stay deterministic; `component_timers()` reader;
  `finalize` split into find and replay spans.
- `src/profile_value_support.h` (new, engine-free, value side only):
  options parsing (`--quiet-version 3` only, `maxdepth` bounded to 255),
  seven-bag scenario generator, shared attack/combo accounting, and the
  66-field `PROFILE_V3` row builder with `na` for disabled measurements.
- `src/tetris_profile_value.cpp` (new): the five-step move loop with
  `T_SETUP`/`T_ROOTSEARCH`/`T_RUN`/`T_PATH`/`T_MOVE`/`T_APPLY` boundaries,
  caller-owned policy config with per-move safe margin and T seeding,
  canonical-spawn finalization with lockout death, rule-API application,
  ordinary-death restart, and invalid-run handling for rejected roots and
  finalization failures in any move including warmup.
- `tests/profile_value_tests.cpp` (new, 59 checks): generator schedule
  differential, accounting outcome-space differential, root-seed
  differential against the legacy policy reads, V3 order and `na` tests,
  counter lifecycle and evaluation-identity tests, telemetry-toggle
  decision identity, rejection and failure-predicate tests.
- `CMakeLists.txt`: `tetris_profile_value` and `profile_value_tests`
  targets plus binary determinism, no-hold, and telemetry-off integration
  tests. `src/tetris_profile.cpp` and the default target are unchanged.

### Evidence

- Engine regression: 16,236 checks, 0 failures (GCC debug, Clang debug,
  sanitizer build).
- New unit tests: 59 checks, 0 failures (GCC debug, GCC self-release,
  Clang debug, sanitizer build).
- Full GCC debug CTest: 18 of 18 pass, including the 4 new profile tests.
- New targets build under GCC and Clang self-release; the legacy default
  target still builds in both.
- Binary smoke runs: deterministic fixed-iteration repeats, zero
  `replay_failures`, `na` component fields with numeric boundary timers
  under `--telemetry off`, root-only `--no-hold` trajectories.
- One instrumentation repair during the slice: component timer reads
  initially consumed the injected engine clock and broke the fake-clock
  budget test; budget reads stay on `clock_nanos` while timer spans read
  `timer_nanos` (same steady-clock default).

## Slice 7.1B repair round (HOLD verdict)

Bounded repairs only; no comparator, campaign, or cutover work.

- `T_ROOTSEARCH` now starts immediately before `set_root`; refill and root
  seeding stay in `T_SETUP` and `T_MOVE`. A scripted-clock boundary test
  pins exact spans (setup 4, root-search 3, run/path/apply 1 tick each).
- Runner extracted into `src/profile_value_runner.h` with an injectable
  clock so death branches run through the actual move logic: spawn death
  records setup only with zero search/path work and no stale snapshot;
  lockout retains completed search/path measurements before the reset;
  hold and queue bookkeeping sit inside the apply span; rejected roots and
  finalization failures invalidate in any move including warmup.
- Cache-hit evaluation timer now closes after memo population, matching
  the miss-path scope.
- `pending_occupancy` refreshes on early stop paths (exhaustion); an
  exhaustion test pins heap-at-stop occupancy (capacity minus two with
  zero merges) for two capacities.
- Strict numeric parsing, representable-budget validation, warmup-plus-moves
  overflow rejection, accepted telemetry values, and `--help` describing
  root-only `--no-hold`; invalid input exits nonzero with no `PROFILE_V3`
  row, covered by three new CTest rejection tests.
- Test hardening: multi-move toggle trajectory identity (selections,
  finalized paths, boards, queue, hold), root-only no-hold execution,
  enabled-vs-disabled row builder fields, validator unit tests, and the
  fixture factory replaced with in-place initialization.

### Repair evidence

- Unit tests: 151 checks, 0 failures (GCC debug, GCC self-release, Clang
  debug, sanitizer build).
- Full GCC debug CTest: 21 of 21 pass, including 7 profile integration
  tests.
- Engine regression: 16,236 checks, 0 failures (GCC debug, Clang debug,
  sanitizer build).
- New targets plus the unchanged legacy default build under GCC and Clang
  self-release.
- Release smoke runs complete with zero replay failures; natural play
  produced no deaths, so death branches rest on the directed runner tests.

## Slice 7.1C: supplemental legacy comparator (implementation)

Bounded to observational changes and test-only support. No campaign, no
speed claims, no baseline replacement, no legacy search/order/legality
changes, no cutover work.

### Approach

- New files only for the build: `src/tetris_profile_legacy_cmp.cpp` (legacy
  loop replicated from the frozen profile, plus per-move production
  `make_path` timing), `src/legacy_cmp_observer.h` (attachable counters and
  timers), `src/legacy_cmp_normalize.h` (land-point identity), and
  `docs/phase7/comparator_schema.md` (PROFILE_CMP record contract).
- Shared headers carry strictly additive `#ifdef TETRIS_LEGACY_CMP` hook
  blocks (`tetris_core.h`: eval request/hit/call split, widening iterations,
  parent expansions with overlapping aggregate timer, fresh/recycled/root
  node counts, status-identity reuse counts; `tetris_core.cpp`: path-mark
  recording; `search_tspin.cpp`: valid-discovered-state scope guard). The
  flag is set on exactly the comparator and its test target, so frozen
  builds compile zero hook code. `src/tetris_profile.cpp`, the default
  target, and frozen artifacts are unchanged.
- Normalization reuses the existing migration facilities
  (`ExternalPoseTransform`, sorted-cell occupancy hashing): per-invocation
  scope so current and hold branches never merge, O rotation collapse, T
  spin and last-rotation channels, opaque status-bits identity for
  unconvertible land points, which are classified under
  `unmatched_candidates` and never silently discarded.
- Established identities: requests equal hits plus calls; materialized
  search children equal requests; linked survivors equal raw landings when
  every land point links exactly one child; comparator `eval_calls`,
  `transitions`, and `searches` reproduce frozen V2 totals exactly with
  matching dead, game, and pool-byte columns.

### Evidence

- New unit tests (`tests/legacy_cmp_tests.cpp`): 44 checks, 0 failures
  (GCC/Clang debug, sanitizer, GCC self-release): normalizer unit cases,
  empty-board directed fixtures matching classical landing counts per piece,
  observer count/timer identities, timers-disabled counting, single-run
  consistency, and path-state cases including start-equals-goal.
- New CTests: frozen work-total parity, comparator determinism, and
  telemetry-off rows; full GCC-debug CTest 25 of 25 pass.
- Both profile artifacts plus the comparator build under GCC and Clang
  self-release.
- Pre-existing limit documented in the schema: sequentially constructed
  fresh legacy engines in one process can disagree (exact flag-off repro
  and build command recorded there; also reproduced with unmodified
  pre-7.1C sources), so parity is established at process level (separate
  deterministic runs), never by in-process fresh-engine comparison. That
  behavior predates this slice and is outside it; both profile binaries run
  one engine across moves exactly like the frozen loop.

## Slice 7.1C repair round (HOLD verdict)

- Strict numeric parsing in the comparator (unsigned and double syntax,
  finite non-negative bounded budgets, accepted telemetry values,
  warmup-plus-moves overflow guard, `maxdepth` bound): invalid input exits
  nonzero with no `PROFILE_CMP` row, covered by three new rejection CTests
  mirroring the value-profile cases. The frozen binary keeps its loose
  parsing; parity on valid inputs re-verified.
- Human-readable summary prints search-child materialization
  (`fresh + recycled`) under the materialized label, matching the binding
  quiet field.
- Schema field 11 wording aligned to the frozen `transitions` column name.
- Divergence substantiation: minimal flag-off repro (two sequential fresh
  engines, empty map, piece T, lookahead `TOJ`, 4 fixed iterations, `-O0`)
  diverges first-vs-second engine and reproduces identically with
  unmodified pre-7.1C sources; schema and record amended to the observed
  facts with exact repro settings instead of the earlier mechanism claim.

## Slice 7.2A: qualification campaign preparation

- Campaign protocol (`qualification_protocol.md`): run matrix, pair order,
  preservation format, gate formulas for plan items 1 through 11 with V3,
  V2, and PROFILE_CMP column mapping, overhead evidence plan, and
  pre-execution prerequisites.
- Machine record (`machine_record.md`) with undisguisable limits disclosed.
- Frozen read-only artifacts with hashes; parameter bytes verified.
- Telemetry pre-gate: median on/off 1.16466 vs the 1.005 bar (FAIL),
  caused by per-item clock reads; baseline capture blocked.

## Slice 7.2B: remediation of P1, P2, P3 (implementation)

- P1: three-mode model (`off`, counters-only `--timers off`, full-timer
  default) with counter/timer gating split in the value engine; per-item
  component scopes unchanged; `timers` field appended to V3 with no order
  shift. Re-gate: median counters-only/off 1.01412 vs the 1.005 bar (FAIL
  as stated; first pair cold-start outlier, remainder noise-dominated).
  Hardware counters show a structural +1.4 percent in cycles against +0.2
  percent in instructions, while same-mode wall repeats spread plus or
  minus 2 percent unpinned-frequency noise: the bar is unresolvable on
  this machine as specified. Forwarded: batched-counting design or the
  twin-run methodology amendment; baseline capture stays blocked.
- P2: `alloc_ns` spans the fresh and recycled allocator branches, reported
  per move; feeds the gate 4 materialized leg against
  `V3(materialize_ns)`.
- P3: per-invocation dedup replaced by a reused linear-probe table with
  exact full-key comparison (a hash/contract mismatch that silently
  overcounted distinct identities was caught by the new regression test
  and fixed); base-call and normalization spans split into `search_ns`
  and diagnostic `norm_ns`; counters-only mode skips normalization.
  Fresh evidence (40-move workload): off 0.645 s, counters-only 0.607 s,
  full 1.161 s with `norm_ns` 0.113 s of the delta.
- Protocol amended per Finding A (timers field, counters-only timed rows,
  unblocked materialized leg, leakage and representativeness statements).

## Slice 7.2C-prep: twin-run amendment and plumbing smoke

- Records authorized option (B): binding totals from `--telemetry off`
  rows with counts and rates from deterministic twins, replacing the
  unresolvable 0.5 percent pre-gate by construction; per-gate row-sourcing
  table and noise rule included. Gates 1 and 2 use off-mode baseline rows
  (deviation from on-mode recording, stated).
- Documentation fixes: P1 wording (resolved by amendment, not by passing
  re-gate), re-frozen hashes in the inputs table, committed re-verification
  rows, schema field 40 subtree-release precision.
- Plumbing smoke (`results/phase7/smoke/`): all three modes on both engines
  flow into rows plus MANIFEST with artifact hashes; all exit 0 with
  correct tokens and timer labels.

## Slice 7.2D: transposition capacity repair

- Measured true demand with an oversized (1,048,576-entry, raised-budget,
  experimental only) build: zero exhaustion on all 660 fixed-work moves
  with per-move distinct-state peaks 448,536 / 429,964 / 408,023 across
  seeds 1/2/3 (medians near 320,000, minima above 221,000); timed mode
  peaks at 22,084 with no exhaustion.
- 262,144 entries is the largest fundable power of two inside 256 MiB
  (2 by 262,144 by 320 bytes against a 173,957,408-byte fixed workspace,
  leaving a 291,598-node arena); 524,288 entries alone would exceed the
  budget. Semantics preserved: per-root reset, no overwrite, rehash
  behavior, fail-stop on exhaustion. Residual fixed-work overflow
  fail-stops gracefully with best-so-far intact; timed demand carries 12x
  headroom. Key compaction or a replacement policy would be needed to hold
  the full fixed-work peak and both stay deferred.
- Permanent additions: `transposition_used()` accessor and per-move table
  pressure in the runner record, with a unit gate pinning observability
  inside capacity. Exhaustion fixtures use their own small capacities and
  are unaffected.

## Slice 7.2E: binding re-campaign (resized candidate)

- Demand evidence preserved with manifest (`results/phase7/sizing/`).
- 124 rows plus MANIFEST collected under the corrected protocol; overall
  verdict NOT QUALIFIED (`results/phase7/recampaign_verdicts.md`): gates
  1, 2 fail at 6-7x (honest volume, expected); gate 3 blocked (deltas now
  +12/-17 percent class, still unpartitionable without new
  instrumentation); gate 4 fails 6 of 7 legs with the 100-percent
  occupancy caveat on materialization (13-14x); gate 5 passes; gate 6
  fails at 0.22-0.32x with the timed rows now untruncated and therefore
  informative; comparator bound unmet this session (1.02528 vs 1.02);
  gates 8 and 9 corrected to UNPROVEN (2026-09-07 audit): the corpus
  harness compares kernel-versus-kernel and Reference A only, so the
  legacy placement-enumeration comparators that plan items 8 and 9
  require were never built.

## Qualification repair: audit, source repairs, pre/post diagnostics (2026-09-07)

- Audit of 7.1C-7.2E committed (`d928a05`): three source repairs accepted
  after source review, exhaustive differentials, and a four-preset
  build/ctest matrix (29/29 each); comparator exact occupied-cell
  identity, compact 152/160-byte transposition keys, per-candidate
  landing validation. Gates 8/9 corrected to UNPROVEN in both verdict
  documents; audit tooling repaired (`tests/audit_phase7_campaign.py`);
  all recorded 7.2E numbers independently reproduced from raw rows.
- Epoch-stamp transposition reset implemented and accepted (`510b356`,
  design in `transposition_reset_design.md`) with the reset microbench.
- Bounded pre/post diagnostics campaign
  (`results/phase7/prepost_diagnostics/`, coordinator verdicts in
  `verdict.md`): count identity exact across frozen/3-repair/post-epoch
  binaries; paired off-mode totals D/A 0.7487 (all four changes) and
  D/B 0.8716 (epoch alone); materialize leg 0.6318; comparator identity
  repair verified collision-free at production scale (55.8M uniques
  identical); recorded counters-only bound 1.02528 reproduced to 8e-05.
  Diagnostics only; binding gates require re-frozen artifacts; the full
  campaign protocol, and the gates 8/9 legacy comparators.
- Gates 8 and 9 measured and PASSING (`results/phase7/gate89/`, bench
  committed at `ea6bf7f`): legacy corpus comparator on the 33-board
  subcorpus (ASan-clean identity) gives non-T time-per-parent worst
  0.2399 against the 1.00 bar and T per normalized semantic candidate
  0.0422 against the 1.02 bar, plus the earlier Reference A half
  (0.0928). Plan items 8 and 9 are now proven; both verdict documents
  updated.
- Hotspot attribution (`docs/phase7/hotspot_attribution_d.md`):
  materialize/transposition 54.3 percent of run (probe 46.9 percent of
  cycles), canonicalization ~18 percent, eval ~8 percent; kernel BFS
  near zero. Count-safe accelerations committed (`e055cf5`: word-wise
  hash, cells caching; count identity 37/37 exact; paired totals
  post/pre 0.7961).
- Gate-3 partition instrument implemented and run
  (`results/phase7/partition/`, design in
  `count_partition_instrument_design.md`): on the timed workload the
  entire per-input semantic delta is class (a) new-legal candidates
  proven by live oracles (seed 1: +395,300; seeds 2/3 repeat), zero
  defects on both legs across all seeds, exact integer accounting
  independently rechecked, and the campaign count deltas fully
  reconciled into semantic partition plus exactly-counted volume terms
  (legacy re-walk asymmetry: 238,654 vs 117,589 parents).
