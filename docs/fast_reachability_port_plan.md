# Fast Reachability Port and Core Refactoring Plan

## 1. Purpose

This document defines the implementation plan for replacing the current pointer-network reachability stack with the pinned fast-reachability kernel while restructuring the engine around value types and explicit contracts.

The repository currently ignores `/docs` in `.gitignore`. This plan file is therefore present in the working tree but hidden from ordinary `git status` until the ignore rule is narrowed or the file is force-added. The first implementation commit must narrow the rule and explicitly stage this plan plus later benchmark reports so they are actually versioned.

This is a clean port, not a compatibility migration. The implementation must discard source structures and logic that do not fit the target architecture. Any old line that conflicts with the target contracts is deleted rather than wrapped. The only compatibility requirements are the exported `TetrisAI` ABI, its coordinate conversion, the accepted path command protocol, and the existing 29-parameter TOJ policy file format.

Fast reachability is expected to discover legal placements that the old search misses. Equal iteration counts may therefore produce different candidate totals and selected moves. This is acceptable when structural, replay, performance, and gameplay-quality gates pass.

The final implementation must satisfy all of the following:

1. Use `reachability::search::binary_bfs` as the placement reachability kernel.
2. Add a new pathfinder that produces replay-valid command paths.
3. Remove `TetrisNode`, `TetrisOpertion`, the current `TetrisContext`, `TetrisMap`, `TetrisMapSnap`, node marks, the precomputed placement graph, and `search_tspin`.
4. Use a canonical 10 by 48 board with no persistent `top`, `width`, `height`, or `count` members.
5. Keep only an incrementally maintained `roof` beside the packed occupancy.
6. Remove `disable_d`, 20G production support, `allow_nont_d`, `allow_rotate_move`, `last_rotate`, and the `X/Z/C` rotate-and-move paths.
7. Preserve observable TOJ T-spin behavior using explicit arrival metadata and rule logic rather than pointer history.
8. Preserve the exported ABI and accepted path commands: `v`, `l`, `r`, `d`, `L`, `R`, `D`, `z`, `c`, `x`, and terminal `V`.
9. Be no more than 2 percent slower than the frozen baseline under the agreed `tetris_profile.cpp` protocol.
10. Meet the agreed 3 percent non-inferiority margins for WR, APP, and APL.

## 2. Locked decisions

The following decisions were confirmed during planning and are not open implementation questions.

| Area | Decision |
|---|---|
| Board geometry | One canonical 10 by 48 physical and logical board |
| Canonical internal spawn | Fast-reachability anchor `(4, 20, 0)` |
| External interface | Preserve the current exported ABI and path alphabet |
| T-spin behavior | Preserve observable TOJ mini and full spin behavior |
| Production movement modes | Soft drop, sonic drop, CW, CCW, and optional 180 rotation |
| Removed movement modes | 20G, `allow_nont_d`, `allow_rotate_move`, `last_rotate`, `X/Z/C`, and `disable_d` |
| Wall commands | `L/R` remain accepted path commands and may be emitted as replay-proven path compression, but they are not reachability modes |
| Policy height constants | Keep TOJ heuristic and safety math based on a named policy height of 40 |
| Lockout | A placement is dead when its lowest occupied mino row is at least 20 |
| Kernel revision | Start from fast-reachability commit `0c35e13` on `avx512-perft` |
| Kernel modification | General and reusable higher-level additions to fast-reachability are allowed |
| Performance margin | Five paired profile repetitions, no more than 2 percent regression |
| Quality margin | Final 1,000 seat-swapped pairs, 3 percentage point WR margin, 3 percent relative APP and APL margins |

The 48-row board is storage and reachability space. The TOJ policy horizon remains 40. These are separate named concepts and must never be represented by an ambiguous literal `40` or `48` inside new code.

## 3. Current architecture and reasons for replacement

### 3.1 Precomputed pointer graph

`TetrisContext::prepare` constructs every piece pose and links moves, rotations, wall kicks, drops, and landing columns into an immutable pointer graph in `src/tetris_core.cpp:264-511`.

`TetrisNode` stores geometry, cached column bounds, graph indexes, movement pointers, kick pointer arrays, and a 40-entry drop table in `src/tetris_core.h:185-255`. Search then follows these pointers in `src/search_tspin.cpp`.

This architecture must be removed because:

- Pose identity is tied to pointer lifetime.
- Rule geometry is materialized into thousands of runtime nodes.
- Search behavior depends on `index`, `index_filtered`, `land_point`, `low`, and prelinked movement state.
- The same rule data is represented several times through operation functions, node geometry, direct rotation pointers, and wall-kick arrays.
- The design prevents fast-reachability from being the canonical representation.

### 3.2 Runtime operation table

`TetrisOpertion` stores creation and rotation function pointers plus kick arrays in `src/tetris_core.h:166-182`. `rule_toj.cpp:24-377` constructs one operation object per piece orientation.

The pinned fast-reachability library already expresses pieces and SRS kicks as compile-time data in:

- `src/fast-reachability/piece_tetromino.hpp`
- `src/fast-reachability/kick_srs.hpp`
- `src/fast-reachability/block.hpp`

The runtime operation table has no place in the target architecture.

### 3.3 Board metadata duplication

`TetrisMap` stores `row[40]`, `top[32]`, `width`, `height`, `roof`, and `count` in `src/tetris_core.h:38-86`.

The duplicated fields are used as follows:

- `top` accelerates drop and supplies one AI feature.
- `width` and `height` are runtime fields even though the remaining rule is fixed to 10 by 40.
- `count` is used primarily for perfect-clear detection.
- `roof` bounds hashing and feature scans.

The target board makes width and height compile-time constants, derives column tops only when needed, uses `roof == 0` for emptiness, and keeps only `roof` as persistent derived metadata.

### 3.4 Legacy reachability and path coupling

`search_tspin::Search` combines four responsibilities:

1. Placement enumeration.
2. T-spin arrival tracking.
3. T-spin classification helpers.
4. Path reconstruction.

Its `disable_d` branch at `src/search_tspin.cpp:140-452` suppresses soft and sonic drop transitions, runs a special first pass, and retries with a broader traversal. A second open-stack fast path at `src/search_tspin.cpp:479-607` depends on `TetrisNode::land_point`, `low`, `open`, and `drop`.

Both paths must be deleted. The target design uses one binary reachability contract and a separate pathfinder.

### 3.5 Engine coupling

The current generic engine discovers interfaces through function arity and return-type traits in `src/tetris_core.h:400-489`, derives a landing type from `Search().search(...)` in `src/tetris_core.h:719-733`, and assumes landing objects are pointer-like throughout tree expansion.

The engine also combines:

- Board transition.
- Evaluation cache lookup.
- Hold branching.
- Tree allocation and reuse.
- Widening and pruning.
- Search dispatch.
- Path dispatch.

The target engine keeps the useful search policy behavior but replaces the type discovery and pointer assumptions with direct, explicit APIs.

## 4. Reference findings and how they affect this port

### 4.1 Bundled fast-reachability kernel

The pinned submodule already provides the core operations needed by the port:

- Packed board: `board_t` in `src/fast-reachability/board.hpp:30-43`
- Line clearing: `clear_full_lines` and its cleared-row mask in `src/fast-reachability/board.hpp:312-370`
- Derived height and columns: `highest_y` and `column_tops` in `src/fast-reachability/board.hpp:456-474`
- Piece definitions and orientation offsets: `piece_tetromino.hpp:29-93`
- SRS kicks: `kick_srs.hpp:8-46`
- Bit-parallel reachability: implementation and public wrapper in `search.hpp:205-465`
- Scalar legality checks over the same geometry: `move_checker` in `search.hpp:476-572`

The existing submodule tests passed at the pinned revision for all eight perft vectors in `src/fast-reachability/bench.cpp:11-29`.

### 4.2 Reference A: `/home/icly/Documents/GitHub/tet`

Useful concepts:

- Value placements instead of node pointers.
- Separate candidate arrival metadata.
- One prepared fast board per expanded parent.
- Replay validation for every generated path.
- Rule classification after search rather than inside a pointer BFS.
- Early semantic deduplication before expensive AI transitions.
- Path generation only for the selected root move.

Patterns that must not be copied:

- Converting a second project board into `board_t` for every search. The new repo board will use `board_t` directly.
- A second scalar traversal after `binary_bfs` to recover T arrival semantics.
- Exact predecessor and rotation-direction candidate multiplication.
- Dijkstra pathfinding for every candidate.
- Persistent `top` and `count` board metadata.
- Stateful spans backed by hidden search-owned vectors.

