# Seam verification: legacy-host diagnostic claims vs actual code

Date: 2026-09-10. Mode: read-only. Source claims: `docs/phase7/decision_request_2026-09-09.md` sections 3 and 5 (revised at `b0dcc28`). Each claim checked against current working-tree code at HEAD `b0dcc28`.

## Verdict table

| # | Claim | Verdict |
|---|---|---|
| 1 | `m_tetris::TetrisEngine` template accepts a replaceable search type; `tetris_profile.cpp` instantiates with `ProfiledSearch` | CONFIRMED |
| 2 | `TetrisContext::get(TetrisBlockStatus)` resolves existing pose nodes; `ExternalPoseTransform::to_legacy` supplies inverse coordinate mapping | CONFIRMED |
| 3 | `TetrisNodeWithTSpinType` carries arrival/classification flags separately from the pose pointer | CONFIRMED |
| 4 | `detail::enumerate_into_for_block` hardcodes spawn; kernel seeding clamps to height cut; no supplied-start entry point | CONFIRMED |
| 5 | Legacy `TetrisTreeNode::search` does pose-status-keyed reuse and same-piece hold dedup that consume arrival semantics | CONFIRMED |
| 6 | `TetrisCore::eval` retains a pointer to the table result; `TranspositionTable::find` verifies hashes not boards | CONFIRMED |
| 7 | Evaluate path and `expand_source` seam locations near `tetris_engine.cpp:1161-1205` for replay harvesting | CONFIRMED (current line range is 1164-1205) |
| 8 | `arrival_candidates` times arrival search and raw counting without canonicalization; `legacy_corpus_bench` includes map import and metadata rebuild; both normalize outside the span | CONFIRMED, with scope caveats below |
| 9 | `tuner.cpp` lacks a `compare-engines` command; `migration_compare.cpp` runs legacy versus legacy | CONFIRMED |
| 10 | 40 versus 48 row domain; `Board` packing versus `TetrisMap` row/top arrays | CONFIRMED |

## Evidence

### 1. Search-type substitution seam
- `src/tetris_core.h:2148-2149`: `template<class TetrisRule, class TetrisAI, class TetrisSearch> class TetrisEngine`.
- `src/tetris_profile.cpp:60`: `struct ProfiledSearch : search_tspin::Search` (counting wrapper); `src/tetris_profile.cpp:77`: `using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ProfiledTOJ, ProfiledSearch>;`.
- Caveat: `ProfiledSearch` derives from `search_tspin::Search`, so it demonstrates template substitutability at the engine level, not a foreign search-type implementation. The traits the substituted type must satisfy are listed under "additional seams" below.

### 2. Pose resolution and coordinate inverse
- Declaration `src/tetris_core.h:345` (`TetrisNode const *get(TetrisBlockStatus const &status) const`), definition `src/tetris_core.cpp:576` (second overload at `:589`); the context's `node_index_` pose map is populated in prepare at `src/tetris_core.cpp:303`.
- Engine-side convenience `src/tetris_core.h:2242-2244` delegates to `shared_context_->get`.
- `src/toj_rule.h:91-104`: `static constexpr std::optional<std::array<int,3>> to_legacy(Piece, Placement)` applies the stored `dx/dy` entry inversely; entries built at `src/toj_rule.h:53-77`; O-piece rotations above 0 are marked invalid (`:66`). Non-representable poses return `nullopt` rather than coercing, matching the runbook requirement to count and report unmappable results.

### 3. Arrival/classification carrier
- `src/search_tspin.h:29-57`: `TetrisNodeWithTSpinType` holds `node`, `last`, `TSpinType type`, and a `flags` union (`is_check`, `is_last_rotate`, `is_ready`, `is_mini_ready`) separate from the pose pointer, with `operator TetrisNode const*` and `operator->` giving pose access. Full equality includes type and flags (`:61-63`).
- Consumer example: `src/ai_zzz.h:17` aliases `TetrisNodeEx` to it.

