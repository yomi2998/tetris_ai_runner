#ifndef TETRIS_CHILD_SOA_TRIAL
#error TETRIS_CHILD_SOA_TRIAL is required
#endif
#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
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

    Fixture make_fixture(bool telemetry, bool timers)
    {
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 5;
        fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.telemetry_enabled = telemetry;
        fixture.engine_config.timers_enabled = timers;
        check(fixture.engine.init(fixture.engine_config), "soa fixture engine initializes");
        return fixture;
    }

    Fixture make_zero_fixture(bool telemetry)
    {
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 0;
        fixture.policy_config.parameters = toj_policy::Parameters{};
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.telemetry_enabled = telemetry;
        fixture.engine_config.timers_enabled = false;
        check(fixture.engine.init(fixture.engine_config), "soa zero fixture initializes");
        return fixture;
    }

    engine_alias::NodeId make_root(Fixture &fixture, std::string_view queue_text)
    {
        auto queue = engine_alias::parse_queue(queue_text);
        check(queue.has_value(), "soa root queue parses");
        if (!queue.has_value())
        {
            return engine_alias::no_node;
        }
        engine_alias::HoldState hold;
        hold.locked = true;
        toj_policy::State state;
        return fixture.engine.set_root(tetris::Board{}, state, std::move(*queue), hold);
    }

    bool same_state(toj_policy::State const &a, toj_policy::State const &b)
    {
        return a.death == b.death && a.combo == b.combo
            && a.under_attack == b.under_attack && a.map_rise == b.map_rise
            && a.b2b == b.b2b && a.t2_value == b.t2_value && a.t3_value == b.t3_value
            && a.acc_value == b.acc_value && a.like == b.like && a.value == b.value;
    }

    bool same_evaluation(toj_policy::Evaluation const &a, toj_policy::Evaluation const &b)
    {
        return std::bit_cast<std::uint64_t>(a.value) == std::bit_cast<std::uint64_t>(b.value)
            && a.t2_value == b.t2_value && a.t3_value == b.t3_value;
    }

    bool same_child(engine_alias::Child const &a, engine_alias::Child const &b)
    {
        return a.board == b.board && same_state(a.state, b.state)
            && same_evaluation(a.evaluation, b.evaluation) && a.cursor == b.cursor
            && a.hold.piece == b.hold.piece && a.hold.locked == b.hold.locked
            && a.outcome == b.outcome && a.parent == b.parent
            && a.candidate == b.candidate && a.played == b.played
            && a.source == b.source && a.expandable == b.expandable;
    }

    bool same_node(engine_alias::Node const &a, engine_alias::Node const &b)
    {
        return a.board == b.board && same_state(a.policy, b.policy)
            && same_evaluation(a.evaluation, b.evaluation) && a.cursor == b.cursor
            && a.depth == b.depth && a.hold.piece == b.hold.piece
            && a.hold.locked == b.hold.locked && a.incoming == b.incoming
            && a.played == b.played && a.parent == b.parent
            && a.root_child == b.root_child && a.has_incoming == b.has_incoming
            && a.source == b.source && a.expandable == b.expandable;
    }

    bool same_stats(engine_alias::SearchStats const &a, engine_alias::SearchStats const &b)
    {
        if (a.widening_passes != b.widening_passes) return false;
        if (a.expanded_parents != b.expanded_parents) return false;
        if (a.enumeration_calls != b.enumeration_calls) return false;
        if (a.raw_kernel_landings != b.raw_kernel_landings) return false;
        if (a.unique_candidates != b.unique_candidates) return false;
        if (a.rule_applications != b.rule_applications) return false;
        if (a.eval_requests != b.eval_requests) return false;
        if (a.eval_memo_hits != b.eval_memo_hits) return false;
        if (a.eval_computed != b.eval_computed) return false;
        if (a.cache_requests != b.cache_requests) return false;
        if (a.cache_hits != b.cache_hits) return false;
        if (a.cache_misses != b.cache_misses) return false;
        if (a.cache_replacements != b.cache_replacements) return false;
        if (a.policy_transitions != b.policy_transitions) return false;
        if (a.materialized_nodes != b.materialized_nodes) return false;
        if (a.transposition_merges != b.transposition_merges) return false;
        if (a.promotions_refused != b.promotions_refused) return false;
        if (a.probe_steps != b.probe_steps) return false;
        if (a.probe_rebuilds != b.probe_rebuilds) return false;
        if (a.probe_histogram != b.probe_histogram) return false;
        return true;
    }

    void run_layout_tests()
    {
        check(sizeof(engine_alias::Child) == 192, "public child stays 192 bytes");
        check(sizeof(engine_alias::Node) == 192, "node layout stays 192 bytes");
        check(sizeof(tetris::Board) == 64, "board stays 64 bytes");
        check(sizeof(engine_alias::ChildSoaMeta) <= 128, "soa meta stays compact");
        check(sizeof(tetris::Board) + sizeof(engine_alias::ChildSoaMeta)
            <= sizeof(engine_alias::Child), "soa pair fits child budget");
        check(alignof(tetris::Board) == 64, "board keeps kernel alignment");
        Fixture fixture = make_fixture(true, false);
        check(fixture.engine.child_soa_size_for_test() == 0, "soa staging starts empty");
        check(fixture.engine.child_soa_lockstep_for_test(), "soa vectors start lockstep");
        check(fixture.engine.child_soa_board_alignment_for_test(),
            "soa board storage aligned");
        check(fixture.engine.child_soa_board_capacity_for_test()
            >= engine_alias::max_children_per_parent, "soa board capacity reserved");
        check(fixture.engine.child_soa_meta_capacity_for_test()
            >= engine_alias::max_children_per_parent, "soa meta capacity reserved");
        check(fixture.engine.child_soa_capacity_for_test()
            >= engine_alias::max_children_per_parent, "soa shared capacity reserved");
        std::uint64_t const reserved = fixture.engine.child_soa_reserved_for_test();
        std::uint64_t const legacy =
            static_cast<std::uint64_t>(fixture.engine.child_soa_board_capacity_for_test())
            * sizeof(engine_alias::Child);
        check(reserved <= legacy, "soa reserved does not exceed legacy child bytes");
        check(reserved > 0, "soa reserved accounts both vectors");
        std::uint64_t const retained = fixture.engine.retained_bytes();
        check(retained < engine_alias::engine_memory_budget, "soa retained under cap");
        check(engine_alias::engine_memory_budget - retained >= 65536,
            "soa retained keeps residual margin");
        check(fixture.engine_config.arena_capacity
            == engine_alias::default_arena_capacity, "soa keeps default arena capacity");
    }

    void run_gather_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "soa gather root is zero");
        std::vector<engine_alias::Child> grown = fixture.engine.expand(0);
        check(!grown.empty(), "soa root expansion stages children");
        check(fixture.engine.child_soa_size_for_test() == grown.size(),
            "soa size matches gathered count");
        check(fixture.engine.child_soa_lockstep_for_test(), "soa vectors stay lockstep");
        for (std::size_t i = 0; i < grown.size(); ++i)
        {
            engine_alias::Child gathered = fixture.engine.child_soa_gather_for_test(i);
            check(same_child(gathered, grown[i]), "soa gather matches public child");
        }
        for (std::size_t i = 0; i < grown.size(); ++i)
        {
            check(fixture.engine.child_soa_key_for_test(i)
                == fixture.engine.build_key_for_test(grown[i]),
                "soa key matches child key");
        }
    }

    void run_dedup_order_tests()
    {
        Fixture first = make_fixture(true, false);
        Fixture second = make_fixture(true, false);
        check(make_root(first, "TIJLOSZT") == 0, "soa order first root is zero");
        check(make_root(second, "TIJLOSZT") == 0, "soa order second root is zero");
        std::vector<engine_alias::Child> a = first.engine.expand(0);
        std::vector<engine_alias::Child> b = second.engine.expand(0);
        check(a.size() == b.size(), "soa repeat expansion keeps count");
        bool order_same = a.size() == b.size();
        for (std::size_t i = 0; order_same && i < a.size(); ++i)
        {
            order_same = same_child(a[i], b[i]);
        }
        check(order_same, "soa repeat expansion keeps order");
        Fixture quiet = make_fixture(false, false);
        check(make_root(quiet, "TIJLOSZT") == 0, "soa quiet root is zero");
        std::vector<engine_alias::Child> c = quiet.engine.expand(0);
        check(c.size() == a.size(), "soa telemetry off keeps count");
        bool quiet_same = c.size() == a.size();
        for (std::size_t i = 0; quiet_same && i < a.size(); ++i)
        {
            quiet_same = same_child(c[i], a[i]);
        }
        check(quiet_same, "soa telemetry off keeps order");
        Fixture untimed = make_fixture(true, true);
        check(make_root(untimed, "TIJLOSZT") == 0, "soa timed root is zero");
        std::vector<engine_alias::Child> d = untimed.engine.expand(0);
        check(d.size() == a.size(), "soa timers on keeps count");
        bool timed_same = d.size() == a.size();
        for (std::size_t i = 0; timed_same && i < a.size(); ++i)
        {
            timed_same = same_child(d[i], a[i]);
        }
        check(timed_same, "soa timers on keeps order");
        bool survivor_unique = true;
        for (std::size_t i = 0; survivor_unique && i < a.size(); ++i)
        {
            for (std::size_t j = i + 1; j < a.size(); ++j)
            {
                if (a[i].board == a[j].board && a[i].outcome == a[j].outcome)
                {
                    survivor_unique = false;
                }
            }
        }
        check(survivor_unique, "soa staged survivors keep first win");
        check(first.engine.expand(999999).empty(), "soa invalid parent expands empty");
        check(first.engine.child_soa_size_for_test() == 0,
            "soa invalid parent leaves staging empty");
    }

    void run_clear_move_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "soa clear root is zero");
        (void)fixture.engine.expand(0);
        check(fixture.engine.child_soa_size_for_test() > 0, "soa staging fills");
        check(make_root(fixture, "SZOTIJL") != engine_alias::no_node,
            "soa second root sets");
        check(fixture.engine.child_soa_size_for_test() == 0, "soa reset clears staging");
        check(fixture.engine.child_soa_lockstep_for_test(), "soa reset keeps lockstep");
        Fixture moved = make_fixture(true, false);
        check(make_root(moved, "TIJLOSZT") == 0, "soa move root is zero");
        engine_alias::Engine relocated = std::move(moved.engine);
        std::vector<engine_alias::Child> grown = relocated.expand(0);
        check(!grown.empty(), "soa moved engine expands");
        check(relocated.child_soa_size_for_test() == grown.size(),
            "soa moved engine keeps staging count");
        check(relocated.child_soa_lockstep_for_test(), "soa moved engine keeps lockstep");
        for (std::size_t i = 0; i < grown.size(); ++i)
        {
            check(same_child(relocated.child_soa_gather_for_test(i), grown[i]),
                "soa moved engine gather matches");
        }
    }

    void run_materialize_merge_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "soa materialize root is zero");
        std::vector<engine_alias::Child> grown = fixture.engine.expand(0);
        check(!grown.empty(), "soa materialize has staged children");
        std::size_t const limit = grown.size() < 8 ? grown.size() : 8;
        for (std::size_t i = 0; i < limit; ++i)
        {
            engine_alias::NodeId id = fixture.engine.child_soa_materialize_for_test(i);
            check(id != engine_alias::no_node, "soa slot materializes");
            engine_alias::NodeId expect = fixture.engine.materialize(grown[i]);
            check(expect != engine_alias::no_node, "soa public materialize pairs");
            engine_alias::Node const *got = fixture.engine.node(id);
            engine_alias::Node const *want = fixture.engine.node(expect);
            check(got != nullptr && want != nullptr, "soa materialized nodes readable");
            if (got != nullptr && want != nullptr)
            {
                check(same_node(*got, *want), "soa node matches child node");
                check(fixture.engine.key_from_node_for_test(id)
                    == fixture.engine.key_from_node_for_test(expect),
                    "soa node keys match");
            }
        }
        Fixture left = make_zero_fixture(true);
        Fixture right = make_zero_fixture(true);
        check(make_root(left, "III") == 0, "soa search left root is zero");
        check(make_root(right, "III") == 0, "soa search right root is zero");
        check(left.engine.run(500),
            "soa search left runs");
        check(right.engine.run(500),
            "soa search right runs");
        check(same_stats(left.engine.search_stats(), right.engine.search_stats()),
            "soa search repeats work vectors");
        check(left.engine.arena_size() == right.engine.arena_size(),
            "soa search repeats arena size");
        check(left.engine.select_best() == right.engine.select_best(),
            "soa search repeats selection");
        check(left.engine.transposition_used() == right.engine.transposition_used(),
            "soa search repeats merge table use");
    }

    void run_search_telemetry_tests()
    {
        Fixture loud = make_zero_fixture(true);
        Fixture quiet = make_zero_fixture(false);
        check(make_root(loud, "III") == 0, "soa loud search root is zero");
        check(make_root(quiet, "III") == 0, "soa quiet search root is zero");
        check(loud.engine.run(500),
            "soa loud search runs");
        check(quiet.engine.run(500),
            "soa quiet search runs");
        check(loud.engine.arena_size() == quiet.engine.arena_size(),
            "soa telemetry off keeps arena size");
        check(loud.engine.select_best() == quiet.engine.select_best(),
            "soa telemetry off keeps selection");
        check(quiet.engine.search_stats().materialized_nodes == 0
            && quiet.engine.search_stats().transposition_merges == 0,
            "soa telemetry off keeps work counters quiet");
        check(loud.engine.search_stats().materialized_nodes
            == loud.engine.arena_size() - 1,
            "soa loud materialized matches arena growth");
        check(quiet.engine.search_stats().eval_requests == 0
            && quiet.engine.search_stats().eval_computed == 0,
            "soa telemetry off keeps counters quiet");
    }

    void run_production_partial_tests()
    {
        Fixture left = make_fixture(true, false);
        Fixture right = make_fixture(true, false);
        check(make_root(left, "TILOSZ") == 0, "soa partial left root is zero");
        check(make_root(right, "TILOSZ") == 0, "soa partial right root is zero");
        (void)left.engine.run(200);
        (void)right.engine.run(200);
        check(same_stats(left.engine.search_stats(), right.engine.search_stats()),
            "soa partial search repeats work vectors");
        check(left.engine.arena_size() == right.engine.arena_size(),
            "soa partial search repeats arena size");
        check(left.engine.select_best() == right.engine.select_best(),
            "soa partial search repeats selection");
    }

    void run_overflow_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "soa overflow root is zero");
        check(fixture.engine.child_soa_size_for_test() == 0,
            "soa overflow starts empty");
        std::size_t const capacity = fixture.engine.child_soa_capacity_for_test();
        check(capacity >= engine_alias::max_children_per_parent,
            "soa overflow capacity covers budget");
        engine_alias::Child proto;
        proto.parent = 0;
        std::size_t pushed = 0;
        while (fixture.engine.child_soa_try_push_for_test(proto))
        {
            ++pushed;
        }
        check(pushed == capacity, "soa overflow fills exactly shared capacity");
        check(fixture.engine.child_soa_size_for_test() == capacity,
            "soa overflow size matches capacity");
        check(fixture.engine.child_soa_lockstep_for_test(),
            "soa overflow keeps lockstep at capacity");
        std::size_t const board_cap =
            fixture.engine.child_soa_board_capacity_for_test();
        std::size_t const meta_cap = fixture.engine.child_soa_meta_capacity_for_test();
        check(fixture.engine.child_soa_try_push_for_test(proto) == false,
            "soa overflow rejects one extra push");
        check(fixture.engine.child_soa_size_for_test() == capacity,
            "soa rejected push leaves size unchanged");
        check(fixture.engine.child_soa_board_capacity_for_test() == board_cap
            && fixture.engine.child_soa_meta_capacity_for_test() == meta_cap,
            "soa rejected push leaves capacities unchanged");
        check(fixture.engine.child_soa_lockstep_for_test(),
            "soa rejected push keeps lockstep");
    }

    void run_depth_zero_merge_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "soa merge root is zero");
        std::vector<engine_alias::Child> grown = fixture.engine.expand(0);
        check(!grown.empty(), "soa merge has staged children");
        std::size_t const arena_before = fixture.engine.arena_size();
        std::size_t const used_before = fixture.engine.transposition_used();
        std::size_t const mat_before =
            fixture.engine.search_stats().materialized_nodes;
        std::size_t const merges_before =
            fixture.engine.search_stats().transposition_merges;
        engine_alias::NodeId first_id = engine_alias::no_node;
        bool first_merged = true;
        check(fixture.engine.child_soa_search_materialize_for_test(0, first_id,
            first_merged), "soa merge first insert runs");
        check(first_merged == false, "soa merge first insert registers");
        check(first_id != engine_alias::no_node, "soa merge first insert has id");
        check(fixture.engine.arena_size() == arena_before + 1,
            "soa merge first insert grows arena");
        check(fixture.engine.search_stats().materialized_nodes == mat_before + 1,
            "soa merge first insert counts materialized");
        engine_alias::NodeId second_id = engine_alias::no_node;
        bool second_merged = false;
        check(fixture.engine.child_soa_search_materialize_for_test(0, second_id,
            second_merged), "soa merge second insert runs");
        check(second_merged, "soa merge second insert merges");
        check(second_id == first_id, "soa merge second insert finds first node");
        check(fixture.engine.arena_size() == arena_before + 1,
            "soa merge discards temporary node");
        check(fixture.engine.transposition_used() == used_before + 1,
            "soa merge holds one table slot");
        check(fixture.engine.search_stats().materialized_nodes == mat_before + 1,
            "soa merge counts no extra materialized node");
        check(fixture.engine.search_stats().transposition_merges == merges_before + 1,
            "soa merge counts one merge");
    }
}

int main()
{
    run_layout_tests();
    run_gather_tests();
    run_dedup_order_tests();
    run_clear_move_tests();
    run_materialize_merge_tests();
    run_overflow_tests();
    run_depth_zero_merge_tests();
    run_production_partial_tests();
    run_search_telemetry_tests();
    std::println("child_soa_trial_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