Reference A's recorded measurements in `/home/icly/Documents/GitHub/tet/docs/performance-comparison-tetris-profile.md` show raw binary BFS in tens to hundreds of nanoseconds while its T wrapper reaches tens of microseconds. That audit identifies scalar T arrival traversal and candidate multiplication as major costs. This port must solve T arrival semantics in the bit-parallel layer rather than reproduce that wrapper.

### 4.3 Reference B: `/home/icly/Downloads/tetris-ai-main`

Useful concepts:

- Direct `binary_bfs` dispatch per piece.
- Iterating bitboard landing positions with `for_each_bit`.
- Visitor-style child generation.
- Keeping rule outcome and evaluation outside the BFS kernel.

Limitations:

- Its fast-reachability submodule directory is present but empty locally, so the exact pinned API and movement behavior are unverifiable.
- It has no executable path reconstruction.
- It has no reachability unit tests or benchmark target.
- It uses a different 10 by 30 board and demo-specific state counters.

Reference B is architectural evidence only. It is not a source of authoritative code or constants.

## 5. Target dependency structure

The target dependency direction is:

```text
tetris value types
    -> fast-reachability
    -> canonical Board wrapper
    -> TOJ rule and reachability adapter
    -> TOJ policy and pathfinder
    -> engine
    -> DLL, match, profile, and tuner harnesses
```

Dependencies must not point back upward. In particular:

- The kernel must not know TOJ attack rules, lockout, hold, the AI policy, or the exported DLL alphabet.
- The board must not know the engine tree.
- The reachability adapter must not call AI evaluation.
- The policy must not know path commands.
- The pathfinder must not run during tree expansion.
- Harnesses must not inspect search internals.

## 6. Proposed final modules

| Proposed file | Responsibility |
|---|---|
| `src/tetris_types.h` | Piece aliases, packed placement, arrival class, candidate, spin, outcome, move enum, search request types |
| `src/tetris_board.h` | Canonical 10 by 48 occupancy plus exact `roof` invariant and board transition helpers |
| `src/toj_rule.h` and `src/toj_rule.cpp` | Spawn, geometry dispatch, lockout, T-spin classification, placement mask, clear outcome. This is the clean replacement for rule-specific parts of both `rule_toj` and `search_tspin`. |
| `src/reachability_search.h` and `src/reachability_search.cpp` | Repo-level search configuration, workspace ownership, semantic candidate enumeration |
| `src/pathfinder.h` and `src/pathfinder.cpp` | Chosen-candidate path search, command compression, replay validation |
| `src/tetris_engine.h` and `src/tetris_engine.cpp` | Hold-aware anytime tree, explicit cache contracts, root reuse, budget handling, result selection |
| `src/ai_zzz.h` and `src/ai_zzz.cpp` | Rewritten TOJ policy over the new board, outcome, and decision context |
| `tests/fast_reachability_tests.cpp` | Kernel wrapper, arrival-channel, and scalar-oracle checks |
| `tests/tetris_board_tests.cpp` | Board transition, roof, clear, import, export, and garbage properties |
| `tests/toj_rule_tests.cpp` | Spawn, geometry, kick, lockout, line clear, perfect clear, and T-spin tests |
| `tests/pathfinder_tests.cpp` | Candidate-to-path and replay equivalence tests |
| `tests/toj_policy_tests.cpp` | Feature and transition fixture parity |
| `tests/engine_tests.cpp` | Hold, queue, budget, reuse, deduplication, and top-out behavior |
| `tests/migration_compare.cpp` | Temporary old-versus-new differential and gameplay harness, removed with legacy code |

Names may be adjusted during implementation if an existing project convention provides a clearly better fit. Responsibilities and dependency direction must remain unchanged.

## 7. Core data contracts

### 7.1 Piece

Use the fast-reachability tetromino type as the canonical internal piece identity or a one-byte project enum with a compile-time one-to-one mapping. Do not retain runtime character-index maps.

Characters are converted only at external boundaries through the tetromino rule data.

An empty hold is represented with `std::optional<Piece>` rather than a space character sentinel.

### 7.2 Placement

Use a trivially copyable 16-bit packed placement containing:

- `x` in the 10-column anchor space.
- `y` in the 48-row anchor space.
- Rotation in the four-state SRS space.

A placement is a value. It does not contain a piece pointer, movement pointers, cached geometry, or an attachment method.

### 7.3 Arrival class

The engine needs semantic arrival information, not exact path history.

Use:

```text
ArrivalClass::Normal
ArrivalClass::TerminalRotation
```

For a T placement, search may emit at most one candidate for each reachable arrival class. For non-T pieces, emit only `Normal`.

Rotation direction and predecessor pose are pathfinder details. They must not multiply tree candidates or enter AI evaluation keys.

### 7.4 Candidate

A candidate contains only:

- Final packed placement.
- Arrival class.

Piece identity is supplied by the expansion request and is not duplicated in every candidate.

### 7.5 Outcome

A rule outcome contains:

- Spin type: none, mini, or full.
- Clear count.
- Lockout state.

Perfect clear is derived from the resulting board being empty. It is not stored as board metadata.

### 7.6 Decision context

Replace `TetrisContext::Env` with an explicit policy input containing:

- Remaining next pieces as a span.
- Optional hold piece.
- Whether this branch used hold.
- Search depth.

This removes the AI dependency on engine internals.

## 8. Canonical board design

### 8.1 Representation

The canonical board is:

```text
Occupancy: reachability::board_t<10, 48>
Roof: unsigned value in the range 0 through 48
```

The board has compile-time constants for width and height. It has no persistent column tops or block count.

The occupancy is private so every mutation can preserve the `roof` invariant. Hashing and equality must use `board_t`'s logical packed data or a kernel-provided logical hash, not raw object bytes or padding.

### 8.2 Required operations

The board API must provide:

- Empty construction.
- Import from 48 bottom-origin row masks.
- Import from the exported 22-row field plus 8-row overfield format.
- A single-row query.
- Cell occupancy query.
- Packed occupancy access for reachability.
- Exact equality and hashing over occupancy.
- `roof()`.
- `empty()` using `roof == 0`.
- Derived `column_tops()` for policy evaluation only.
- Apply a validated piece mask.
- Clear full lines.
- Add garbage rows.
- Debug-only invariant validation.

### 8.3 Roof maintenance

Land the initial Board implementation with `highest_y()` as the production update and invariant oracle, collect its component timing, and establish correctness first. Enable the incremental paths below only after property tests pass and profiling shows they are beneficial. Keep a debug cross-check against `highest_y()` after every mutation.

For a placement with no clear:

```text
roof = max(old_roof, highest_occupied_row_of_piece + 1)
```

For a placement with one or more clears, update roof incrementally from `pre_clear_roof = max(old_roof, highest_occupied_row_of_piece + 1)` and the cleared-row mask returned by `clear_full_lines`. Start at row `pre_clear_roof - 1`, skip cleared rows, and scan downward only until the first surviving occupied row. Subtract the number of cleared rows below that survivor to obtain its compacted height. Because four lines can clear at most, this scan normally touches only the cleared top fringe. Use `highest_y()` as the debug oracle and measured fallback during initial bring-up.

For garbage insertion, rows are shifted upward by the inserted count and the known garbage masks occupy the new bottom rows. Compute the candidate roof from the shifted old roof and highest inserted row, then scan only a clipped top fringe when rows 48 and above are discarded. A full-height shift or top clipping may use a measured `highest_y()` fallback.

`highest_y()` scans a fixed small number of packed words and remains the invariant oracle. It must not be the unconditional hot-path implementation if benchmark telemetry shows measurable cost. No `top` reconstruction is permitted.

### 8.4 Row and column views

AI evaluation converts occupancy to a local row array once per evaluation. All row features use that local array.

The six side-column heights used by `ai_zzz` are derived once with `column_tops()` during evaluation. They are not persisted in the board.

### 8.5 Policy horizon

Define a named constant `toj_policy_height = 40` in the TOJ policy module.

The board remains 48 rows. The following existing formulas remain based on 40 unless an independently benchmarked policy change is approved later:

- Column-transition top boundary.
- Row-transition top boundary.
- Field-value scaling.
- Hold and map-rise terms that currently use 40.

Visible-frame safety logic that currently uses 22 remains 22, and lockout logic uses the confirmed lowest-mino-row threshold 20. Section 12.3 inventories the remaining local bounds.

Live states above the policy horizon should not occur because the lockout threshold is lower. Tests must still make behavior explicit for imported or malformed high boards.

## 9. Rule and coordinate design

### 9.1 Canonical spawn and boundary conversion

Every standard piece spawns at fast-reachability anchor `(4, 20, 0)`.

The old `(3, 21, 0)` pose and the exported `(x, y, spin)` representation use a different geometry frame. The port must not assume that one constant translation works for every piece and orientation.

