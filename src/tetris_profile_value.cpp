#include "profile_value_support.h"
#include "tetris_board.h"
#include "tetris_engine.h"
#include "tetris_types.h"
#include "toj_policy.h"
#include "toj_rule.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <print>
#include <string>
#include <vector>

namespace
{
    namespace engine_alias = tetris_engine;
    namespace toj_alias = tetris::toj;
    namespace support = profile_value;

    using Board = tetris::Board;
    using Piece = tetris::Piece;

    bool read_param_file(std::string const &path, double *out)
    {
        FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }
        std::size_t const count = std::fread(out, sizeof(double), 29, file);
        std::fclose(file);
        return count == 29;
    }

    void invalidate(std::string const &reason)
    {
        std::println(stderr, "tetris_profile_value: invalid run: {}", reason);
    }
}

int main(int argc, char **argv)
{
    using namespace std::chrono;
    support::Options opt = support::parse_args(argc, argv);
    std::uint64_t budget_ms =
        opt.ms > 0 ? static_cast<std::uint64_t>(opt.ms)
                   : static_cast<std::uint64_t>(std::pow(100.0, opt.level / 8.0));

    steady_clock::time_point t_init0 = steady_clock::now();

    toj_policy::Config policy_config;
    policy_config.combo_table = support::combo_table;
    policy_config.combo_table_max = support::combo_table_max;
    policy_config.safe = 0;
    policy_config.parameters = toj_policy::Parameters::production_defaults();
    if (!opt.param_file.empty())
    {
        double theta[29];
        if (!read_param_file(opt.param_file, theta))
        {
            std::println(stderr, "failed to read 29-double parameter file: {}", opt.param_file);
            return 1;
        }
        toj_policy::Parameters::from_theta(theta, policy_config.parameters);
    }
    toj_policy::Policy seed_policy;
    seed_policy.init(&policy_config);

    engine_alias::EngineConfig engine_config;
    engine_config.policy = &policy_config;
    engine_config.movement.allow_180 = true;
    engine_config.telemetry_enabled = opt.telemetry;
    engine_alias::Engine engine;
    if (!engine.init(engine_config))
    {
        std::println(stderr, "engine init failed");
        return 1;
    }

    double init_ms =
        duration<double, std::milli>(steady_clock::now() - t_init0).count();

    Board board;
    support::Scenario scenario(opt.seed);
    std::optional<Piece> hold_piece;
    int combo = 0;
    int b2b = 0;
    std::int64_t total_clear = 0;
    std::int64_t total_attack = 0;
    std::int64_t games = 0;
    std::int64_t dead_moves = 0;
    std::size_t moves_done = 0;

    std::vector<double> rootsearch_ms;
    std::vector<double> emove_ms;

    double setup_ms = 0;
    double setup_eval_ms = 0;
    double run_ms = 0;
    double path_ms = 0;
    double apply_ms = 0;

    std::int64_t acc_parents = 0;
    std::int64_t acc_widening = 0;
    std::int64_t acc_enum_calls = 0;
    std::int64_t acc_raw = 0;
    std::int64_t acc_unique = 0;
    std::int64_t acc_rule = 0;
    std::int64_t acc_memo_hits = 0;
    std::int64_t acc_computed = 0;
    std::int64_t acc_eval_requests = 0;
    std::int64_t acc_cache_requests = 0;
    std::int64_t acc_cache_hits = 0;
    std::int64_t acc_cache_misses = 0;
    std::int64_t acc_cache_replacements = 0;
    std::int64_t acc_materialized = 0;
    std::int64_t acc_policy_transitions = 0;
    std::int64_t acc_merges = 0;
    std::int64_t acc_refused = 0;
    std::int64_t acc_path_calls = 0;
    std::int64_t acc_path_states = 0;
    std::int64_t acc_replay_failures = 0;
    std::int64_t acc_texhaust = 0;
    std::size_t pending_end_max = 0;
    std::int64_t acc_enum_ns = 0;
    std::int64_t acc_rule_ns = 0;
    std::int64_t acc_hit_ns = 0;
    std::int64_t acc_miss_ns = 0;
    std::int64_t acc_materialize_ns = 0;
    std::int64_t acc_policy_ns = 0;
    std::int64_t acc_parent_ns = 0;
    std::int64_t acc_find_ns = 0;
    std::int64_t acc_replay_ns = 0;
    std::int64_t node_live_delta = 0;

    tetris::Placement const spawn =
        tetris::Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);

    std::size_t const total_moves = opt.warmup_moves + opt.moves;
    steady_clock::time_point t_total0 = steady_clock::now();
    bool valid = true;
    std::string invalid_reason;

    while (valid && moves_done < total_moves)
    {
        bool const warming = moves_done < opt.warmup_moves;
        steady_clock::time_point t_setup0 = steady_clock::now();
        scenario.start_move(opt.maxdepth);
        char current_char = scenario.current();
        auto current_piece = tetris::try_from_char(current_char);
        if (!current_piece.has_value())
        {
            valid = false;
            invalid_reason = "scenario produced an unknown piece";
            break;
        }
        Piece current = *current_piece;
        std::size_t arena_before = engine.arena_size();

        auto die_ordinary = [&]() {
            if (!warming)
            {
                ++dead_moves;
                ++games;
            }
            board = Board{};
            scenario.reset_on_death();
            hold_piece.reset();
            combo = 0;
            b2b = 0;
        };

        if (!toj_alias::fits(current, spawn, board))
        {
            double span =
                duration<double, std::milli>(steady_clock::now() - t_setup0).count();
            if (!warming)
            {
                rootsearch_ms.push_back(span);
                emove_ms.push_back(span);
            }
            die_ordinary();
            if (warming && moves_done + 1 == opt.warmup_moves)
            {
                t_total0 = steady_clock::now();
            }
            ++moves_done;
            continue;
        }

        toj_policy::State root_state;
        root_state.combo = static_cast<std::int8_t>(combo);
        root_state.b2b = static_cast<std::int8_t>(b2b ? 1 : 0);
        policy_config.safe = seed_policy.safe_margin(board, current);
        steady_clock::time_point t_eval0 = steady_clock::now();
        toj_policy::Evaluation seed_eval = seed_policy.evaluate(board);
        double seed_eval_ms =
            duration<double, std::milli>(steady_clock::now() - t_eval0).count();
        root_state.t2_value = seed_eval.t2_value;
        root_state.t3_value = seed_eval.t3_value;

        engine_alias::HoldState hold;
        hold.piece = opt.hold ? hold_piece : std::nullopt;
        hold.locked = !opt.hold;

        engine_alias::Queue queue;
        for (std::size_t i = 0; i <= opt.maxdepth; ++i)
        {
            auto piece = tetris::try_from_char(scenario.queue()[i]);
            if (!piece.has_value())
            {
                valid = false;
                invalid_reason = "scenario queue holds an unknown piece";
                break;
            }
            queue.pieces.push_back(*piece);
            queue.boundary.push_back(false);
        }
        if (!valid)
        {
            break;
        }

        engine_alias::NodeId root = engine.set_root(board, root_state, queue, hold);
        steady_clock::time_point t_setup1 = steady_clock::now();
        if (root == engine_alias::no_node)
        {
            valid = false;
            invalid_reason = "root rejected (invalid queue, full row, or exhausted capacity)";
            break;
        }

        engine_alias::SearchBudget budget = opt.iters > 0
            ? engine_alias::SearchBudget::by_iterations(opt.iters)
            : engine_alias::SearchBudget::by_time(budget_ms);
        steady_clock::time_point t_run0 = steady_clock::now();
        engine.run(budget);
        steady_clock::time_point t_run1 = steady_clock::now();

        engine_alias::PathTelemetry path_before = engine.path_telemetry();

        engine_alias::FinalResult result = engine.finalize(spawn);
        steady_clock::time_point t_path1 = steady_clock::now();

        engine_alias::SearchStats stats = engine.search_stats();
        engine_alias::ComponentTimers timers = engine.component_timers();

        double setup_span =
            duration<double, std::milli>(t_setup1 - t_setup0).count();
        double run_span =
            duration<double, std::milli>(t_run1 - t_run0).count();
        double path_span =
            duration<double, std::milli>(t_path1 - t_run1).count();
        double emove_span =
            duration<double, std::milli>(t_path1 - t_setup0).count();
        double rootsearch_span =
            duration<double, std::milli>(t_run1 - t_setup0).count();

        if (!result.has_selection || !result.path_ok)
        {
            valid = false;
            invalid_reason = "finalization failed (missing selection or unverified path)";
            break;
        }

        if (toj_policy::Policy::is_lockout(result.played, result.candidate->placement))
        {
            if (!warming)
            {
                rootsearch_ms.push_back(rootsearch_span);
                emove_ms.push_back(emove_span);
            }
            die_ordinary();
            if (warming && moves_done + 1 == opt.warmup_moves)
            {
                t_total0 = steady_clock::now();
            }
            ++moves_done;
            continue;
        }

        steady_clock::time_point t_apply0 = steady_clock::now();
        std::optional<toj_alias::RuleResult> applied =
            toj_alias::apply(board, result.played, *result.candidate);
        if (!applied.has_value())
        {
            valid = false;
            invalid_reason = "rule application rejected the finalized candidate";
            break;
        }
        auto spin = static_cast<support::SpinClass>(
            static_cast<std::uint8_t>(applied->spin));
        int attack = support::score_attack(
            applied->clear_count, spin, applied->perfect_clear, combo, b2b);
        board = applied->board;
        double apply_span =
            duration<double, std::milli>(steady_clock::now() - t_apply0).count();

        bool hold_was_empty = !hold_piece.has_value();
        if (result.used_hold)
        {
            hold_piece = current;
            if (hold_was_empty)
            {
                scenario.pop_played_extra();
            }
        }

        std::size_t arena_after = engine.arena_size();
        engine_alias::PathTelemetry path_after = engine.path_telemetry();

        if (!warming)
        {
            total_clear += applied->clear_count;
            total_attack += attack;
            rootsearch_ms.push_back(rootsearch_span);
            emove_ms.push_back(emove_span);
            setup_ms += setup_span;
            setup_eval_ms += seed_eval_ms;
            run_ms += run_span;
            path_ms += path_span;
            apply_ms += apply_span;
            acc_parents += static_cast<std::int64_t>(stats.expanded_parents);
            acc_widening += static_cast<std::int64_t>(stats.widening_passes);
            acc_enum_calls += static_cast<std::int64_t>(stats.enumeration_calls);
            acc_raw += static_cast<std::int64_t>(stats.raw_kernel_landings);
            acc_unique += static_cast<std::int64_t>(stats.unique_candidates);
            acc_rule += static_cast<std::int64_t>(stats.rule_applications);
            acc_memo_hits += static_cast<std::int64_t>(stats.eval_memo_hits);
            acc_computed += static_cast<std::int64_t>(stats.eval_computed);
            acc_eval_requests += static_cast<std::int64_t>(stats.eval_requests);
            acc_cache_requests += static_cast<std::int64_t>(stats.cache_requests);
            acc_cache_hits += static_cast<std::int64_t>(stats.cache_hits);
            acc_cache_misses += static_cast<std::int64_t>(stats.cache_misses);
            acc_cache_replacements +=
                static_cast<std::int64_t>(stats.cache_replacements);
            acc_materialized += static_cast<std::int64_t>(stats.materialized_nodes);
            acc_policy_transitions +=
                static_cast<std::int64_t>(stats.policy_transitions);
            acc_merges += static_cast<std::int64_t>(stats.transposition_merges);
            acc_refused += static_cast<std::int64_t>(stats.promotions_refused);
            acc_path_calls += static_cast<std::int64_t>(
                path_after.calls - path_before.calls);
            acc_path_states += static_cast<std::int64_t>(
                path_after.states_expanded - path_before.states_expanded);
            acc_replay_failures += static_cast<std::int64_t>(
                path_after.failures - path_before.failures);
            acc_texhaust += stats.transposition_exhausted ? 1 : 0;
            pending_end_max = std::max(pending_end_max, stats.pending_occupancy);
            acc_enum_ns += timers.enum_ns;
            acc_rule_ns += timers.rule_ns;
            acc_hit_ns += timers.eval_hit_ns;
            acc_miss_ns += timers.eval_miss_ns;
            acc_materialize_ns += timers.materialize_ns;
            acc_policy_ns += timers.policy_ns;
            acc_parent_ns += timers.parent_ns;
            acc_find_ns += timers.path_find_ns;
            acc_replay_ns += timers.path_replay_ns;
            node_live_delta += static_cast<std::int64_t>(arena_after)
                * static_cast<std::int64_t>(sizeof(engine_alias::Node))
                - static_cast<std::int64_t>(arena_before)
                    * static_cast<std::int64_t>(sizeof(engine_alias::Node));
        }
        if (warming && moves_done + 1 == opt.warmup_moves)
        {
            t_total0 = steady_clock::now();
        }
        ++moves_done;
    }

    if (!valid)
    {
        invalidate(invalid_reason);
        return 1;
    }

    double total_sec = duration<double>(steady_clock::now() - t_total0).count();
    if (total_sec <= 0)
    {
        total_sec = 1e-9;
    }

    auto as_count = [&](std::int64_t v) -> std::optional<std::int64_t> {
        return opt.telemetry ? std::optional<std::int64_t>(v) : std::nullopt;
    };

    support::V3Row row;
    row.moves = rootsearch_ms.size();
    row.total_s = total_sec;
    row.min_ms = rootsearch_ms.empty()
        ? 0
        : *std::min_element(rootsearch_ms.begin(), rootsearch_ms.end());
    row.median_ms = support::percentile(rootsearch_ms, 0.5);
    row.p95_ms = support::percentile(rootsearch_ms, 0.95);
    row.p99_ms = support::percentile(rootsearch_ms, 0.99);
    row.max_ms = rootsearch_ms.empty()
        ? 0
        : *std::max_element(rootsearch_ms.begin(), rootsearch_ms.end());
    row.evals = as_count(acc_eval_requests);
    row.transitions = as_count(acc_policy_transitions);
    row.searches = as_count(acc_enum_calls);
    row.dead_moves = dead_moves;
    row.games = games;
    row.node_live_delta_bytes = node_live_delta;
    if (opt.telemetry)
    {
        row.evals_per_s = static_cast<double>(acc_eval_requests) / total_sec;
        row.transitions_per_s =
            static_cast<double>(acc_policy_transitions) / total_sec;
        row.searches_per_s = static_cast<double>(acc_enum_calls) / total_sec;
    }
    row.warmup_moves = opt.warmup_moves;
    row.seed = opt.seed;
    row.iters = opt.iters;
    row.maxdepth = opt.maxdepth;
    row.budget_ms = opt.ms > 0 ? opt.ms : 0.0;
    row.mode = opt.iters > 0 ? "iters" : "ms";
    row.telemetry = opt.telemetry ? "on" : "off";
    row.emove_min_ms = emove_ms.empty()
        ? 0
        : *std::min_element(emove_ms.begin(), emove_ms.end());
    row.emove_med_ms = support::percentile(emove_ms, 0.5);
    row.emove_p95_ms = support::percentile(emove_ms, 0.95);
    row.emove_p99_ms = support::percentile(emove_ms, 0.99);
    row.emove_max_ms =
        emove_ms.empty() ? 0 : *std::max_element(emove_ms.begin(), emove_ms.end());
    row.setup_ms = setup_ms;
    row.setup_eval_ms = setup_eval_ms;
    row.run_ms = run_ms;
    row.path_ms = path_ms;
    row.apply_ms = apply_ms;
    row.init_ms = init_ms;
    row.parents = as_count(acc_parents);
    row.parent_ns = as_count(acc_parent_ns);
    row.widening_iters = as_count(acc_widening);
    row.enum_ns = as_count(acc_enum_ns);
    row.raw_landings = as_count(acc_raw);
    row.unique_candidates = as_count(acc_unique);
    row.rule_transitions = as_count(acc_rule);
    row.rule_ns = as_count(acc_rule_ns);
    row.eval_hit_ns = as_count(acc_hit_ns);
    row.eval_miss_ns = as_count(acc_miss_ns);
    row.eval_memo_hits = as_count(acc_memo_hits);
    row.eval_computed = as_count(acc_computed);
    row.cache_requests = as_count(acc_cache_requests);
    row.cache_hits = as_count(acc_cache_hits);
    row.cache_misses = as_count(acc_cache_misses);
    row.cache_replacements = as_count(acc_cache_replacements);
    row.materialized_nodes = as_count(acc_materialized);
    row.materialize_ns = as_count(acc_materialize_ns);
    row.policy_ns = as_count(acc_policy_ns);
    row.transposition_merges = as_count(acc_merges);
    row.promotions_refused = as_count(acc_refused);
    row.pending_end_max =
        as_count(static_cast<std::int64_t>(pending_end_max));
    row.texhaust_moves = as_count(acc_texhaust);
    row.path_calls = as_count(acc_path_calls);
    row.path_states = as_count(acc_path_states);
    row.path_find_ns = as_count(acc_find_ns);
    row.path_replay_ns = as_count(acc_replay_ns);
    row.replay_failures = as_count(acc_replay_failures);
    row.mem_retained_bytes = static_cast<std::int64_t>(engine.retained_bytes());
    row.arena_reserved_bytes =
        static_cast<std::int64_t>(engine.arena_reserved_bytes());
    row.idmap_reserved_bytes =
        static_cast<std::int64_t>(engine.idmap_reserved_bytes());
    if (opt.telemetry)
    {
        row.raw_unique_ratio_x1000 =
            1000 * acc_raw / std::max<std::int64_t>(1, acc_unique);
    }

    if (opt.quiet)
    {
        std::println("{}", support::format_v3(row));
        return 0;
    }

    std::println("=== Value-engine profile (value search + verified finalization) ===");
    std::println("board 10x40, budget {}, maxdepth {}, hold {}, seed {}",
        opt.iters > 0 ? (std::to_string(opt.iters) + " iters [DETERMINISTIC]")
                      : (std::to_string(budget_ms) + " ms/move"),
        opt.maxdepth, opt.hold ? "on (root-only when disabled)" : "off", opt.seed);
    std::println("moves: {} (games completed: {}, dead-moves: {})",
        row.moves, games, dead_moves);
    std::println("total wall time: {:.3f} s", total_sec);
    std::println("root-update-plus-search per-move [ms]: min {:.3f} | median {:.3f} | p95 {:.3f} | p99 {:.3f} | max {:.3f}",
        row.min_ms, row.median_ms, row.p95_ms, row.p99_ms, row.max_ms);
    std::println("end-to-end per-move [ms]: min {:.3f} | median {:.3f} | p95 {:.3f} | p99 {:.3f} | max {:.3f}",
        row.emove_min_ms, row.emove_med_ms, row.emove_p95_ms, row.emove_p99_ms,
        row.emove_max_ms);
    std::println("totals [ms]: setup {:.3f} (root eval {:.3f}) | run {:.3f} | path {:.3f} | apply {:.3f} | init {:.3f}",
        setup_ms, setup_eval_ms, run_ms, path_ms, apply_ms, init_ms);
    if (opt.telemetry)
    {
        std::println("parents {} | widening iters {} | raw landings {} | unique {} | rule transitions {}",
            acc_parents, acc_widening, acc_raw, acc_unique, acc_rule);
        std::println("eval requests {} (memo hits {} | computed {}) | cache req {} hit {} miss {} repl {}",
            acc_eval_requests, acc_memo_hits, acc_computed, acc_cache_requests,
            acc_cache_hits, acc_cache_misses, acc_cache_replacements);
        std::println("materialized {} | policy transitions {} | merges {} | refused {} | pending end max {} | texhaust moves {}",
            acc_materialized, acc_policy_transitions, acc_merges, acc_refused,
            pending_end_max, acc_texhaust);
        std::println("paths {} | states {} | replay failures {}",
            acc_path_calls, acc_path_states, acc_replay_failures);
    }
    std::println("memory retained: {} bytes ({:.1f} MB)",
        row.mem_retained_bytes, row.mem_retained_bytes / (1024.0 * 1024.0));
    std::println("game stats: clears {} | attack {} | b2b {} | combo {}",
        total_clear, total_attack, b2b, combo);
    return 0;
}
