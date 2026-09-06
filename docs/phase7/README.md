# Phase 7 record: profile migration

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