`TojRule` owns a compile-time `ExternalPoseTransform` table indexed by piece and external rotation. Each entry maps an exported active pose to the canonical fast-reachability anchor and provides the inverse mapping needed by fixture diagnostics. The table is generated from old and new occupied-cell offsets, stored as plain integer offsets, and contains no legacy node or status type.

Before production cutover, generate an exhaustive geometry table that compares occupied cells for:

- Every piece.
- Every rotation.
- Every valid horizontal position.
- Representative vertical positions including spawn and rows near lockout.

The external adapter is accepted only when old and new occupied-cell sets match for every comparable pose. The canonical spawn is a rule value, while external active-pose conversion is a boundary table. They must not be conflated.

### 9.2 SRS authority

The pinned `Tetromino` and `SRS_Kicks` definitions are the target rule source.

Differential tests must prove that first-valid kick results match the observable old TOJ rule for CW, CCW, and 180 rotations. The tests must explicitly include zero-kick direct 180 behavior and the empty or identity 180 table cases in both implementations, so the 180 check cannot pass vacuously. If a mismatch is caused by coordinate frames, fix the frame conversion. If a genuine SRS defect exists in the general kernel, fix it in the submodule with a general test. Do not add a legacy TOJ kick table beside the kernel table.

### 9.3 Lockout and spawn death

Compute the lowest occupied mino row from the selected piece shape and placement. The placement is terminal when:

```text
lowest_occupied_row >= 20
```

Do not use anchor `y`, bounding-box row, or `roof` as a substitute.

This is an intentional target semantic that replaces the old `node->row >= 20` checks in `src/match.cpp:472-475` and `src/ai_zzz.cpp:444`. The legacy fixture records those old results only to expose the expected changed cases. A dedicated orientation corpus must enumerate every case where anchor or bounding row disagrees with lowest occupied mino row, and all new consumers must use the target semantic.

Spawn obstruction is a separate death condition. If the active piece cannot occupy canonical spawn after hold and queue resolution, the state is dead before reachability enumeration.

Terminal candidates do not become expandable live children. The engine may retain a compact dead result for ranking parity, but it must never attach and continue a lockout state.

### 9.4 T-spin classification

T-spin truth belongs to the rule boundary and uses:

1. Piece is T.
2. Arrival class is `TerminalRotation`.
3. Candidate came from the landable set, so downward movement is invalid by search invariant.
4. At least three center corners are occupied or outside the board.
5. Mini readiness is true only when no CW, CCW, or 180 rotation from the final pose can succeed: for each direction, the first kick result whose cells lie inside the board must be occupied. The legacy net's rotation pointers are exactly those first in-bounds kick results, so this rule reproduces the observable legacy classification, including at wall poses where a kick enables a rotation that an in-place rotation does not.
6. Zero-line spins become none.
7. A mini with more than one cleared line becomes full.

The rule does not add a second groundedness behavior beyond the landable-candidate invariant. Tests must assert that every classified candidate is landable and that directly supplied non-landable values are rejected as invalid input rather than reclassified.

The implementation must express these checks directly from the canonical board, placement, and fast-reachability geometry. It must not recreate `TetrisMapSnap`, `block_data_`, `x_diff_`, `y_diff_`, or pointer rotations.

A migration corpus must compare this classifier with `search_tspin::classify` for shared old placements before the old module is removed.

## 10. Fast-reachability extensions

Any submodule change must be generic, separately tested, and free of TOJ policy behavior.

### 10.1 Search result wrapper

Add a reusable higher-level search result or workspace that can expose:

- Reachable states per orientation.
- Landing positions.
- Normal-arrival landing positions.
- Terminal-rotation landing positions.
- A legality checker backed by the same usable-position data.

Extend the generic `search_config` only if the arrival-channel wrapper needs a reusable option. Its public movement flags remain `allow_180`, `allow_softdrop`, `allow_sonicdrop`, and `allow_20g`; the runner uses `allow_softdrop = true`, `allow_sonicdrop = true`, runtime `allow_180`, and always sets `allow_20g = false`. No production 20G mode remains.

The kernel owns only generic movement options and height-specialized workspace. The repo adapter owns TOJ spawn selection, piece dispatch, semantic candidate conversion, occupancy canonicalization, and production-mode filtering.

The wrapper must accept caller-owned workspace storage or return a value with explicit ownership. It must not expose spans backed by hidden mutable global state.

### 10.2 Dynamic height selection

Use the existing `call_with_height` mechanism with the pinned cutoffs 6, 12, 24, and 48. The repo adapter computes the occupied height from `Board::roof()`, adds the kernel's safety margin used for height cutting, and dispatches the generic workspace. For each compile-time piece block, compute `necessary_height = spawn_y + downmost_position<B>` and select `check_consecutive` exactly from occupied height versus that requirement, following `perft.hpp:24-38`.

These cutoffs and the consecutive-row rule remain generic kernel policy. TOJ supplies only canonical spawn and Board roof. The common low-board path should avoid the more expensive consecutive-row check. High-board correctness must still use it when required.

### 10.3 Two-channel arrival propagation

Do not run a scalar pose BFS after `binary_bfs` to classify T arrivals.

Add a generic bit-parallel propagation mode with two state channels per orientation:

- The last successful command was not a rotation.
- The last successful command was a rotation.

A kicked rotation is still a rotation command even when its first-valid SRS result changes `x` or `y`. The arrival class describes command semantics, not whether occupied cells translated.

Transitions update channels as follows:

- Left, right, soft drop, sonic drop, and wall movement enter the non-rotation channel.
- CW, CCW, and 180 rotation commands enter the rotation channel.
- Rotation transitions may originate from either channel.
- Future non-rotation movement after a rotation moves the state back to the non-rotation channel.

For every source channel, a rotation must compute the same first-valid destination mask as ordinary `binary_bfs`: kick alternatives are applied in table order, and a source state contributes to only its first legal kick result. Destination suppression must be derived from source legality and kick order before channel union. One channel must never mask or steal the first-valid result of the other channel.

At convergence, intersect both channels with landable positions. This directly yields the two semantic candidate classes without exact predecessor multiplication.

The implementation must share usable masks, ordered kick application, and fixpoint machinery with `binary_bfs`. It must not duplicate the full reachability algorithm into a repo-specific file. The required scalar-oracle equality gate applies to nonzero `dx` and `dy` kicks explicitly, including cases where the two channels reach the same source pose through different histories.

### 10.4 Independent scalar oracle

Add a slow, test-only scalar BFS over `(x, y, rotation, arrival_class)`.

Use it to verify the generic two-channel bit-parallel result on seeded random boards and all movement configurations supported by the generic kernel. The scalar oracle must never be linked into production targets.

### 10.5 Candidate reduction

The repo adapter converts the kernel's shape-indexed reachable masks into canonical four-state placements. It owns any alias expansion required by `block.hpp` when a piece has fewer unique shapes than SRS orientations, then canonicalizes by occupied-cell mask.

Apply these rules:

- T: at most one normal and one terminal-rotation candidate per physical placement.
- I, S, and Z: canonicalize duplicate orientation representations by occupied-cell mask.
- O: one physical orientation.
- J and L: one candidate per physical placement.
- Non-T terminal rotation history is ignored because it does not affect rule or policy behavior.

Within one parent expansion for one chosen piece source, if two candidates produce the same resulting occupancy, spin, clear count, and lockout result, evaluate and materialize that transition once. Do not deduplicate across current-piece and hold branches when their consumed queue index, resulting hold, hold-lock state, or first-move identity differs.

Within one depth, transposition equivalence is exact occupancy plus complete policy state, active piece, optional hold, hold availability, queue cursor, and remaining virtual-next metadata. There is no global cross-depth deduplication. Any pruning of equivalent values must preserve the earliest root-move attribution needed for final selection.

## 11. New pathfinder

### 11.1 Scope

The pathfinder runs only after the engine selects a root move. It is not part of placement enumeration or child expansion.

Input:

- Canonical board.
- Piece.
- Actual start placement.
- Selected candidate.
- `allow_180`.

Output:

- Command sequence excluding the final `V`.
- Replay result containing final placement and arrival class.
- Validity flag.

### 11.2 State and storage

Use a fixed state space for 10 by 48 and four rotations, with an arrival-class bit where required.

Use fixed-capacity arrays for:

- Visited state.
- Parent state.
- Parent command.
- Queue storage.

Do not use `std::priority_queue`, per-call hash maps, or per-candidate dynamic allocation.

A normal breadth-first search is sufficient when every emitted host command has unit cost.

### 11.3 Commands

Primitive path transitions:

- `l`
- `r`
- `d`
- `z`
- `c`
- `x` when 180 is enabled
- `D` for a sonic drop that does not lock

