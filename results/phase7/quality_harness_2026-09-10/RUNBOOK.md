# Quality-evidence runbook: value engine versus legacy engine

Date: 2026-09-10.
Authority: owner directive to proceed with the quality-first recommendation in `docs/phase7/decision_request_2026-09-09.md` section 6, option A.
Status: protocol frozen before any collection. This runbook defines the harness change, the verification gates, the pre-declared criteria, and the staged schedule. No quality number collected under any earlier command counts.

## 1. Scope

One question: at equal 20 ms per-move budgets, is the value engine non-inferior to the frozen legacy engine in played games, and is any superiority worth the measured 4 percent speed premium?

This campaign measures gameplay quality only. It does not re-run performance gates, does not change production, does not touch frozen artifacts, and does not authorize cutover, deletion, or qualification. The performance record stands: four `NO-ADVANCE` trials, binding ratio 1.04155, and this campaign is the input to deciding whether plan item 9 is amended or the migration stops.

## 2. Harness facts today

- `src/tuner_match.h` drives `TunerEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>`, the legacy engine, for both players. Verified at line 62 and through `BotInstance` (line 114), whose `run()` calls the legacy `ai.run_hold` with `search_budget` and consumes `Scenario` pieces.
- `tuner vs` (`src/tuner.cpp:646`) already implements: seat-swapped pairs (job `2j` and `2j+1` swap the two parameter arrays over identical per-seat scenario seeds), equal budgets per seat (`search_ms` or `iters`), a thread pool with a two-threads-per-pair semaphore, winner by survivor then APL, Wilson 95 percent CI on win equivalence, and APP/APL/lines/deaths/T-spin/perfect-clear accounting. It compares two PARAMETER files with the one built-in legacy engine. It is a parameter quality tool, not an architecture comparison tool.
- `src/migration_compare.cpp` replays the same match machinery for determinism auditing (two identical runs, zero mismatches required) and writes per-pair CSV rows. Its engine labels are hardcoded `legacy,legacy`.
- `MatchOutcome.replay_failures` is hardcoded 0 in tuner matches; legacy paths are not replay-validated in match play.
- The value engine drives games through `src/profile_value_runner.h`: the five-step move loop with per-move budget, canonical-spawn finalization, lockout death, rule-API application, and replay validation, over `Engine` (`src/tetris_engine.h`) with the `ai_zzz::TOJ` policy over the value Board and `src/toj_pathfinder.h` paths. It allocates a bounded arena and tables (`mem_retained_bytes=266338276` at production capacity, mostly reserved virtual space; observed RSS is far smaller but scales with play).

## 3. Harness change: one per-seat engine abstraction

Minimal, test-only, no production source changes.

New diagnostic target `quality_match` plus a small header. The battle rules that are already engine-independent in `BotInstance` (combo, b2b, attack table, garbage injection via `scenario_hole`, perfect-clear detection, death on spawn-lock or lockout, winner/APL selection in `play_match`) stay exactly as they are. The change extracts only the per-move engine call behind a seat backend:

- `LegacySeat`: the existing `BotInstance` path, unchanged: legacy `TetrisEngine` with `run_hold`, `ai.status()` fed from shared battle state (combo, under attack, b2b, t-values), budget `SearchBudget{ms}` or `by_iterations`.
- `ValueSeat`: the value `Engine` plus policy plus pathfinder, driven per move by the same `profile_value_runner` move-loop logic, fed the SAME shared battle state through `EngineConfig` policy inputs, consuming current piece, next window, and hold from the shared `Scenario`, applying the chosen candidate through the rule API, and returning per move: clear count, T-spin class (mini, full, none), hold consumed, spawn-blocked or lockout death, and a replay-validated command path. `replay_failures` counted per seat and per pair; any failure is recorded, never hidden.
- Each `MatchJob` gains two engine-id fields (`engine1`, `engine2`: `legacy` or `value`). The scenario seeds, budgets, parameter arrays, and accounting stay identical per seat. Seat-swapping swaps engine ids together with parameters, preserving the existing job structure.
- Budget mapping: both seats receive the same numeric budget in the same mode. The 20 ms mode maps legacy `SearchBudget{ms}` and the value engine's wall-clock budget; the iters mode maps `by_iterations` and the value `iters` budget. No other mode is permitted in this campaign.
- Determinism note: in iters mode both seats are deterministic. In 20 ms mode both seats are wall-time sensitive and move choices may legitimately differ between repeats; that is expected and is not nondeterminism for gating purposes (see section 5).

## 4. Verification gates (all must pass before any quality number counts)

1. Legacy-parity: `quality_match` with both seats `legacy`, iters mode, on a fixed 512-pair seed block, must produce zero nondeterministic pairs on an immediate identical rerun, matching the `migration_compare` standard, and its per-pair rows must match `migration_compare` output on the same seeds field-for-field (engine labels excepted).
2. Value self-parity: both seats `value`, iters mode, 128 pairs, immediate rerun with zero mismatches.
3. Shared-scenario identity: in every mixed pair, both seats must consume identical piece sequences and identical garbage-hole decisions per round. Verified by per-round digest in a 32-pair instrumented run.
4. Accounting unit checks: attack values from the shared table, b2b, mini/full T-spin classification, perfect-clear bonus, and garbage placement must produce identical battle state for the same move sequence in both seat types on directed fixtures.
5. Memory: with the maximum campaign concurrency, total process set must stay far below physical RAM. The value seat is production-configured (arena capacity unchanged; no semantic reduction). Mixed-pair concurrency is capped at 8 concurrent pairs (16 engines, one value per pair) with recorded RSS sampled during the smoke screen; legacy-only stages may use higher concurrency. A memory kill during any stage is a stage `FAIL` with the concurrency recorded, and the next attempt must lower concurrency; that failed stage consumes its invocation.
6. The normal value candidate binary and the frozen baseline remain byte-identical to their recorded hashes throughout; the harness links sources, it does not modify them.

