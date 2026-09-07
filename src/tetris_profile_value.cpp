#include "profile_value_runner.h"
#include "profile_value_support.h"
#include "tetris_engine.h"
#include "toj_policy.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <print>
#include <string>

namespace
{
    namespace engine_alias = tetris_engine;
    namespace support = profile_value;

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
}

int main(int argc, char **argv)
{
    using namespace std::chrono;
    support::Options opt = support::parse_args(argc, argv);
    std::uint64_t budget_ms = 0;
    if (!support::resolve_budget_ms(opt, budget_ms))
    {
        std::println(stderr, "tetris_profile_value: time budget is not representable");
        return 1;
    }

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
    engine_config.timers_enabled = opt.timers;
    engine_alias::Engine engine;
    if (!engine.init(engine_config))
    {
        std::println(stderr, "engine init failed");
        return 1;
    }

    double init_ms =
        duration<double, std::milli>(steady_clock::now() - t_init0).count();

    support::Runner::Config runner_config;
    runner_config.maxdepth = opt.maxdepth;
    runner_config.hold = opt.hold;
    runner_config.iters = opt.iters;
    runner_config.budget_ms = budget_ms;
    support::Runner runner(
        policy_config, seed_policy, engine, opt.seed, runner_config);

    support::Totals totals;
    std::size_t moves_done = 0;
    std::size_t const total_moves = opt.warmup_moves + opt.moves;
    steady_clock::time_point t_total0 = steady_clock::now();
    bool valid = true;
    std::string invalid_reason;

    while (valid && moves_done < total_moves)
    {
        bool const warming = moves_done < opt.warmup_moves;
        support::MoveRecord record = runner.step();
        if (record.kind == support::MoveRecord::Kind::Invalid)
        {
            valid = false;
            invalid_reason = record.invalid_reason;
            break;
        }
        if (!warming)
        {
            if (record.kind == support::MoveRecord::Kind::Placed)
            {
                totals.add(record, runner.last_attack());
            }
            else
            {
                totals.add(record, 0);
                totals.add_death();
            }
        }
        if (warming && moves_done + 1 == opt.warmup_moves)
        {
            t_total0 = steady_clock::now();
        }
        ++moves_done;
    }

    if (!valid)
    {
        std::println(stderr, "tetris_profile_value: invalid run: {}", invalid_reason);
        return 1;
    }

    double total_sec = duration<double>(steady_clock::now() - t_total0).count();
    if (total_sec <= 0)
    {
        total_sec = 1e-9;
    }

    support::V3Row row = support::build_v3_row(totals, opt, total_sec, init_ms,
        static_cast<std::int64_t>(engine.retained_bytes()),
        static_cast<std::int64_t>(engine.arena_reserved_bytes()),
        static_cast<std::int64_t>(engine.idmap_reserved_bytes()), opt.telemetry);

    if (opt.quiet)
    {
        std::println("{}", support::format_v3(row));
        return 0;
    }

    std::println("=== Value-engine profile (value search + verified finalization) ===");
    std::println("board 10x40, budget {}, maxdepth {}, hold {}, seed {}",
        opt.iters > 0 ? (std::to_string(opt.iters) + " iters [DETERMINISTIC]")
                      : (std::to_string(budget_ms) + " ms/move"),
        opt.maxdepth,
        opt.hold ? "on" : "off (root-only: no executed hold at each root)", opt.seed);
    std::println("moves: {} (games completed: {}, dead-moves: {})",
        row.moves, totals.games, totals.dead_moves);
    std::println("total wall time: {:.3f} s", total_sec);
    std::println("root-update-plus-search per-move [ms]: min {:.3f} | median {:.3f} | p95 {:.3f} | p99 {:.3f} | max {:.3f}",
        row.min_ms, row.median_ms, row.p95_ms, row.p99_ms, row.max_ms);
    std::println("end-to-end per-move [ms]: min {:.3f} | median {:.3f} | p95 {:.3f} | p99 {:.3f} | max {:.3f}",
        row.emove_min_ms, row.emove_med_ms, row.emove_p95_ms, row.emove_p99_ms,
        row.emove_max_ms);
    std::println("totals [ms]: setup {:.3f} (root eval {:.3f}) | run {:.3f} | path {:.3f} | apply {:.3f} | init {:.3f}",
        totals.setup_ms, totals.setup_eval_ms, totals.run_ms, totals.path_ms,
        totals.apply_ms, init_ms);
    if (opt.telemetry)
    {
        std::println("parents {} | widening iters {} | raw landings {} | unique {} | rule transitions {}",
            totals.parents, totals.widening_iters, totals.raw_landings,
            totals.unique_candidates, totals.rule_transitions);
        std::println("eval requests {} (memo hits {} | computed {}) | cache req {} hit {} miss {} repl {}",
            totals.eval_requests, totals.eval_memo_hits, totals.eval_computed,
            totals.cache_requests, totals.cache_hits, totals.cache_misses,
            totals.cache_replacements);
        std::println("materialized {} | policy transitions {} | merges {} | refused {} | pending end max {} | texhaust moves {}",
            totals.materialized_nodes, totals.policy_transitions,
            totals.transposition_merges, totals.promotions_refused,
            totals.pending_end_max, totals.texhaust_moves);
        std::println("paths {} | states {} | replay failures {}",
            totals.path_calls, totals.path_states, totals.replay_failures);
    }
    std::println("memory retained: {} bytes ({:.1f} MB)",
        row.mem_retained_bytes, row.mem_retained_bytes / (1024.0 * 1024.0));
    std::println("game stats: clears {} | attack {} | b2b {} | combo {}",
        totals.total_clear, totals.total_attack, runner.b2b(), runner.combo());
    return 0;
}