`L` and `R` are path-only wall commands. They may be represented as BFS edges or introduced by a replay-proven compression pass.

The pathfinder must not emit `X`, `Z`, or `C`. The shared replay interpreter rejects every unknown or removed command. It must never ignore an unrecognized byte as the current `match.cpp` default branch does.

The external adapter adds `v` before the piece path when hold changes and appends `V` after the path.

### 11.4 Goal semantics

For a normal candidate, the last successful movement before `V` must be non-rotation or absent, and `V` must hard-drop to the selected placement. If the piece is already at the selected placement, an empty pre-`V` path is valid.

For a terminal-rotation candidate, the last successful movement before `V` must be a successful CW, CCW, or 180 rotation and `V` must not move the selected already-landable placement. A hard drop that changes `y` would erase terminal-rotation semantics under the preserved protocol and is invalid for this candidate class.

The pathfinder may choose any predecessor and rotation direction that satisfy the semantic candidate. Candidate identity does not include that witness.

### 11.5 Replay validation

Every path is replayed through the production interpreter built on the same generic legality and kick routines used by search. Correctness tests also replay the same paths through an independent row-and-cell scalar SRS interpreter that does not call `move_checker`, `binary_bfs`, or the production pathfinder. The scalar interpreter is implemented from the published SRS transition tables and hand-authored expected cases, not copied from `rule_toj.cpp`. This prevents either the legacy code or a shared kernel bug from validating itself.

A valid path must prove:

- Every command is legal.
- First-valid kick behavior is respected.
- Final hard drop produces exactly the candidate placement.
- Replayed arrival class equals the candidate arrival class.
- Lockout behavior is unchanged.
- The output fits the 1,024-byte exported buffer including optional `v`, `V`, and null terminator.

During development, a replay mismatch is an assertion and test failure. Production code fails closed and records telemetry. It must not silently substitute an unverified path.

## 12. TOJ policy rewrite

### 12.1 Preserved policy contract

Preserve:

- The 29 parameters and their order.
- Production default values.
- Binary parameter file format.
- Evaluation formulas and arithmetic order where practical.
- Combo, B2B, attack, hold, safety, T-slot, and death behavior.

Do not preserve old data structures or pointer-based signatures.

### 12.2 New policy interface

The rewritten policy receives explicit values:

- Piece.
- Candidate.
- Rule outcome.
- Source board.
- Result board.
- Parent policy state.
- Decision context.

It returns:

- Board evaluation result.
- New policy state.

The engine should call board evaluation once per unique resulting board and policy transition once per unique branch state.

### 12.3 Removed context dependencies

Replace current uses as follows:

| Current dependency | Replacement |
|---|---|
| `context_->full()` | Compile-time row mask |
| `context_->width()` | `Board::width` |
| `context_->height()` | Named TOJ policy height only at the existing 40-row boundary terms |
| `context_->convert()` | Compile-time piece conversion |
| `context_->generate()` | Compile-time piece geometry and canonical spawn |
| `node->status.t` | Explicit piece argument |
| `node->row` | `lowest_occupied_row(piece, placement)` |
| `node.type` | Explicit rule spin outcome |
| `map.count == 0` | `board.empty()` |
| `map.top[...]` | One derived `column_tops()` call |
| `TetrisContext::Env` | Explicit decision context |
| `TOJ::spawn` identity hook | Canonical `TojRule::spawn(piece)` |
| `AIHasSpawn` and `AIHasIterate` discovery | Direct concrete engine calls |

Do not collapse every old numeric height into one 40-row constant. Inventory and name the existing domains before editing formulas:

| Domain | Existing uses to preserve and test |
|---|---|
| Storage height 48 | Board storage, search workspace, imported high-row validation |
| Policy height 40 | Evaluation top boundary, field scaling, hold and safety scaling where the current formula uses 40 |
| Visible or spawn frame 22 | `get_safe` fallback and `map_in_danger_` indexing |
| Lockout threshold 20 | New lowest-mino-row death rule and T-slot scan bounds where 20 is algorithmic |
| Piece geometry height 4 | Danger-mask row offsets |
| Local scan limits 8, 10, 13, 19, and 23 | Existing hole, T-slot, queue, and danger formulas, each retained with a descriptive name or localized test |

The policy-fixture phase must cover each site in `src/ai_zzz.cpp`, including `init_t_value`, `map_in_danger_`, safety fallback, hold scoring, field scaling, and map-rise penalties. Only the storage representation changes to 48. Existing policy literals retain their observable arithmetic unless this plan explicitly names the changed lockout rule.

### 12.4 Feature computation

Convert the packed board to a local row array once in `eval`.

`init_t_value` and its overlays operate on local row arrays. They must not mutate a Board copy only to obtain feature values.

Compute the six side heights from derived column tops once per evaluation.

Generate danger masks directly from compile-time piece geometry rather than attaching a generated `TetrisNode` to an empty map.

### 12.5 Differential fixtures

Before rewriting the old policy interface, generate deterministic fixtures containing:

- Source rows.
- Piece and placement.
- Arrival class and expected spin.
- Clear count.
- Parent status.
- Next and hold context.
- Expected evaluation result.
- Expected resulting status.

For candidates shared with the old engine, the new policy must match fixture values exactly or within an explicitly documented floating-point tolerance caused only by intentional arithmetic restructuring.

Extra candidates found by fast reachability are validated by policy invariants and gameplay tests rather than old expected moves.

## 13. Engine redesign

### 13.1 Scope

Retain the useful policy of the current engine:

- Hold-aware expansion.
- Time and deterministic iteration budgets.
- Anytime widening.
- Depth-dependent beam pruning.
- Root reuse across turns.
- Bounded memory.
- Best first-move projection from deeper search.

Do not retain the template and pointer machinery used to express it.

### 13.2 Explicit components

Use one concrete engine composed from:

- `TojRule`
- `ReachabilitySearch`
- `Pathfinder`
- `ai_zzz::TOJ`

Do not retain the `TetrisEngine<Rule, AI, Search>` type-discovery framework unless a second real production rule or search implementation appears before this work begins.

Profiling counters belong in component telemetry rather than inheritance wrappers such as `ProfiledTOJ` and `ProfiledSearch`. Each component updates a plain `SearchTelemetry` value owned by the engine run. Harnesses receive a public snapshot of aggregate counters and timings after a move; they do not inspect workspaces, bitsets, caches, or tree internals.

### 13.3 Tree nodes

Tree nodes store values and compact indexes:

- Board.
- Policy state.
- Candidate that reached the node.
- Evaluation result or compact cache reference.
- Parent node index.
- Child range or child linkage by node index.
- Piece, hold state, next position, depth, and branch flags.

Do not store placement pointers, `TetrisNode` indexes, iterators into external next arrays, or pointers to cache slots that may be overwritten.

Use a stable arena with integer `NodeId` values. Frontier heaps and child references contain `NodeId`, not raw pointers.

### 13.4 Expansion pipeline

For each parent and current or hold piece:

1. Build one reachability workspace for the parent board.
2. Enumerate semantic candidates.
3. Apply lockout filtering.
4. Build and validate the piece mask.
5. Classify spin and clear outcome.
6. Apply the placement and clear lines.
7. Deduplicate within this parent and piece source by resulting occupancy, spin, clear, and lockout, while preserving distinct hold and first-move identities.
8. Look up board evaluation.
9. Apply the policy transition with parent state and decision context.
10. Apply the exact per-depth transposition key defined in Section 10.5, with no cross-depth deduplication.
11. Materialize the child node.

Pathfinding is absent from this pipeline.

### 13.5 Evaluation cache

The TOJ board evaluation depends on the resulting board. Use a bounded direct-mapped or small set-associative cache with:

- Explicit occupancy hash.
- Compact collision fingerprint or exact packed occupancy verification.
- Evaluation result stored by value.
- Generation management suitable for root reuse.

Do not hash raw object padding. Equality and hashing for policy state must list the same fields explicitly.

Benchmark cache disabled, direct-mapped, and small set-associative variants before choosing the final layout.

### 13.6 Queue semantics, virtual next, and root reuse

The existing `?` token marks a virtual boundary on the preceding active or next piece in `TetrisTreeNode::process_next`. Preserve parsing and queue identity for this external input without preserving `TetrisNext`, `AIHasIterate`, or pointer-tree machinery.

Represent the queue as explicit values:

- Concrete piece sequence.
- Queue cursor.
- Per-position virtual-boundary bit.

The current TOJ policy has no `iterate` method, so a virtual boundary is behaviorally inert for this production policy after parsing. Keep its metadata in root identity and queue advancement so the ABI input is not reinterpreted. Add fixtures for leading, interior, repeated, and trailing `?` markers. Reject a marker that has no preceding piece. If a future concrete policy needs boundary aggregation, add an explicit policy method then rather than retain dormant generic discovery.

