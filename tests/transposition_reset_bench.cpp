#include "tetris_engine.h"
#include "toj_policy.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <print>

namespace engine_alias = tetris_engine;

namespace
{
    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    tetris::Board shelf_board()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 18; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        return tetris::Board::from_rows(rows);
    }

    engine_alias::Queue remaining_queue(engine_alias::Queue const &full,
        std::size_t from)
    {
        engine_alias::Queue out;
        for (std::size_t i = from; i < full.pieces.size(); ++i)
        {
            out.pieces.push_back(full.pieces[i]);
            out.boundary.push_back(full.boundary[i]);
        }
        out.marker_count = full.marker_count;
        return out;
    }
}

int main()
{
    constexpr int kRoots = 200;

    toj_policy::Config policy_config;
    policy_config.combo_table = combo_table;
    policy_config.combo_table_max = 10;
    policy_config.safe = 5;
    policy_config.parameters = toj_policy::Parameters::production_defaults();

    engine_alias::EngineConfig config;
    config.policy = &policy_config;
    engine_alias::Engine engine;
    if (!engine.init(config))
    {
        std::println(stderr, "transposition_reset_bench: engine init failed");
        return 1;
    }

    toj_policy::State state;
    engine_alias::HoldState no_hold;
    no_hold.locked = true;

    tetris::Board const board_a = shelf_board();
    tetris::Board const board_b{};

    // (a) N=200 cold-path resets: alternate between two boards that miss reuse.
    auto cold_begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kRoots; ++i)
    {
        tetris::Board const &board = (i % 2 == 0) ? board_a : board_b;
        auto queue = engine_alias::parse_queue("TIS");
        if (!queue.has_value())
        {
            std::println(stderr, "transposition_reset_bench: queue parse failed");
            return 1;
        }
        engine_alias::NodeId root =
            engine.set_root(board, state, std::move(*queue), no_hold);
        if (root == engine_alias::no_node)
        {
            std::println(stderr, "transposition_reset_bench: cold root {} rejected", i);
            return 1;
        }
    }
    auto cold_end = std::chrono::steady_clock::now();
    std::uint64_t const cold_nanos = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(cold_end - cold_begin)
            .count());
    std::uint64_t const ns_per_cold =
        cold_nanos / static_cast<std::uint64_t>(kRoots);

    // (b) N=200 reroots: each iteration cold-roots A, runs a short search to
    // populate a depth-1 child with children, then reroots into that child.
    // The child board/policy/hold plus the remaining queue suffix satisfy
    // reuse_matches, so set_root takes the reroot path (arena_size > 1 after).
    std::uint64_t reroot_nanos = 0;
    int reroots_ok = 0;
    for (int i = 0; i < kRoots; ++i)
    {
        auto queue_a = engine_alias::parse_queue("TIS");
        if (!queue_a.has_value())
        {
            std::println(stderr, "transposition_reset_bench: queue parse failed");
            return 1;
        }
        if (engine.set_root(board_a, state, std::move(*queue_a), no_hold)
            == engine_alias::no_node)
        {
            std::println(stderr, "transposition_reset_bench: reroot population root rejected");
            return 1;
        }
        engine.run(2);
        engine_alias::NodeId target = engine_alias::no_node;
        for (std::size_t id = 1; id < engine.arena_size(); ++id)
        {
            auto const *node =
                engine.node(static_cast<engine_alias::NodeId>(id));
            if (node->depth == 1 && node->child_count > 0)
            {
                target = static_cast<engine_alias::NodeId>(id);
                break;
            }
        }
        if (target == engine_alias::no_node)
        {
            // Fall back to any depth-1 child; still a reuse hit, though the
            // retained subtree may be trivial.
            for (std::size_t id = 1; id < engine.arena_size(); ++id)
            {
                auto const *node =
                    engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1)
                {
                    target = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
        }
        if (target == engine_alias::no_node)
        {
            std::println(stderr,
                "transposition_reset_bench: no depth-1 child to reroot into");
            return 1;
        }
        auto const *child = engine.node(target);
        tetris::Board const next_board = child->board;
        toj_policy::State const next_policy = child->policy;
        engine_alias::HoldState const next_hold = child->hold;
        engine_alias::Queue next = remaining_queue(engine.queue(), child->cursor);
        auto reroot_begin = std::chrono::steady_clock::now();
        engine_alias::NodeId rerooted =
            engine.set_root(next_board, next_policy, std::move(next), next_hold);
        auto reroot_end = std::chrono::steady_clock::now();
        reroot_nanos += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                reroot_end - reroot_begin)
                .count());
        if (rerooted == engine_alias::no_node)
        {
            std::println(stderr, "transposition_reset_bench: reroot {} rejected", i);
            return 1;
        }
        if (engine.arena_size() > 1)
        {
            ++reroots_ok;
        }
    }
    std::uint64_t const ns_per_reroot =
        reroot_nanos / static_cast<std::uint64_t>(kRoots);
    std::size_t const used_last = engine.transposition_used();

    // Sanity: run a short search so the transposition table is exercised and
    // confirm the used trajectory is nonzero.
    engine.run(2);
    std::size_t const used_after_run = engine.transposition_used();
    bool const ok = (used_after_run > 0) && (reroots_ok == kRoots);

    std::println("RESET ns_per_cold_root={} ns_per_reroot={} transposition_used_last={}",
        ns_per_cold, ns_per_reroot, used_last);
    std::println("SANITY reroots_retained={}/{} transposition_used_after_run={} ok={}",
        reroots_ok, kRoots, used_after_run, ok ? 1 : 0);
    if (!ok)
    {
        std::println(stderr, "transposition_reset_bench: sanity failed");
        return 1;
    }
    return 0;
}