## 5. Pre-declared criteria (frozen before collection)

Binding budget: 20 ms per move per seat, both seats equal, max rounds 3600, garbage and scenario generation exactly as in the existing tuner match. The iters mode is diagnostic only and never produces a criterion input.

Final campaign: 1000 fresh seat-swapped pairs (2000 games) with fresh seeds, never reused from screens.

Non-inferiority, all three required, computed on the 2000 final games:

- WR: the Wilson one-sided 95 percent lower bound for the value engine's win-equivalence rate must exceed 0.47 (three points below even), using z = 1.6449 (one-sided 95 percent; the tuner's existing two-sided z = 1.96 interval is reported for context only and is not the criterion).
- APP: the value engine's mean attack per piece must be at least 0.97 times legacy's (relative lower bound above minus 3 percent).
- APL: the value engine's mean attack per line must be at least 0.97 times legacy's.

Budget-scope symmetry is verified, not assumed: both seats bound the search or root-search call with setup and path outside, and the smoke CSV records per-seat per-move wall distributions so a systematic scope skew above 10 percent is detected before the screen. If the value arena exhausts mid-game, the seat records exhaustion as a counted death with the counter reported, never silent. Adding engine-id fields to the shared MatchJob defaults both ids to legacy, and the post-change rerun of migration_compare must show zero mismatches to prove the existing tools behavior-identical. Eight concurrent pairs oversubscribe 15 usable CPUs with 16 threads; the oversubscription is recorded in each MACHINE.md and accepted because it is symmetric within every pair.

Superiority readout, reported but explicitly non-gating: value win-equivalence point estimate and its two-sided 95 percent CI. Superiority does not gate any stage; it feeds only the final decision mapping.

Analysis precision: exact integer counts with doubles only for ratios; criteria evaluated at full double precision.

## 6. Staged schedule and advancement

Three stages, each one fail-closed collector invocation with fixed budgets and no extension:

1. Smoke screen: 32 pairs (64 games), 20 ms binding budgets, mixed seats both orientations, plus the section 4 verification gates 1 through 3 at this scale. Advance on sanity only: zero replay failures, determinism in iters-mode parity runs, both seats win some games is not required, and no crash or memory kill. Any failure stops the program here for repair review.
2. Screen: 128 pairs (256 games), 20 ms budgets. Powers the go/no-go for the final campaign. Advance if: zero replay failures, no memory kill, WR point estimate not below 0.40, and APP and APL not below 0.90 relative. These go/no-go numbers exist to catch pathology cheaply; they are not the campaign criteria.
3. Final campaign: 1000 fresh pairs (2000 games), 20 ms budgets, fresh seeds. Its outcome maps one-to-one to pre-declared decisions:

- Non-inferior AND superiority CI entirely above even: the quality premium justifies the speed cost; amends plan item 9 to at most 5 percent slower, and the migration proceeds to qualification with row fusion adoption considered separately.
- Non-inferior but not superior: reported to the owner as an owner judgment between amending item 9 and stopping; neither is automatic.
- Non-inferiority failed on any criterion: stops the migration per option D; the legacy engine stays in production.

## 7. Machine controls

- CPU 15 offline for the duration of each stage, restored online after, including every abort path; the campaign is throughput-bound and fair-budget play needs a quiet sibling.
- Load5 below 1.5 before each stage's prewarm; if at least 2.0 before a stage, wait up to ten one-minute rechecks.
- Threads: fixed per stage and recorded; smoke 8, screen 8, final 8 concurrent pairs. No dynamic adjustment.
- Seeds fixed per stage and recorded in the manifest before execution: smoke 910, screen 911, final 999.
- No other intensive process during a stage; check load and process table before each stage.

## 8. Evidence and preservation

- Per-stage directories `results/phase7/quality_harness_2026-09-10/{smoke,screen,final}/`, each with: `matches.csv` (per-pair rows: pair, seeds, engine1, engine2, winner, reason, rounds, deaths, capped, attack, pieces, lines, tspins, perfect clears, replay failures, per-seat engine ids), `MACHINE.md`, `MANIFEST.txt`, `summary.json`, and `verdict.md`.
- Collectors are single-invocation and fail-closed: an existing `matches.csv` or log is `BLOCKED`; nothing is deleted, truncated, replaced, or re-rolled; a crash is a stage `FAIL` with partial evidence preserved.
- Deterministic reruns happen only as the section 4 parity check, never to replace a collected stage.
- Hashes recorded: harness binary, both engine-linked binaries or source HEAD, parameter files, seeds, and the CSV itself at finalization.
- Parent independently reconciles criteria arithmetic, CSV hashes, seed derivations, and machine controls before any verdict is accepted, with a fresh read-only review.

## 9. Claim discipline

- Smoke and screen results are diagnostic; no superiority, non-inferiority, or quality claim may be cited from them.
- The 20 ms mode is the binding budget; fixed-iteration results support determinism and parity only.
- No claim may extrapolate from 20 ms play to other budgets or to the profile protocol's fixed-iteration numbers.
- Only the final campaign maps to the section 6 decisions; the migration's performance record (1.04155, four `NO-ADVANCE` trials) is unchanged by any quality outcome and is reported alongside it.
- Production remains the legacy engine throughout; this campaign changes nothing automatically.