Root matching uses:

- Exact board occupancy.
- Policy state.
- Active piece.
- Hold piece and hold availability.
- Queue cursor, remaining concrete next sequence, and virtual-boundary bits.

Derived `roof` is validated but not treated as independent identity because it is fully determined by occupancy.

### 13.7 Result and path materialization

The run result contains:

- Optional selected candidate.
- Selected piece.
- Updated policy state.
- Whether hold changed.

The engine materializes a path once, after final result selection. If the result came from an empty hold, the pathfinder starts from the correct next piece spawn after the `v` operation.

## 14. Harness migration

### 14.1 `src/ai.cpp`

Keep the exported function signatures and output buffer contract.

Replace:

- `prepare(10, 40)` with static engine initialization.
- Manual `TetrisMap` metadata rebuilding with Board import.
- Legacy search flags with `allow_180 = can180spin`.
- Legacy active-pose lookup with the `ExternalPoseTransform` table defined in Section 9.1.
- Context-generated hold spawn with canonical rule spawn.
- Legacy `make_path` with the new pathfinder result.

The function still writes optional `v`, the path commands, `V`, and `\0`.

Preserve the eight 1,024-byte result slots as the ABI storage model, but validate `player` before indexing so malformed input cannot write out of bounds. Preserve combo values, not the current static-once cache bug: copy or validate the sentinel-terminated combo table for each distinct input table, cap it at the configured array capacity, and add tests that call different players and different combo tables in one process. Parameter loading must likewise use the same explicitly pinned file for every compared engine.

### 14.2 `src/match.cpp`

Replace `Player::map` with the canonical Board.

Replace `apply_path` with the shared production path replay API. The match simulator must not maintain a second production interpretation of SRS kicks. The independent scalar interpreter remains test-only.

The match replay must reject unknown commands, use the target lowest-mino-row lockout rule, and keep spawn obstruction as a separate death check. Add cases where the old bounding row and new lowest mino row disagree.

Replace context uses with:

- Compile-time piece list for bag generation.
- Compile-time row mask for garbage.
- Board garbage insertion.
- Canonical spawn.

Keep match attack, recovery, APL, APP, winner, received-attack accounting, cancellation order, garbage-hole generation, and telemetry behavior unchanged.

### 14.3 `src/tetris_profile.cpp`

Update the profile harness before the production cutover so it can compare both engines with the same metrics.

Add explicit telemetry for:

- Parent expansions.
- Raw kernel landings.
- Unique semantic candidates.
- Rule transitions.
- Board-evaluation requests, hits, and misses.
- Policy transitions.
- Materialized tree nodes.
- Completed widening iterations.
- Selected-path states expanded.
- Selected-path time.
- Replay failures.

Correct the current `tree node pool growth (new allocs)` label because it reports a memory-usage byte delta, not an allocation count.

Version the machine-readable output before adding columns. Add `--quiet-version 2`, begin each record with a schema token, document the exact ordered fields, and keep version 1 unchanged for existing scripts. Version 2 carries the complete `SearchTelemetry` snapshot and explicit units.

### 14.4 `src/tuner_match.h` and `src/tuner.cpp`

Replace `TunerEngine`, `TetrisMap`, context spawn, attachment, and garbage metadata rebuilding with the new engine and Board APIs.

Keep:

- The 29-parameter contract.
- Deterministic scenario generation.
- Iteration and millisecond budgets.
- APP and APL definitions.
- Paired mirrored match behavior.
- Existing tuner checkpoint formats unless a separate tuner migration is approved.

Add a temporary engine-comparison mode that runs legacy and new engine factories inside the tuner simulator on the same paired scenarios until final acceptance is complete. This mode is the authoritative gameplay gate because it already owns deterministic scenario seeds and seat-swapped scheduling. It accepts an explicit engine selector per seat, pair count, budget mode, seed, worker count, and round cap, and writes one machine-readable row per seat-swapped pair.

Extend `MatchOutcome` or its replacement with total attack, pieces, lines, deaths, caps, rounds, T-spin classes, perfect clears, and replay failures so APP and APL can be recomputed as pooled ratios. Preserve `recv_attack`, cancellation, garbage-hole common-random-number behavior, and checkpoint parameter formats. The DLL `match` executable remains the ABI and path integration oracle, not the statistical-quality harness.

### 14.5 CMake

Create a header-only `fast_reachability` interface target for include propagation and compile features.

Create reusable project libraries instead of listing the same source files independently in every executable.

Recommended target structure:

```text
fast_reachability interface
    -> tetris_core_lib

tetris_core_lib
    -> tetris_ai_objects
    -> match
    -> tetris_profile
    -> tuner
    -> fast_reachability_tests
    -> tetris_board_tests
    -> toj_rule_tests
    -> pathfinder_tests
    -> toj_policy_tests
    -> engine_tests
    -> migration_compare

tetris_ai_objects
    -> tetris_ai shared library
```

Enable CTest and build the test targets in GCC and Clang debug and self-release presets. `migration_compare` exists only through final acceptance and is removed with legacy code.

Do not add a new third-party dependency.

## 15. Ordered implementation phases

### Phase 0: Freeze baseline and improve observability

Tasks:

1. Record root commit, submodule commit, compiler, flags, CPU model, CPU governor, and thread count.
2. Build frozen baseline binaries with `linux-gcc-self-release`.
3. Extend `tetris_profile` telemetry without changing engine behavior.
4. Measure telemetry overhead with the same instrumented binary and workload, counters disabled versus enabled, using the paired protocol in Section 17. Verify the median overhead is below 0.5 percent.
5. Save frozen profile and shared-library artifacts under a versioned external results directory, record SHA-256 hashes, and make the copies read-only.
6. Generate geometry, reachability, T-spin, policy, queue-marker, hold, garbage, and lockout fixture corpora from the legacy implementation.
7. Add independent spec-oracle cases that are not generated by legacy code, including hand-authored SRS kicks, row-array board transitions, and scalar replay paths.
8. Add the temporary migration comparison target and per-pair output schema.

Gate:

- Existing targets build.
- Existing behavior and profile work counts remain unchanged.
- Fixture generation is deterministic.
- Independent spec cases pass against the frozen baseline where semantics are meant to remain equal.
- Artifact, parameter, combo-table, compiler, CPU, and schema hashes are recorded before behavior changes.

Rollback:

- Revert instrumentation only. No production architecture has changed.

### Phase 1: Add canonical value types and Board

Tasks:

1. Add piece, placement, candidate, arrival, spin, outcome, move, and decision-context types.
2. Add the 10 by 48 Board using `board_t<10,48>` plus `roof`.
3. Add Board import, row view, equality, hash, apply, clear, garbage, and invariant functions.
4. Add property tests against a simple row-array oracle.
5. Add differential tests against legacy `TetrisMap` transitions on valid 10 by 40 states, extending expected storage to 48 rows.

Gate:

- Board tests pass under GCC and Clang debug.
- `roof` is exact after every seeded operation sequence.
- No new persistent `top`, `width`, `height`, or `count` field exists.

Rollback:

- New files are unused by production and can be removed independently.

### Phase 2: Extend the general fast-reachability layer

Tasks:

1. Add explicit workspace/result ownership.
2. Add dynamic board-height specialization and `check_consecutive` selection.
3. Add two-channel normal-versus-rotation propagation.
4. Add the test-only scalar oracle.
5. Add per-piece and per-mode microbench telemetry.
6. Add submodule tests and update the root submodule pointer.

Gate:

- Existing perft vectors remain exact.
- Bit-parallel arrival sets equal the scalar oracle on the full seeded corpus.
- The new wrapper does not regress raw non-T BFS by more than 2 percent.
- T semantic enumeration is at least 2 times faster than the Phase 0 frozen build of Reference A's scalar-wrapper design on the same board corpus and normalized candidate contract.

Rollback:

- Revert the submodule commit and root pointer. Phase 1 remains usable.

### Phase 3: Implement TOJ rule and reachability adapter

Tasks:

1. Implement canonical spawn and piece dispatch.
2. Prove old-to-new geometry conversion for every piece and rotation.
3. Prove SRS first-valid kick behavior on the differential corpus.
4. Enumerate semantic candidates from the kernel workspace.
5. Canonicalize duplicate I, S, and Z occupancies.
6. Implement lockout, T-spin classification, line clear, and perfect-clear detection.
7. Compare shared old candidates and independently replay every new-only candidate.

Gate:

