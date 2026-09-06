#include "tetris_engine.h"
#include "toj_policy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <print>
#include <random>
#include <vector>

namespace engine_alias = tetris_engine;

namespace
{
    engine_alias::Board board_with_density(std::mt19937_64 &rng, int roof, int density)
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < roof; ++y)
        {
            std::uint16_t row = 0;
            for (int x = 0; x < 10; ++x)
            {
                if (static_cast<int>(rng() % 100) < density)
                {
                    row |= static_cast<std::uint16_t>(1u << x);
                }
            }
            rows[static_cast<std::size_t>(y)] = row;
        }
        return engine_alias::Board::from_rows(rows);
    }

    bool has_full_row(engine_alias::Board const &board)
    {
        for (int y = 0; y < engine_alias::Board::height; ++y)
        {
            if (board.row(y) == engine_alias::Board::row_mask)
            {
                return true;
            }
        }
        return false;
    }

    std::vector<engine_alias::Board> workload_boards()
    {
        std::mt19937_64 rng(20260906);
        std::vector<engine_alias::Board> boards;
        while (boards.size() < 16)
        {
            int const roof = 4 + static_cast<int>(rng() % 14);
            int const density = 45 + static_cast<int>(rng() % 40);
            engine_alias::Board candidate = board_with_density(rng, roof, density);
            if (!has_full_row(candidate))
            {
                boards.push_back(candidate);
            }
        }
        return boards;
    }

    struct RunCounters
    {
        std::uint64_t nanos = 0;
        std::uint64_t passes = 0;
        std::uint64_t computed = 0;
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t replacements = 0;
        std::uint64_t fingerprint = 1469598103934665603ull;
        std::size_t roots_rejected = 0;
    };

    engine_alias::CacheConfig cache_for(engine_alias::CacheConfig::Layout layout)
    {
        engine_alias::CacheConfig config;
        config.layout = layout;
        config.entries = 16384;
        config.ways = 4;
        return config;
    }

    RunCounters run_workload(engine_alias::CacheConfig::Layout layout,
        std::vector<engine_alias::Board> const &boards,
        toj_policy::Config &policy_config, bool verbose)
    {
        RunCounters totals;
        engine_alias::EngineConfig config;
        config.policy = &policy_config;
        config.cache = cache_for(layout);
        engine_alias::Engine engine;
        if (!engine.init(config))
        {
            std::println(stderr, "bench engine failed to initialize");
            std::exit(1);
        }
        for (std::size_t index = 0; index < boards.size(); ++index)
        {
            auto queue = engine_alias::parse_queue("III");
            if (!queue.has_value())
            {
                std::println(stderr, "bench queue failed to parse");
                std::exit(1);
            }
            toj_policy::State state;
            engine_alias::HoldState hold;
            hold.locked = true;
            auto begin = std::chrono::steady_clock::now();
            auto root = engine.set_root(boards[index], state, std::move(*queue), hold);
            if (root == engine_alias::no_node)
            {
                std::println(stderr, "bench board {} was rejected as a root", index);
                std::exit(1);
            }
            engine.run(engine_alias::SearchBudget::by_iterations(300));
            auto finish = std::chrono::steady_clock::now();
            totals.nanos += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(finish - begin).count());
            auto stats = engine.search_stats();
            totals.passes += stats.widening_passes;
            totals.computed += stats.eval_computed;
            totals.hits += stats.cache_hits;
            totals.misses += stats.cache_misses;
            totals.replacements += stats.cache_replacements;
            auto selection = engine.select_best();
            if (!selection.has_value())
            {
                std::println(stderr, "bench board {} produced no selection", index);
                std::exit(1);
            }
            for (std::size_t way = 0; way < 2; ++way)
            {
                totals.fingerprint =
                    (totals.fingerprint ^ (way == 0 ? selection->root_child
                                                    : selection->evidence))
                    * 1099511628211ull;
            }
            if (verbose)
            {
                std::println("  board={} arena={} root_child={} evidence={}", index,
                    engine.arena_size(), selection->root_child, selection->evidence);
            }
        }
        return totals;
    }
}

int main()
{
    toj_policy::Config policy_config;
    static int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    policy_config.combo_table = combo_table;
    policy_config.combo_table_max = 10;
    policy_config.safe = 5;
    policy_config.parameters = toj_policy::Parameters::production_defaults();

    auto const boards = workload_boards();
    auto const layout_bytes = sizeof(engine_alias::EvalCacheEntry) * 16384;

    std::println("workload: 16 full-row-free seeded boards, queue III, hold empty+locked, 300 iterations per search");
    std::println("variant byte budget: {} bytes per cached variant (16384 entries x {} byte entry)",
        layout_bytes * 1ull, sizeof(engine_alias::EvalCacheEntry));
    std::println("layouts: direct-mapped is one way per set; set-associative-4 uses four ways per set");
    std::println("counters are per-search; totals accumulate the per-search snapshots");
    std::println("warmup: 1 sweep, measured: 5 sweeps, fixed variant order per sweep");

    struct Variant
    {
        char const *name;
        engine_alias::CacheConfig::Layout layout;
    };
    std::array<Variant, 3> const variants{ {
        { "disabled", engine_alias::CacheConfig::Layout::Disabled },
        { "direct-mapped", engine_alias::CacheConfig::Layout::DirectMapped },
        { "set-associative-4", engine_alias::CacheConfig::Layout::SetAssociative },
    } };

    std::array<std::uint64_t, 3> totals{};
    std::array<RunCounters, 3> last{};
    for (int warmup = 0; warmup < 1; ++warmup)
    {
        for (auto const &variant : variants)
        {
            (void)run_workload(variant.layout, boards, policy_config, false);
        }
    }
    for (int rep = 0; rep < 5; ++rep)
    {
        for (std::size_t v = 0; v < variants.size(); ++v)
        {
            RunCounters const result = run_workload(variants[v].layout, boards, policy_config, rep == 0);
            totals[v] += result.nanos;
            last[v] = result;
            std::println("rep={} variant={} nanos={} passes={} computed={} hits={} misses={} replacements={} fingerprint={:016x}",
                rep, variants[v].name, result.nanos, result.passes, result.computed,
                result.hits, result.misses, result.replacements, result.fingerprint);
        }
    }
    for (std::size_t v = 0; v < variants.size(); ++v)
    {
        std::println("total variant={} nanos={}", variants[v].name, totals[v]);
        std::println("last variant={} computed={} hits={} misses={} replacements={} passes={} fingerprint={:016x}",
            variants[v].name, last[v].computed, last[v].hits, last[v].misses,
            last[v].replacements, last[v].passes, last[v].fingerprint);
    }
    return 0;
}
