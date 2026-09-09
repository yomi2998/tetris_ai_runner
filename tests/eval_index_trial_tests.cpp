#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace engine_alias = tetris_engine;

namespace
{
    std::size_t checks = 0;
    std::size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(stderr, "FAIL: {}", what);
        }
    }

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };

    struct Fixture
    {
        toj_policy::Config policy_config;
        engine_alias::EngineConfig engine_config;
        engine_alias::Engine engine;
    };

    Fixture make_fixture(bool telemetry)
    {
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 5;
        fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.telemetry_enabled = telemetry;
        check(fixture.engine.init(fixture.engine_config), "trial fixture engine initializes");
        return fixture;
    }

    engine_alias::NodeId make_root(Fixture &fixture, std::string_view queue_text)
    {
        auto queue = engine_alias::parse_queue(queue_text);
        check(queue.has_value(), "trial root queue parses");
        if (!queue.has_value())
        {
            return engine_alias::no_node;
        }
        engine_alias::HoldState hold;
        hold.locked = true;
        toj_policy::State state;
        return fixture.engine.set_root(tetris::Board{}, state, std::move(*queue), hold);
    }

    std::size_t trial_slots()
    {
        return TETRIS_EVAL_INDEX_SLOTS;
    }

    bool trial_tagged()
    {
        return TETRIS_EVAL_INDEX_TAGGED == 1;
    }

    std::size_t slot_of(tetris::Board const &board)
    {
        std::uint64_t finalized = engine_alias::eval_index_finalize(
            engine_alias::occupancy_fingerprint(board.occupancy()));
        return static_cast<std::size_t>(finalized & (TETRIS_EVAL_INDEX_SLOTS - 1));
    }

    std::uint32_t tag_of(tetris::Board const &board)
    {
        std::uint64_t finalized = engine_alias::eval_index_finalize(
            engine_alias::occupancy_fingerprint(board.occupancy()));
        return static_cast<std::uint32_t>(finalized >> 32);
    }

    void test_finalize()
    {
        check(engine_alias::eval_index_finalize(0u) == 0u, "fmix zero maps to zero");
        check(engine_alias::eval_index_finalize(0u)
                == engine_alias::eval_index_finalize(0u),
            "fmix deterministic");
        check(engine_alias::eval_index_finalize(1u) != 1u, "fmix mixes nonzero input");
        check(engine_alias::eval_index_finalize(0xFFFFFFFFFFFFFFFFull)
                != 0xFFFFFFFFFFFFFFFFull,
            "fmix mixes all-ones input");
        tetris::Board empty;
        check(slot_of(empty) < trial_slots(), "slot derivation within range");
    }

    void test_geometry()
    {
        Fixture fixture = make_fixture(true);
        std::size_t expect = trial_slots() * 4u + (trial_tagged() ? trial_slots() * 4u : 0u);
        check(fixture.engine.eval_index_reserved_for_test() == expect,
            "reserved bytes match slot geometry");
        check(fixture.engine.retained_bytes() < (256ull << 20), "retained under cap");
        check(fixture.engine.retained_bytes() >= expect, "retained covers index");
    }

    void test_root_insert_and_reset()
    {
        Fixture fixture = make_fixture(true);
        engine_alias::NodeId root = make_root(fixture, "TJ");
        check(root == 0, "trial root materializes");
        check(fixture.engine.eval_index_occupied_for_test() == 1,
            "root insert occupies one slot");
        std::size_t clears_before = fixture.engine.eval_index_clears_for_test();
        check(clears_before >= 1, "move start clears index");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "trial root expands");
        for (auto const &child : children)
        {
            engine_alias::NodeId id = engine_alias::no_node;
            bool merged = false;
            fixture.engine.eval_index_search_materialize_for_test(child, id, merged);
            check(id != engine_alias::no_node, "trial child materializes");
        }
        check(fixture.engine.eval_index_occupied_for_test() > 1,
            "materializations populate index");
        engine_alias::NodeId root2 = make_root(fixture, "TJ");
        check(root2 != engine_alias::no_node, "second root accepted");
        check(fixture.engine.eval_index_occupied_for_test() == 1,
            "new move clears then reinserts root only");
        check(fixture.engine.eval_index_clears_for_test() == clears_before + 1,
            "exactly one clear per move start");
    }

    void test_single_insertion_and_depth_zero_merge()
    {
        Fixture fixture = make_fixture(true);
        engine_alias::NodeId root = make_root(fixture, "T");
        check(root == 0, "single-insert root materializes");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "single-insert fixture expands");
        if (children.empty())
        {
            return;
        }
        std::size_t insertions_before = fixture.engine.eval_index_insertions_for_test();
        engine_alias::NodeId first_id = engine_alias::no_node;
        bool first_merged = true;
        fixture.engine.eval_index_search_materialize_for_test(
            children[0], first_id, first_merged);
        check(!first_merged && first_id != engine_alias::no_node,
            "first search materialization retains a node");
        check(fixture.engine.eval_index_insertions_for_test() == insertions_before + 1,
            "one retained node inserts exactly once");
        std::size_t size_before = fixture.engine.arena_size();
        engine_alias::NodeId second_id = engine_alias::no_node;
        bool second_merged = false;
        fixture.engine.eval_index_search_materialize_for_test(
            children[0], second_id, second_merged);
        check(second_merged, "repeated child merges at depth zero");
        check(second_id == first_id, "merge returns the retained node");
        check(fixture.engine.arena_size() == size_before,
            "merged tail pops back out of the arena");
        check(fixture.engine.eval_index_insertions_for_test() == insertions_before + 1,
            "merged pop path inserts nothing");
        check(fixture.engine.eval_index_slot_for_test(slot_of(children[0].board))
                != static_cast<std::uint32_t>(size_before),
            "merged pop path leaves no dead slot");
    }

    void test_index_hit_path()
    {
        Fixture fixture = make_fixture(true);
        engine_alias::NodeId root = make_root(fixture, "T");
        check(root == 0, "hit-path root materializes");
        auto first = fixture.engine.expand(root);
        check(!first.empty(), "hit-path first expansion nonempty");
        for (auto const &child : first)
        {
            engine_alias::NodeId id = engine_alias::no_node;
            bool merged = false;
            fixture.engine.eval_index_search_materialize_for_test(child, id, merged);
        }
        std::size_t requests_before = fixture.engine.eval_index_requests_for_test();
        std::size_t hits_before = fixture.engine.eval_index_hits_for_test();
        std::size_t misses_before = fixture.engine.eval_index_misses_for_test();
        auto second = fixture.engine.expand(root);
        check(second.size() == first.size(), "repeat expansion stages same count");
        for (std::size_t i = 0; i < second.size() && i < first.size(); ++i)
        {
            check(second[i].board == first[i].board, "repeat expansion boards identical");
        }
        std::size_t requests_delta =
            fixture.engine.eval_index_requests_for_test() - requests_before;
        std::size_t hits_delta = fixture.engine.eval_index_hits_for_test() - hits_before;
        std::size_t misses_delta =
            fixture.engine.eval_index_misses_for_test() - misses_before;
        check(requests_delta == second.size(), "every memo miss probes index");
        check(hits_delta == requests_delta, "indexed boards all hit");
        check(misses_delta == 0, "no index miss on repeat expansion");
        auto stats = fixture.engine.search_stats();
        check(stats.cache_requests == fixture.engine.eval_index_requests_for_test(),
            "row requests mirror index requests");
        check(stats.cache_hits == fixture.engine.eval_index_hits_for_test(),
            "row hits mirror index hits");
        check(stats.cache_misses == fixture.engine.eval_index_misses_for_test(),
            "row misses mirror index misses");
    }

    void test_exact_mismatch_collision()
    {
        Fixture fixture = make_fixture(true);
        engine_alias::NodeId root = make_root(fixture, "T");
        check(root == 0, "collision root materializes");
        auto children = fixture.engine.expand(root);
        check(children.size() >= 2, "collision fixture has two children");
        if (children.size() < 2)
        {
            return;
        }
        std::size_t pick_a = children.size();
        std::size_t pick_b = children.size();
        std::vector<engine_alias::NodeId> kept(children.size(), engine_alias::no_node);
        for (std::size_t i = 0; i < children.size(); ++i)
        {
            bool merged = false;
            fixture.engine.eval_index_search_materialize_for_test(
                children[i], kept[i], merged);
        }
        for (std::size_t i = 0; i < children.size() && pick_b == children.size(); ++i)
        {
            for (std::size_t j = i + 1; j < children.size(); ++j)
            {
                if (kept[i] != engine_alias::no_node && kept[j] != engine_alias::no_node
                    && !(children[i].board == children[j].board))
                {
                    pick_a = i;
                    pick_b = j;
                    break;
                }
            }
        }
        check(pick_b != children.size(), "collision pair with distinct boards found");
        if (pick_b == children.size())
        {
            return;
        }
        engine_alias::NodeId id0 = kept[pick_a];
        std::size_t slot = slot_of(children[pick_b].board);
        fixture.engine.eval_index_overwrite_for_test(
            slot, tag_of(children[pick_b].board), id0);
        std::size_t requests_before = fixture.engine.eval_index_requests_for_test();
        std::size_t misses_before = fixture.engine.eval_index_misses_for_test();
        std::size_t hits_before = fixture.engine.eval_index_hits_for_test();
        std::size_t tag_before = fixture.engine.eval_index_tag_mismatches_for_test();
        auto repeat = fixture.engine.expand(root);
        check(!repeat.empty(), "collision repeat expansion nonempty");
        std::size_t requests_after = fixture.engine.eval_index_requests_for_test();
        check(fixture.engine.eval_index_misses_for_test() > misses_before,
            "exact mismatch counts a miss");
        check(fixture.engine.eval_index_hits_for_test() - hits_before
                + fixture.engine.eval_index_misses_for_test() - misses_before
                == requests_after - requests_before,
            "every probe resolves as hit or miss");
        check(fixture.engine.eval_index_tag_mismatches_for_test() > tag_before,
            "exact mismatch records tag diagnostic");
        std::uint32_t wrong_tag = tag_of(children[pick_b].board) ^ 0xFFFFFFFFu;
        std::size_t misses_mid = fixture.engine.eval_index_misses_for_test();
        std::size_t tag_mid = fixture.engine.eval_index_tag_mismatches_for_test();
        fixture.engine.eval_index_overwrite_for_test(slot, wrong_tag, id0);
        auto repeat2 = fixture.engine.expand(root);
        check(!repeat2.empty(), "wrong-tag repeat expansion nonempty");
        check(fixture.engine.eval_index_misses_for_test() > misses_mid,
            "wrong tag counts a miss");
        if (trial_tagged())
        {
            check(fixture.engine.eval_index_tag_mismatches_for_test() == tag_mid,
                "tag mismatch skips exact comparison");
        }
        else
        {
            check(fixture.engine.eval_index_tag_mismatches_for_test() > tag_mid,
                "untagged probe always compares");
        }
    }

    void test_stale_and_range_rejection()
    {
        Fixture fixture = make_fixture(true);
        engine_alias::NodeId root = make_root(fixture, "T");
        check(root == 0, "rejection root materializes");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "rejection fixture expands");
        if (children.empty())
        {
            return;
        }
        {
            engine_alias::NodeId id = engine_alias::no_node;
            bool merged = false;
            fixture.engine.eval_index_search_materialize_for_test(children[0], id, merged);
        }
        std::size_t slot = slot_of(children[0].board);
        fixture.engine.eval_index_overwrite_for_test(
            slot, tag_of(children[0].board), 0xFFFFFFFEu);
        std::size_t misses_before = fixture.engine.eval_index_misses_for_test();
        std::size_t hits_before = fixture.engine.eval_index_hits_for_test();
        auto repeat = fixture.engine.expand(root);
        check(!repeat.empty(), "rejection repeat expansion nonempty");
        check(fixture.engine.eval_index_misses_for_test() > misses_before,
            "out-of-range id counts a miss");
        check(fixture.engine.eval_index_hits_for_test() == hits_before,
            "out-of-range id never counts a hit");
    }

    void test_enabled_disabled_identity()
    {
        Fixture on = make_fixture(true);
        Fixture off = make_fixture(true);
        off.engine.set_eval_index_enabled_for_test(false);
        engine_alias::NodeId root_on = make_root(on, "TJ");
        engine_alias::NodeId root_off = make_root(off, "TJ");
        check(root_on == root_off, "enabled and disabled roots agree");
        auto children_on = on.engine.expand(root_on);
        auto children_off = off.engine.expand(root_off);
        check(children_on.size() == children_off.size(), "expansions agree in size");
        for (std::size_t i = 0; i < children_on.size() && i < children_off.size(); ++i)
        {
            check(children_on[i].board == children_off[i].board,
                "expansion boards identical");
            check(children_on[i].evaluation.value == children_off[i].evaluation.value
                    && children_on[i].evaluation.t2_value
                        == children_off[i].evaluation.t2_value
                    && children_on[i].evaluation.t3_value
                        == children_off[i].evaluation.t3_value,
                "expansion evaluations identical");
            {
                engine_alias::NodeId id = engine_alias::no_node;
                bool merged = false;
                on.engine.eval_index_search_materialize_for_test(
                    children_on[i], id, merged);
            }
            {
                engine_alias::NodeId id = engine_alias::no_node;
                bool merged = false;
                off.engine.eval_index_search_materialize_for_test(
                    children_off[i], id, merged);
            }
        }
        auto second_on = on.engine.expand(root_on);
        auto second_off = off.engine.expand(root_off);
        check(second_on.size() == second_off.size(), "repeat expansions agree");
        check(on.engine.eval_index_digest_for_test()
                == off.engine.eval_index_digest_for_test(),
            "request result digests identical");
        auto stats_on = on.engine.search_stats();
        auto stats_off = off.engine.search_stats();
        check(stats_on.eval_requests == stats_off.eval_requests, "request counts agree");
        check(stats_on.eval_memo_hits == stats_off.eval_memo_hits, "memo hits agree");
        check(stats_on.materialized_nodes == stats_off.materialized_nodes,
            "materialization counts agree");
        check(stats_on.policy_transitions == stats_off.policy_transitions,
            "transition counts agree");
        check(stats_on.unique_candidates == stats_off.unique_candidates,
            "candidate counts agree");
        check(off.engine.eval_index_requests_for_test() == 0, "disabled probes nothing");
        check(off.engine.eval_index_hits_for_test() == 0, "disabled hits nothing");
        check(on.engine.eval_index_hits_for_test() > 0, "enabled hits indexed boards");
    }

    void test_telemetry_lifecycle()
    {
        Fixture fixture = make_fixture(false);
        engine_alias::NodeId root = make_root(fixture, "TJ");
        check(root == 0, "telemetry-off root materializes");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "telemetry-off expansion nonempty");
        for (auto const &child : children)
        {
            engine_alias::NodeId id = engine_alias::no_node;
            bool merged = false;
            fixture.engine.eval_index_search_materialize_for_test(child, id, merged);
        }
        check(fixture.engine.eval_index_requests_for_test() == 0,
            "telemetry off counts no requests");
        check(fixture.engine.eval_index_hits_for_test() == 0,
            "telemetry off counts no hits");
        check(fixture.engine.eval_index_misses_for_test() == 0,
            "telemetry off counts no misses");
        check(fixture.engine.eval_index_replacements_for_test() == 0,
            "telemetry off counts no replacements");
        check(fixture.engine.eval_index_insertions_for_test() == 0,
            "telemetry off counts no insertions");
        check(fixture.engine.eval_index_tag_mismatches_for_test() == 0,
            "telemetry off counts no tag diagnostics");
        check(fixture.engine.eval_index_digest_for_test() == 1469598103934665603ull,
            "telemetry off leaves digest at seed");
        check(fixture.engine.eval_index_occupied_for_test() > 0,
            "index stays active with telemetry off");
        Fixture loud = make_fixture(true);
        engine_alias::NodeId loud_root = make_root(loud, "TJ");
        auto loud_children = loud.engine.expand(loud_root);
        check(loud_children.size() == children.size(),
            "telemetry setting changes no expansion");
    }
}

int main()
{
    test_finalize();
    test_geometry();
    test_root_insert_and_reset();
    test_single_insertion_and_depth_zero_merge();
    test_index_hit_path();
    test_exact_mismatch_collision();
    test_stale_and_range_rejection();
    test_enabled_disabled_identity();
    test_telemetry_lifecycle();
    if (failures == 0)
    {
        std::println("EVAL_INDEX_TRIAL_TESTS OK checks={}", checks);
        return 0;
    }
    std::println(stderr, "EVAL_INDEX_TRIAL_TESTS FAIL checks={} failures={}", checks, failures);
    return 1;
}