- Every legacy physical placement has an equivalent new candidate unless a legacy-invalid placement is documented.
- New-only candidates are legal under the independent replay oracle.
- Shared T candidates preserve normal-versus-terminal semantics.
- Rule outcomes match fixtures for shared candidates.

Rollback:

- Production still uses the legacy engine.

### Phase 4: Implement and validate the pathfinder

Tasks:

1. Implement fixed-array BFS over pose and arrival state.
2. Add sonic-drop transitions.
3. Add optional wall-command edges or replay-proven compression.
4. Add CW, CCW, and conditional 180 transitions through `move_checker`.
5. Add reconstruction and external command mapping.
6. Add independent replay validation.
7. Run path checks for every candidate in the seeded corpus.

Gate:

- Every emitted candidate has a valid path from its tested start pose, and every terminal-rotation candidate reaches its final landable pose with rotation as the last successful pre-lock command.
- Replay produces exact placement and arrival class.
- No path uses removed commands.
- No path exceeds the exported buffer.
- Pathfinding occurs once per selected move in integration tests.

Rollback:

- Search and rule tests remain valid without production cutover.

### Phase 5: Rewrite `ai_zzz` against explicit values

Tasks:

1. Replace context and node arguments with Board, piece, candidate, outcome, and decision context.
2. Convert rows once per evaluation.
3. Derive side columns on demand.
4. Replace perfect-clear count checks with Board emptiness.
5. Build danger masks from compile-time geometry.
6. Preserve the 29-parameter serialization contract.
7. Run fixture parity tests.

Gate:

- Shared-candidate evaluation and status transitions meet fixture parity.
- Policy height 40 is named and tested.
- No `TetrisNode`, `TetrisMap`, or `TetrisContext` appears in the rewritten policy.

Rollback:

- The frozen baseline binary and fixture corpus remain available. No forwarding overloads are introduced.

### Phase 6: Implement the value-based engine

Tasks:

1. Implement the node arena with `NodeId` references.
2. Port hold and next sequencing.
3. Port widening, pruning, budgets, and best-root projection.
4. Add early semantic deduplication.
5. Add the board evaluation cache with explicit collision handling.
6. Add explicit state equality and hashing.
7. Add concrete `?` virtual-boundary parsing, queue identity, and advancement.
8. Add root reuse over complete queue metadata.
9. Add telemetry.
10. Materialize one selected path after search.

Gate:

- Engine unit tests pass.
- Deterministic iteration runs are repeatable.
- Hold, queue consumption, and `?` virtual-boundary behavior match fixtures.
- No pathfinder call occurs during child expansion.
- Memory use remains below the explicit 256 MB limits currently set by `ai.cpp` and `tuner_match.h`; the new engine sets this production limit directly rather than relying on the old 128 MB class default.

Rollback:

- The new engine remains a separate target until the next phase.

### Phase 7: Cut over production harnesses one at a time

Keep build-time `LEGACY` and `VALUE` target variants only for migration validation. The default production target changes once, after its own gate, and no runtime compatibility adapter enters release code.

Tasks:

1. Switch `tetris_profile.cpp` and pass its deterministic and timed gates.
2. Switch the tuner simulator and pass 32-pair correctness screening.
3. Switch `match.cpp` to Board and shared replay, then pass path and garbage integration tests.
4. Switch `ai.cpp` to the new engine and coordinate adapter, then run the DLL against the switched match simulator.
5. Update final CMake target composition.
6. Build the shared library and all executables.
7. Run ABI and path-protocol integration tests.

Gate:

- Each harness has a named legacy and value build artifact during its cutover gate.
- All production targets build in GCC and Clang self-release.
- The DLL output is accepted by `match`.
- No path replay failure occurs.
- Phase 8 performance and quality screens pass before deletion.

Rollback:

- Revert only the latest harness default-target switch and continue to build its explicit value variant for correction. Remove all migration variants in Phase 8.

### Phase 8: Final validation and legacy deletion

Tasks:

1. Run the complete correctness suite.
2. Run the five-pair profile protocol.
3. Run staged gameplay screens and the final 1,000-pair comparison.
4. Delete old core, rule, and search files.
5. Delete the temporary migration comparison target after archiving results.
6. Remove dead includes and unused containers.
7. Update documentation and benchmark summaries.

Gate:

- Every definition-of-done item in Section 21 passes.

Rollback:

- Legacy deletion is its own final commit after all recorded gates pass. If post-deletion build or integration verification fails, revert that deletion commit exactly, fix the value implementation, repeat the full affected gates, and create a new deletion commit. Do not add a production compatibility path.

## 16. Correctness validation matrix

| Area | Oracle | Required result |
|---|---|---|
| Board import and export | Row-array oracle | Exact rows and roof |
| Placement occupancy | Legacy geometry fixture plus compile-time shape calculation | Exact occupied cells for shared poses |
| SRS rotation | Legacy fixture and generic scalar checker | Exact first-valid result |
| Raw reachability | Scalar BFS oracle | Exact reachable and landable sets |
| Semantic arrival | Two-channel scalar BFS | Exact normal and terminal sets |
| Legacy coverage | Legacy search fixture | Old legal physical placements are covered |
| New-only placements | Independent replay | Every placement is executable |
| Line clear | Row-array oracle | Exact resulting rows and clear count |
| Roof | `highest_y()` oracle | Exact after every mutation |
| T-spin | Legacy classifier fixture and direct rule cases | Observable TOJ parity on shared cases |
| Perfect clear | Board emptiness | Exact attack trigger |
| Pathfinder | Independent scalar SRS interpreter | Exact placement and arrival class |
| Hold | Scenario fixtures | Exact piece consumption and hold state |
| Policy eval | Legacy fixture | Exact or approved floating tolerance |
| Policy transition | Legacy fixture | Exact state and score |
| Engine iteration mode | Repeated fixed-seed run | Bit-for-bit repeatable output |
| ABI output | Match replay | Accepted path and correct lock |

The seeded board corpus must include:

- Empty board.
- Flat and jagged stacks.
- Wells at every column.
- Overhangs and enclosed cavities.
- T-spin mini and full setups.
- I, S, and Z duplicate-orientation cases.
- Boards near rows 18 through 22.
- Boards occupying rows 31 through 47.
- Garbage rises of varying packet sizes.
- Spawn obstruction.
- Cases where only a kick reaches the target.
- Cases where both normal and terminal T arrivals exist.
- Cases where only one T arrival class exists.
- 180-only placements, including direct zero-kick behavior.
- Translating CW and CCW kicks that are terminal rotations.
- Lockout cases where anchor row and lowest occupied mino row disagree.
- Spawn obstruction independent of lockout.
- Unknown and removed path commands.
- Leading, interior, repeated, and trailing virtual-next markers.

## 17. Performance validation

### 17.1 Baseline recorded during planning

Repository state:

- Branch: `fast-reachability-migration`
- Root commit: `82bed15`
- Fast-reachability commit: `0c35e13`
- Preset: `linux-gcc-self-release`
- Workload: 50 moves, 1,000 deterministic iterations, seed 1, max depth 6, hold enabled

Command:

```bash
./out/build/linux-gcc-self-release/tetris_profile \
  --moves 50 --iters 1000 --seed 1 --maxdepth 6 --quiet
```

Observed runs:

| Run | Total seconds | Per-move median ms | Per-move p95 ms | Eval/s | Get/s | Search/s |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4.550 | 55.165 | 89.309 | 825566 | 3650203 | 139963 |
| 2 | 4.760 | 59.632 | 95.022 | 789245 | 3489613 | 133805 |
| 3 | 4.913 | 62.104 | 97.436 | 764644 | 3380839 | 129634 |
| 4 | 4.774 | 62.637 | 94.069 | 786935 | 3479398 | 133414 |
| 5 | 4.676 | 57.333 | 92.111 | 803342 | 3551941 | 136195 |
| Median | 4.760 | 59.632 | 94.069 | 789245 | 3489613 | 133805 |

Every run performed the same legacy work counts:

- Evaluations: 3,756,670
- Policy transitions: 16,609,949
- Search calls: 636,890

These numbers are evidence only. Phase 0 must rerun the final baseline with 200 moves after telemetry is added and before behavior changes.

### 17.2 Required profile protocol

Phase 0 creates two immutable executables, `tetris_profile.baseline` and `tetris_profile.candidate`, plus one absolute frozen 29-double parameter file. Record SHA-256 hashes for all three. Set `TETRIS_AI_PARAM_FILE` to that same absolute file or explicitly unset it for both executables. Record the combo-table content and hash where it comes from an external fixture.

Use one pinned physical core, a recorded CPU model, fixed worker count, recorded scaling governor and frequency policy, and the same compiler and link flags. Do not run unrelated workloads during a pair. Record turbo state and kernel version. The harness adds `--warmup-moves 20`, which executes but excludes those moves from every time and work aggregate.