### 4. Spawn-hardcoded enumeration
- `src/toj_rule.h:32-33`: `spawn_x = 4`, `spawn_y = 20`; `:35-37`: `spawn(Piece)` returns that coord.
- `src/toj_rule.h:334` (`enumerate_into_for_block` signature: board, piece, config, span): search start is the literal `spawn(piece)` at `:382`; kernel seeding is `reachability::search::dispatch_with_height<B, 20>(board.occupancy(), board.roof(), ...)` at `:379` (fixed height cut 20). No overload takes an arbitrary start pose. The supplied-start entry point the decision request demands genuinely does not exist.
- Note: the public wrapper dispatches to this same function at `src/toj_rule.h:450`.

### 5. Pose-status-keyed reuse and hold dedup in the legacy tree
- `TetrisTreeNode::search(context, search_node, is_hold)` at `src/tetris_core.h:1391`: incremental pass rebuilds children from `old` (a `chash_map<TetrisBlockStatus, TetrisTreeNode*>` declared at `:983`), keyed on `land_point_node->status` (`:1412-1420`), i.e., pose identity only.
- Same-piece hold dedup at `src/tetris_core.h:1450-1490`: when `search_node->status.t == hold_node->status.t`, current-piece landings are inserted into `uniq` (a `chash_set<TetrisBlockStatus,...>` at `:984`) by `child->identity->status` (`:1471`) and hold landings are dropped when `uniq.find(land_point_node->status)` matches (`:1475-1479`).
- Consequence for the adapter: both `old` and `uniq` key on the `TetrisBlockStatus` union (piece, x, y, rotation; `src/tetris_core.h:115-130`, hash/equal at `:131-145`). Two landings at the same pose but different `ArrivalClass` (the value engine's terminal-rotation channel) collapse into one child through these consumers. Preserving both T arrival channels through legacy tree code is therefore an actual semantic blocker, exactly as the document states.

### 6. Legacy evaluation cache contract
- `src/tetris_core.h:649-687`: `TranspositionTable` with `max_count = 1 << 16`, `Entry { uint64_t hash; Result result; }`, and `std::pair<bool, Result*> find(uint64_t hash)` returning `{hash != 0 && e.hash == hash, &e.result}`: hash equality only, no board stored or compared, direct-mapped by `hash & mask_`.
- `src/tetris_core.h:828-870` (`TetrisCore::eval`): computes `hash = map_hash(new_map)` (`:837`), then `auto [hit, slot] = table->find(hash); tree_node->result = slot;` (`:845-846`): the tree node retains a pointer into table storage, not an owned copy; misses write `*slot` (`:857`) and `table->set_hash(hash)` (`:864`). Also note `tree_node->clear = node->attach(context->engine, new_map)` (`:831-833`) runs before the hit test, and `table` is per-depth (`tt[depth]`, `:836`), so a "hit" can alias a different board sharing the hash. The decision request's demand to audit the hash-only control against an exact or cache-disabled configuration is well founded.

### 7. Value-engine evaluate/expand seams (fusion-trial region)
- Current committed positions in `src/tetris_engine.cpp`: `Engine::evaluate_once` at `:869` and `:874/877` overloads; the per-candidate hot loop inside `expand_source_for_block` runs `:1164-1205`: source-local board/outcome dedup scan from `source_begin` (`:1164-1171`), then `Evaluation evaluation = evaluate_once(applied->board)` (`:1193`, with `TETRIS_ROW_FUSION_TRIAL` fused-safe overload at `:1188-1190` and trace overload at `:1184-1185`), then `policy_.transition_known_lockout(...)` (`:1196-1204`), then `Child child;` construction (`:1207+`).
- Replay-harvesting seam: `config_.expand_source_record` callbacks already exist around this block (`:1085`, `:1120`, `:1342`, `:1377`), plus `record_push`/`record_emit` instrumentation. A frozen parent-input replay can be harvested from these without new production code. The doc's "near 1161-1205" is accurate modulo a 1-3 line drift at current HEAD.
- Note: two `expand_source`/`expand_source_for_block` definitions exist (`:1035/1059` and `:1301/1317`), macro-selected (`#ifndef TETRIS_CHILD_SOA_TRIAL` region near `:1013`); the runbook should say which variant the diagnostic builds.

### 8. What the two benches actually time
- `src/arrival_candidates.cpp`: `enumerate_normalized` (`:71-103`) = `search_workspace` + `arrival_search` from the fixed coord `reach_corpus::spawn_x/y` (`:74`) + raw landing iteration; with `report == nullptr` (timed pass) it skips all hashing/normalization (`:85-87`), returning only the count. `run_timed` (`:119-128`) puts `board_from_rows` (48-row arrays to packed BOARD via `from_row_bitboard<true>`) INSIDE the span, then divides by board count. So the span is: board import + workspace + arrival search + raw count; NO canonicalization/sorting/bucketing.
- `tests/legacy_corpus_bench.cpp`: `drive_case` (`:147-188`) creates a fresh `TetrisMap(10, 40)` (`:150`, constants at `:53-54`), copies rows 0..39, runs `rebuild_metadata` (`:100-122`), `generate(piece)`, then `search(map, node, 1)`; normalization into the report occurs only when `report != nullptr` (`:158-186`). `run_timed_piece` (`:191-203`) times the whole `drive_case` including map construction, row copy, metadata rebuild, and `generate`; normalization stays outside the span. Boards with occupied rows at or above 40 are rejected with a hard error (`:246-249`).
- Conclusion: the decision request's statement is correct as far as it goes, with this refinement for Step 1 design: BOTH benches currently include their respective input-conversion cost in the span (packed-board build on the candidate side, `TetrisMap` import plus metadata rebuild on the legacy side), and NEITHER includes production candidate canonicalization. The Step 1 "prepacked" stage must hoist `board_from_rows` out, and the legacy import-inclusive variant must be reported separately from a prepared-input variant, or the 0.621 per-call diagnostic will keep mixing import with search.
- The cited absolute numbers (about 1,087ns candidate vs 1,751ns legacy per search call, total 1.242s vs 1.763s) are consistent with these scopes: they measure different, partial wrapper stages, not like-for-like enumeration. Rows: `results/phase7/architecture_review_2026-09-09/raw/architecture-review-2026-09-09-candidate-timers.txt` and `-legacy-cmp-timers.txt`.

### 9. Quality-harness status
- `src/tuner.cpp`: no `compare-engines` string anywhere in the file (its command vocabulary is tune/compare of parameter sets for one engine; e.g., `[VS]` and `[TUNER]` output paths at `:700`, `:1532`). Claim CONFIRMED.
- `src/migration_compare.cpp`: the CSV header has `engine1,engine2` columns (`:89`) but `outcome_row` hardcodes the pair as `",legacy,legacy"` (`:23`); matches run through `tuner_match::run_batch` (`:70`) on `TunerEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>` (`src/tuner_match.h:62`) for both seats. The claim is correct: the comparison harness is legacy-versus-legacy as wired today.

### 10. Row domain mismatch
- Value board: `src/tetris_board.h:15-16` `width = 10`, `height = 48`; `row(int y)` returns `uint16_t` (`:59`); occupancy import clips to 48 rows via `from_row_bitboard<true>` over `std::array<row_t, height>` (`:25-28`).
- Legacy map: `src/tetris_core.h:28` `const int max_height = 40`; `TetrisMap { uint32_t row[max_height]; int32_t top[32]; int32_t width, height, roof, count; }` (`:42-49`), with `full(x,y)` bit-tested per `uint32_t` row; the pose graph, drop tables (`TetrisNode::move_down_multi[max_height]`, `:239`), and the zobrist table (`max_height * 32`, `:692`) are all sized to 40 rows.
- Consequence: target placements in rows 40..47 (upper domain) and any pose outside the prepared 40-row domain have no `TetrisBlockStatus` node; `TetrisContext::get` returns `nullptr` (`src/tetris_core.cpp:576-587`). The legacy bench's own height guard (`legacy_corpus_bench.cpp:246-249`) demonstrates this is a live boundary, not a theoretical one. A common-domain-only adapter is a valid partial result, as the document says.

## Additional seams and blockers the document does not state

1. **Hard traits the substitute `TetrisSearch` must satisfy.** `TetrisCore` computes its landing type from `element_traits<decltype(TetrisSearch().search(TetrisMap(), nullptr, 0))>::Element` (`src/tetris_core.h:725-733`), which requires: default constructibility of the search type; `search(map, node, depth)` returning `std::vector<Element> const *`; `init(TetrisContext const*, Config const*)` (`src/search_tspin.h:74`, `Config` at `:18`); and `make_path(node, land_point, map)` with the exact legacy signature (`src/search_tspin.h:75`). The engine wires the search object through `LocalContextBuilder`/`init_search` (`src/tetris_core.h:641`). The adapter must satisfy all of this at compile time; "reuse the value pathfinder" means writing a shim with the legacy `make_path` signature returning `std::vector<char>`.
2. **Single-live-result lifetime.** `search(...)` returns a pointer to an internal buffer that the tree iterates immediately per call (`src/tetris_core.h:1396, 1417, 1462, 1504, 1546`); the value engine's `CandidateBatch` does not have this shape. The adapter must hold or copy results with legacy lifetime semantics.
3. **LandPoint element requirements beyond status.** Elements must convert to `TetrisNode const*` and support `->status` deref and `attach(context->engine, map)` returning a clear count (`src/tetris_core.h:833`; `TetrisNode::attach` declared at `:250`); `tree_node->identity = node` stores the element itself (`:832`). A "richer landing" that is not a legacy pose pointer (e.g., a value-engine Candidate) will not flow through `Core::eval` without changing `eval` or wrapping the element around an existing stable `TetrisNode`. The existing-pose-node design in section 3 is therefore load-bearing, not incidental.
4. **Pre-evaluation attach and per-depth tables in legacy eval.** `Core::eval` attaches the move to the child map before consulting the table (`:831-833`), and the table is selected by depth (`:836`, `tt` vector at `:982`). Any "exact or cache-disabled control" the document asks for must disable lookup without removing the attach effect.
5. **Virtual-boundary/'?' next-token handling.** Legacy tree state carries `process_next` and `context->virtual_flag` (`src/tetris_core.h:995, 1307, 1741-1759`), which the value engine replaced with queue/hold semantics. Adapter replay through `update/search` keeps this machinery on the legacy side; the document's hold/queue boundary test items are necessary, not optional.
6. **The Step 1 prepacked stage must exclude `board_from_rows`.** `arrival_candidates.cpp:119-128` includes it in the span today; hoisting it changes the reported per-call cost on the candidate side, so the "same boards, same flags" comparison must prebuild BOTH sides' inputs (packed boards and prepared `TetrisMap`s) outside the span, with import-inclusive variants reported separately, as the document already specifies for the legacy side.
7. **All current production-facing harnesses instantiate the legacy engine** (`src/ai.cpp:46`, `src/match.cpp:907`, `src/migration_fixtures.cpp:37`, `src/spec_oracle.cpp:204`, `src/tuner_match.h:62`); the value engine has no match/quality harness at all. This reinforces the decision request's section 6A point that quality comparison is unbuilt work, and also means a legacy-host adapter result will be directly comparable to all existing quality baselines without new harness work only if it preserves legacy semantics exactly, which is the thing section 3 issues 2 and 3 warn against.

## Unknowns
- No artifact records any prior hybrid prototype, adapter spike, or kernel-in-legacy measurement; nothing found under `results/` or `docs/phase7/`.
- The `<50ns` raw-BFS figure has no directly timed artifact: it is consistent with derived attribution (`docs/phase7/hotspot_attribution_f.md`: wrapper at 2912ns per search over 2,855,528 searches with about 2.15 percent self cycles in the reachability primitives gives order 50-60ns per call) but not a direct timer; the only direct absolute in cited docs is Reference A's 159ns on their corpus (`/home/icly/Documents/GitHub/tet/docs/performance-comparison-tetris-profile.md`, revision `a5f20f7`).
- The `~1500ns` legacy per-call absolute has no measured source in recorded artifacts; gate89 (`results/phase7/gate89/summary.md`) printed ratios only (current/legacy per-parent medians 0.0762 T raw time, worst non-T 0.2399).