Use two profile modes with versioned output. The binding fixed-work seed is 1. Seeds 2 and 3 are required reporting checks. `--iters 1000` retains the legacy meaning of 1,000 outer anytime-widening iterations per move; it is a repeatable workload controller, not a claim that both engines execute equal internal work. The versioned work vector provides that accounting.

Fixed-iteration template:

```bash
taskset --cpu-list 0 ./artifacts/tetris_profile.ENGINE \
  --warmup-moves 20 --moves 200 --iters 1000 --seed SEED \
  --maxdepth 6 --param-file /absolute/path/frozen_29d.bin \
  --quiet --quiet-version 2
```

Timed template:

```bash
taskset --cpu-list 0 ./artifacts/tetris_profile.ENGINE \
  --warmup-moves 20 --moves 200 --ms 20 --seed SEED \
  --maxdepth 6 --param-file /absolute/path/frozen_29d.bin \
  --quiet --quiet-version 2
```

Run five paired repetitions per mode with first-engine order `baseline, candidate, candidate, baseline, baseline`. Within each pair, run the second engine immediately after the first. This ABBA plus A order alternates first-run effects while retaining the agreed five pairs. Preserve each baseline and candidate output row rather than aggregating during collection. Compute a candidate-to-baseline ratio inside each pair, then summarize the five pair ratios. Never divide independently aggregated medians.

Version 2 must report total time and the complete work vector from Section 14.3. The binding component rates are nanoseconds per search invocation inside enumeration, per unique semantic candidate inside rule application and deduplication, per board-evaluation request split into hit and miss paths, per policy transition, per materialized tree node, and per selected-path state. Parent-expansion and widening-iteration rates are aggregate diagnostics because their work varies with fan-out. Scoped timers must be nested, non-overlapping at the binding component level, based on the same monotonic clock, and measured disabled versus enabled under the telemetry-overhead gate. Raw-to-unique candidate ratios and cache rates are also reported.

### 17.3 Performance acceptance

The binding claim combines end-to-end latency with per-unit-work evidence because the new engine may legally enumerate more placements.

All conditions are required:

1. For seed 1 fixed-work mode, the median of five paired candidate-to-baseline total-time ratios is at most 1.02.
2. For seed 1 fixed-work mode, the median paired ratio for per-move p95 latency is at most 1.02. A diagnostic interval may resample complete benchmark pairs, then moves within each selected pair, but it never overrides the median-ratio gate.
3. Define relative count change as `(candidate_count - baseline_count) / baseline_count`. If unique semantic candidates or policy transitions differ in magnitude by more than 2 percent, the analysis must partition the delta into fixture-identified new legal candidates, removed semantic duplicates, lockout-semantic changes, and defects. The partition must account for 100 percent of the integer delta. Any unclassified gain or loss invalidates the campaign.
4. For fixed-work mode, none of the binding component rates named in Section 17.2 may regress by more than 2 percent. Aggregate rates are reported but are not used to hide a slower component behind changed fan-out.
5. Selected-path generation plus production replay does not add more than 2 percent to full production move time. Test-only scalar replay is excluded from release timing and reported separately.
6. Timed mode is a production-throughput and timer-behavior gate, not the sole speed proof. For seed 1 it must not reduce completed widening iterations, expanded parents, or retained transitions per second by more than 2 percent after applying the complete count partition from item 3.
7. Seeds 2 and 3 are diagnostics. They do not veto a passing seed 1 speed gate, but any correctness failure, unclassified count delta, replay failure, or telemetry inconsistency found in them invalidates the benchmark implementation and requires all gates to be rerun. A latency-only regression on these seeds is reported and investigated without silently changing the predeclared binding seed.
8. Raw non-T enumeration has a median paired time-per-parent ratio of at most 1.00 against legacy placement enumeration on the frozen search corpus defined in Phase 0.
9. Freeze two T-search comparators in Phase 0: the legacy `search_tspin` enumerator and a local build of Reference A's scalar-arrival wrapper. Use identical boards, T spawn, movement flags, and semantic candidate normalization. The new T enumerator must be no more than 2 percent slower than frozen legacy per normalized semantic candidate and at least 2 times faster than the frozen Reference A wrapper.
10. No benchmark result is accepted if warmup handling, artifact hashes, candidate counts, cache hits, completed work, run order, or machine controls are omitted.
11. No parameter retuning is allowed during the performance comparison.

Telemetry-overhead measurement uses the same candidate binary and fixed-work command with counters collected versus counters disabled at runtime. It follows the same five-pair order defined above and must remain below 0.5 percent before baseline capture.

If the end-to-end gate fails, optimize measured hotspots in this order:

1. Duplicate Board conversion or row extraction.
2. T arrival-channel propagation.
3. Candidate canonicalization and early deduplication.
4. Board apply and clear.
5. Evaluation cache layout.
6. Node materialization and frontier queues.
7. Deadline polling.
8. Selected-path BFS.

Do not reintroduce the legacy pointer graph, `top`, `disable_d`, or per-candidate pathfinding as a performance fix.

## 18. Gameplay quality validation

### 18.1 Metrics and estimators

Use these campaign-level estimators:

- APP: pooled total attack divided by pooled placed pieces for each engine over all games in the selected pairs.
- APL: pooled total attack divided by pooled cleared lines for each engine over all games in the selected pairs.
- WR: for pair `i`, let `n_i1` and `n_i2` be the new engine's win-equivalent scores in the two games after seat normalization, and let `l_i1` and `l_i2` be the legacy scores. Define `pair_wr_diff_i = ((n_i1 + n_i2) - (l_i1 + l_i2)) / 2`. The campaign WR difference is the mean of this value. It lies in `[-1, 1]`, so 3 percentage points means `0.03` on this scale.

Do not average per-game APP or APL ratios for the gate. If the observed legacy attack, pieces, or lines denominator is zero, or any bootstrap replicate has a zero legacy denominator, the campaign is invalid. Diagnose the simulator or choose a predeclared larger pair count and rerun with a fresh final seed. Do not add an arbitrary epsilon.

Also report:

- Death rate by engine.
- Survivor win rate.
- Capped-game rate.
- Mean rounds.
- Perfect clears.
- T-spin mini, single, double, and triple rates.
- Average raw and unique candidates per piece.
- Fixed-work gameplay results as a timer-noise diagnostic.

### 18.2 Binding harness and comparison conditions

The binding harness is the temporary tuner engine-comparison mode from Section 14.4. It uses the tuner simulator's deterministic scenario seeds, attack cancellation, garbage generation, and seat-swapped scheduling. The DLL `match` run is a separate ABI and replay gate.

The implementation must freeze the exact option names, final seed, fixed worker count, and versioned CSV schema before the 32-pair stage. The comparison command then uses this form:

```bash
./out/build/linux-gcc-self-release/tuner compare-engines \
  --engine-a legacy --engine-b value \
  --param-file /absolute/path/frozen_29d.bin \
  --pairs 1000 --search-ms 20 --iters-per-move 0 \
  --seed FINAL_SEED --threads FIXED_THREADS --max-rounds 3600 \
  --pair-output /absolute/path/final_pairs_v1.csv
```

Required controls:

- Same frozen production parameter vector and SHA-256 hash.
- Same sentinel-terminated combo table and recorded values.
- Same max depth.
- Same 20 ms per-move budget for the binding gate.
- Same 3,600-round horizon.
- Same deterministic scenario seed pair in both seat orders.
- Same fixed, explicit worker count rather than `hardware_concurrency()`.
- Same machine, build flags, CPU controls, and artifact hashes.
- Legacy and value engine assignments swapped once inside every pair.
- Per-pair output with pair id, both scenario seeds, seat assignment, both game outcomes, attack, pieces, lines, death, cap, rounds, spin classes, perfect clears, and replay failures.

Use a fixed-iteration comparison on the same staged seeds as a diagnostic for timer variance. During the 256-pair stage, run the timed comparison three times with the same seeds. Timing is unstable if any primary endpoint changes pass-versus-fail status at its non-inferiority margin or if the maximum endpoint point-estimate spread exceeds 1 percentage point for WR or 1 percent relative for APP or APL. If unstable, correct deadline polling or machine controls before declaring the final campaign. The final seed is run once under the frozen stable protocol and is not relitigated through optional repeats.

### 18.3 Staged screen and held-out seeds

Run quality comparisons in stages:

1. 32 pairs for correctness and crash screening.
2. 128 pairs for directional APP, APL, and death-rate screening.
3. 256 pairs after performance acceptance and for variance estimation.
4. 1,000 fresh pairs for final non-inferiority.

The first three stages are non-binding engineering screens. They use seed ranges disjoint from tuning and from the final campaign. They may reveal a defect and stop work, but they do not contribute observations to the final interval. Record the final seed before launching the binding run.

Use the 256-pair stage to estimate pair-level variance. With a deterministic simulation seeded from the fitted pair-level outcome distribution, estimate power as the fraction of 20,000 synthetic 1,000-pair campaigns whose one-sided lower bound clears each 3 percent margin when the true difference is zero. Require at least 80 percent projected power for all three endpoints. The agreed final campaign remains exactly 1,000 pairs. If any endpoint is below 80 percent, the migration cannot claim non-inferiority under this plan and requires a separate user-approved change to the validation design. A low projected power never permits changing a margin after observing final data.

The final report also gives survivor-only and capped-game-stratified APP and APL as sensitivity diagnostics. These do not replace the pooled primary estimators.

### 18.4 Statistical gate

The unit of resampling is one complete seat-swapped pair.

Publish a deterministic analysis script with the migration results. It performs 20,000 bootstrap replicates using a recorded bootstrap seed. Each replicate samples 1,000 pairs with replacement, then recomputes:

- Mean paired WR difference, new minus legacy.
- Pooled APP for each engine and relative difference `(new / legacy) - 1`.
- Pooled APL for each engine and relative difference `(new / legacy) - 1`.
- Absolute death-rate difference as a safety diagnostic.

Use one-sided 95 percent lower percentile bounds. All three primary endpoints must pass simultaneously. This is an intersection-union non-inferiority decision, so no multiplicity adjustment is applied. The one-sided convention must be used consistently in the report rather than the current unpaired two-sided Wilson interval in `tuner vs`.

Final requirements:

- WR difference lower bound is at least negative 3 percentage points.
- APP relative-difference lower bound is at least negative 3 percent.
- APL relative-difference lower bound is at least negative 3 percent.
- The observed point estimate of value-engine death rate minus legacy death rate must be at most 3 percentage points. Also report its paired-bootstrap interval as a diagnostic.
- Path replay failure count is zero.
- The per-pair artifact, analysis script, seeds, binary hashes, parameter hash, machine configuration, and full intervals are archived.

Fast reachability is allowed to choose different moves and find additional placements. Move-for-move equality is not a quality requirement.

## 19. Risk register

| Risk | Consequence | Mitigation |
|---|---|---|
| Old and fast coordinate frames differ | Missing placements, wrong kicks, invalid paths | Exhaustive occupied-cell mapping before search integration |
| 10 by 48 board changes policy behavior | APP, APL, or survival regression | Named policy height 40, lockout at lowest mino row 20, policy fixtures |
| T arrival recovery adds scalar work | Search slower than current implementation | Generic two-channel bit-parallel propagation |
| T candidates multiply by predecessor | Large tree and policy overhead | At most two semantic candidates per physical placement |
| Duplicate I, S, and Z orientations | Redundant child boards | Occupancy canonicalization before evaluation |
| Board metadata is recomputed too often | Lost BFS speedup | Direct canonical `board_t`, one local row extraction per eval, `highest_y` only when needed |
| Pathfinder becomes part of hot expansion | Major slowdown | Generate one path after result selection |
| Pathfinder and search disagree | Invalid external command sequence | Shared production geometry plus independent scalar replay oracle |
| Root reuse hashes padding or derived data | Missed reuse or incorrect dedup | Field-wise equality, occupancy hash, collision verification |
| Fast-reachability fork becomes repo-specific | Maintenance burden | Only generic submodule additions, independent tests, no TOJ policy in submodule |
| Profile comparison hides changed work | False performance claim | Report raw candidates, unique candidates, transitions, iterations, cache rates, and path cost |
| More placements alter tuned policy | Quality regression despite faster search | Fixed-parameter paired non-inferiority gate before any retune |
| Garbage update breaks roof | Invalid safety and top-out behavior | Seeded Board mutation property tests |
| Compiler SIMD behavior differs | GCC or Clang-only correctness | Debug and self-release tests with both compilers |

## 20. Legacy deletion checklist

Delete or fully replace:

- `src/tetris_core.h`
- `src/tetris_core.cpp`
- `src/search_tspin.h`
- `src/search_tspin.cpp`
- `src/rule_toj.h`
- `src/rule_toj.cpp`
- `TetrisNode`
- `TetrisOpertion`
- `TetrisWallKickOpertion`
- `TetrisContext`
- `TetrisMap`
- `TetrisMapSnap`
- `TetrisNodeMarkTemplate`
- `TetrisNodeMark`
- `TetrisNodeMarkFiltered`
- `TetrisNodeBlockLocate`
- `TetrisBlockStatus`
- `TetrisEngine`
- `TetrisTreeNode`
- `TetrisCore`
- `TranspositionTable`
- `TetrisNodeFlag`
- `TetrisNext`
- `AIHasIterate`
- `AIHasSpawn`
- `ProfiledTOJ`
- `ProfiledSearch`
- `m_tetris_rule_tools`
- `index_filtered`
- `node_max`
- `place_cache_`
- `generate_cache_`
- `type_max_`
- `zobrist_table`
- `map_hash`
- `land_point`
- `move_down_multi`
- `disable_d`
- `allow_d`
- `allow_D`
- `allow_LR`
- `allow_nont_d`
- `allow_rotate_move`
- `last_rotate`
- production `is_20g`

After deletion, search the complete `src` and test trees for every name above. Matches are allowed only in archived migration results or documentation, not compiled production or test source. Remove `migration_compare` and its legacy fixture generator after preserving immutable fixture data and final reports.

Remove `chash_map.h`, `chash_set.h`, or other legacy utilities only if the final dependency scan proves they are unused. Their removal is not a prerequisite for the reachability port.

## 21. Definition of done

The task is complete only when all statements below are true.

### Architecture

- The canonical board is 10 by 48 and uses fast-reachability occupancy directly.
- Board stores only occupancy and exact roof metadata.
- No legacy placement node graph or operation table remains.
- Search, rule, policy, pathfinder, engine, and harness responsibilities are separated.
- Fast-reachability modifications are generic and covered by submodule tests.

### Correctness

- Existing perft vectors pass.
- Bit-parallel reachability and semantic arrivals match the scalar oracle.
- Shared legacy placements are covered.
- Every new-only candidate independently replays.
- T-spin behavior matches observable TOJ fixtures.
- Board, clear, roof, perfect clear, garbage, hold, and top-out tests pass.
- Every selected path replays to exact placement and arrival class.
- Exported ABI and path protocol remain accepted by `match`.

### Performance

- Seed 1 fixed-work total time and per-move p95 pass the five-pair 2 percent gates.
- Every binding component rate in Section 17.2 passes its 2 percent gate.
- Every candidate and policy-transition count delta is fully classified.
- Timed profile completed-work metrics pass the reconciled 2 percent gates.
- The frozen T comparators pass the legacy and Reference A thresholds.
- Selected-path overhead is within 2 percent of production move time.
- Seeds 2 and 3 contain no correctness, accounting, replay, or telemetry defect.
- Memory remains within the explicitly configured 256 MB production limit.

### Quality

- The predeclared final campaign has at least 80 percent projected power for all primary endpoints.
- Final paired WR lower-bound non-inferiority passes.
- Final pooled APP lower-bound non-inferiority passes.
- Final pooled APL lower-bound non-inferiority passes.
- The death-rate point-difference safety threshold passes.
- Replay failures are zero.
- Production parameters were not retuned to mask the migration result.

### Cleanup

- Legacy files and temporary comparison code are removed.
- CMake builds shared library, match, profile, tuner, and tests through reusable targets.
- GCC and Clang debug and self-release builds pass.
- Documentation records final benchmark commands, hardware, results, and submodule commit.

## 22. Recommended commit sequence

1. `docs: track migration plan and narrow docs ignore rule`
2. `test: add migration fixtures and profile telemetry`
3. `core: add packed placement types and 10x48 board`
4. `reachability: add generic semantic arrival propagation`
5. `rule: add value-based TOJ placement outcomes`
6. `search: add semantic placement enumeration`
7. `path: add replay-validated selected-move pathfinder`
8. `ai: port TOJ policy to value contracts`
9. `engine: add value-based hold-aware search tree`
10. `profile: cut profile harness to value engine`
11. `tuner: cut paired simulator to value engine`
12. `match: cut simulator and replay to value board`
13. `dll: cut exported AI to value engine`
14. `build: finalize value-engine target composition`
15. `perf: record profile and gameplay acceptance results`
16. `cleanup: remove legacy node, map, rule, search, and migration targets`

Each commit should build and pass the tests relevant to its phase. Production cutover and legacy deletion must remain separate commits so a failed acceptance gate can revert the cutover without discarding the validated foundation.
