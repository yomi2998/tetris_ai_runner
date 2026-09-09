#ifndef TETRIS_DIRECT_KEY_TRIAL
#error TETRIS_DIRECT_KEY_TRIAL is required
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
        check(fixture.engine.init(fixture.engine_config), "direct key fixture initializes");
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
        check(fixture.engine.init(fixture.engine_config),
            "direct key zero fixture initializes");
        return fixture;
    }

    engine_alias::NodeId make_root(Fixture &fixture, std::string_view queue_text,
        engine_alias::HoldState hold = {})
    {
        auto queue = engine_alias::parse_queue(queue_text);
        check(queue.has_value(), "direct key queue parses");
        if (!queue.has_value())
        {
            return engine_alias::no_node;
        }
        toj_policy::State state;
        return fixture.engine.set_root(tetris::Board{}, state, std::move(*queue), hold);
    }

    std::uint64_t materialized_fingerprint(Fixture &fixture,
        engine_alias::Child const &child)
    {
        return engine_alias::transposition_hash(
            fixture.engine.build_key_for_test(child));
    }

    bool materialized_verdict(Fixture &fixture, engine_alias::NodeId node,
        engine_alias::Child const &child)
    {
        return fixture.engine.key_from_node_for_test(node)
            == fixture.engine.build_key_for_test(child);
    }

    engine_alias::Child mutated(engine_alias::Child child,
        void (*apply)(engine_alias::Child &))
    {
        apply(child);
        return child;
    }

    void flip_acc(engine_alias::Child &child)
    {
        child.state.acc_value = child.state.acc_value == 1.5 ? 2.5 : 1.5;
    }

    void flip_like(engine_alias::Child &child)
    {
        child.state.like = child.state.like == 1.25 ? 2.25 : 1.25;
    }

    void flip_value(engine_alias::Child &child)
    {
        child.state.value = child.state.value == 3.5 ? 4.5 : 3.5;
    }

    void flip_death(engine_alias::Child &child)
    {
        child.state.death = child.state.death == 0 ? 1 : 0;
    }

    void flip_combo(engine_alias::Child &child)
    {
        child.state.combo = child.state.combo == 0 ? 2 : 0;
    }

    void flip_under_attack(engine_alias::Child &child)
    {
        child.state.under_attack = child.state.under_attack == 0 ? 1 : 0;
    }

    void flip_map_rise(engine_alias::Child &child)
    {
        child.state.map_rise = child.state.map_rise == 0 ? 1 : 0;
    }

    void flip_b2b(engine_alias::Child &child)
    {
        child.state.b2b = child.state.b2b == 0 ? 1 : 0;
    }

    void flip_t2(engine_alias::Child &child)
    {
        child.state.t2_value = child.state.t2_value == 0 ? 3 : 0;
    }

    void flip_t3(engine_alias::Child &child)
    {
        child.state.t3_value = child.state.t3_value == 0 ? 4 : 0;
    }

    void flip_cursor(engine_alias::Child &child)
    {
        child.cursor += 1;
    }

    void flip_hold_piece(engine_alias::Child &child)
    {
        if (child.hold.piece.has_value())
        {
            child.hold.piece = *child.hold.piece == engine_alias::Piece::T
                ? engine_alias::Piece::I
                : engine_alias::Piece::T;
        }
        else
        {
            child.hold.piece = engine_alias::Piece::T;
        }
    }

    void flip_hold_lock(engine_alias::Child &child)
    {
        child.hold.locked = !child.hold.locked;
    }

    void flip_played(engine_alias::Child &child)
    {
        child.played = child.played == engine_alias::Piece::T
            ? engine_alias::Piece::I
            : engine_alias::Piece::T;
    }

    void flip_source(engine_alias::Child &child)
    {
        child.source = child.source == engine_alias::BranchSource::Current
            ? engine_alias::BranchSource::Hold
            : engine_alias::BranchSource::Current;
    }

    void flip_candidate(engine_alias::Child &child,
        engine_alias::Child const &other)
    {
        child.candidate = other.candidate;
    }

    void acc_negative_zero(engine_alias::Child &child)
    {
        child.state.acc_value = -0.0;
    }

    void like_negative_zero(engine_alias::Child &child)
    {
        child.state.like = -0.0;
    }

    void value_negative_zero(engine_alias::Child &child)
    {
        child.state.value = -0.0;
    }

    void collect_children(Fixture &fixture, engine_alias::NodeId root,
        std::size_t target, std::vector<engine_alias::Child> &out)
    {
        std::vector<engine_alias::NodeId> frontier{root};
        std::size_t level = 0;
        while (out.size() < target && level < 4 && !frontier.empty())
        {
            std::vector<engine_alias::NodeId> next;
            for (auto id : frontier)
            {
                auto children = fixture.engine.expand(id);
                for (auto &child : children)
                {
                    out.push_back(child);
                    engine_alias::NodeId materialized =
                        fixture.engine.materialize(child);
                    if (materialized != engine_alias::no_node
                        && fixture.engine.node(materialized) != nullptr
                        && fixture.engine.node(materialized)->expandable)
                    {
                        next.push_back(materialized);
                    }
                    if (out.size() >= target)
                    {
                        break;
                    }
                }
                if (out.size() >= target)
                {
                    break;
                }
            }
            frontier = next;
            ++level;
        }
    }

    void fingerprint_check(Fixture &fixture, engine_alias::Child const &child,
        std::string const &what)
    {
        std::uint64_t const direct = fixture.engine.direct_key_hash_for_test(child);
        std::uint64_t const materialized = materialized_fingerprint(fixture, child);
        check(direct == materialized, what);
    }

    void verdict_check(Fixture &fixture, engine_alias::NodeId node,
        engine_alias::Child const &child, std::string const &what)
    {
        bool const direct = fixture.engine.direct_key_node_matches_for_test(node, child);
        bool const materialized = materialized_verdict(fixture, node, child);
        check(direct == materialized, what);
    }

    void run_layout_tests()
    {
        check(sizeof(engine_alias::DirectKeySourceContext) <= 64,
            "direct key context stays within one cache line");
        Fixture fixture = make_fixture(true, false);
        check(fixture.engine.retained_bytes() < engine_alias::engine_memory_budget,
            "direct key retained under cap");
        check(fixture.engine.retained_bytes()
                < engine_alias::engine_memory_budget - 65536,
            "direct key retained keeps residual margin");
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key root is zero");
        auto grown = fixture.engine.expand(0);
        check(!grown.empty(), "direct key expansion stages children");
        engine_alias::NodeId const first_id = fixture.engine.materialize(grown[0]);
        check(first_id != engine_alias::no_node, "direct key root child materializes");
        for (auto const &child : grown)
        {
            fingerprint_check(fixture, child, "direct key root fingerprint matches");
            verdict_check(fixture, first_id, child,
                "direct key root verdict matches");
        }
    }

    void run_fingerprint_corpus_tests()
    {
        struct Corpus
        {
            Fixture fixture;
            std::vector<engine_alias::Child> children;
        };
        std::vector<Corpus> corpora;
        for (std::size_t seed_index = 0; seed_index < 3; ++seed_index)
        {
            Corpus corpus{make_fixture(true, false), {}};
            engine_alias::HoldState hold;
            hold.piece = engine_alias::Piece::T;
            hold.locked = false;
            engine_alias::NodeId const root = seed_index == 0
                ? make_root(corpus.fixture, "TIJLOSZT")
                : seed_index == 1
                ? make_root(corpus.fixture, "TIJLOSZTTIJLOSZT", hold)
                : make_root(corpus.fixture, "LOSZTIJT");
            check(root == 0, "direct key corpus root is zero");
            collect_children(corpus.fixture, root, 8000, corpus.children);
            corpora.push_back(std::move(corpus));
        }
        std::size_t total = 0;
        for (auto &corpus : corpora)
        {
            total += corpus.children.size();
        }
        check(total >= 20000, "direct key corpus reaches target size");
        for (auto &corpus : corpora)
        {
            for (auto const &child : corpus.children)
            {
                fingerprint_check(corpus.fixture, child,
                    "direct key corpus fingerprint matches materialized");
            }
        }
        auto &stale = corpora[0];
        std::size_t const stale_count = stale.children.size() < 64
            ? stale.children.size()
            : 64;
        for (std::size_t i = 0; i < stale_count; ++i)
        {
            fingerprint_check(stale.fixture, stale.children[i],
                "direct key stale context fingerprint falls back correctly");
        }
    }

    void run_signed_zero_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key zero root is zero");
        auto grown = fixture.engine.expand(0);
        check(!grown.empty(), "direct key zero expansion stages children");
        engine_alias::Child base = grown[0];
        double const acc_values[] = { 0.0, -0.0, 1.5 };
        double const like_values[] = { 0.0, -0.0 };
        double const value_values[] = { 0.0, -0.0, 42.0 };
        for (double acc : acc_values)
        {
            for (double like : like_values)
            {
                for (double value : value_values)
                {
                    engine_alias::Child child = base;
                    child.state.acc_value = acc;
                    child.state.like = like;
                    child.state.value = value;
                    fingerprint_check(fixture, child,
                        "direct key signed zero fingerprint matches");
                    engine_alias::NodeId id = fixture.engine.materialize(child);
                    check(id != engine_alias::no_node,
                        "direct key signed zero node materializes");
                    if (id != engine_alias::no_node)
                    {
                        verdict_check(fixture, id, child,
                            "direct key signed zero verdict matches");
                    }
                }
            }
        }
        for (void (*apply)(engine_alias::Child &) :
            { acc_negative_zero, like_negative_zero, value_negative_zero })
        {
            engine_alias::Child child = mutated(base, apply);
            fingerprint_check(fixture, child,
                "direct key negative zero mutation fingerprint matches");
        }
    }

    void run_equality_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key equality root is zero");
        std::vector<engine_alias::Child> children;
        collect_children(fixture, 0, 2000, children);
        check(children.size() >= 1000, "direct key equality corpus fills");
        std::size_t const arena_size = fixture.engine.arena_size();
        check(arena_size > 1, "direct key equality arena grows");
        std::size_t const node_samples = arena_size - 1 < 25 ? arena_size - 1 : 25;
        std::size_t const child_samples = children.size() < 2000 ? children.size() : 2000;
        std::size_t pairs = 0;
        for (std::size_t c = 0; c < child_samples; ++c)
        {
            for (std::size_t n = 0; n < node_samples; ++n)
            {
                engine_alias::NodeId const node =
                    static_cast<engine_alias::NodeId>(1 + (n * 7) % (arena_size - 1));
                verdict_check(fixture, node, children[c],
                    "direct key pair verdict matches materialized");
                ++pairs;
            }
        }
        check(pairs >= 20000, "direct key equality pairs reach target size");
        engine_alias::NodeId const sample_node = 1;
        for (void (*apply)(engine_alias::Child &) :
            { flip_acc, flip_like, flip_value, flip_death, flip_combo,
                flip_under_attack, flip_map_rise, flip_b2b, flip_t2, flip_t3,
                flip_cursor, flip_hold_piece, flip_hold_lock, flip_played,
                flip_source, acc_negative_zero, like_negative_zero,
                value_negative_zero })
        {
            engine_alias::Child child = mutated(children[0], apply);
            verdict_check(fixture, sample_node, child,
                "direct key single field near miss matches");
            fingerprint_check(fixture, child,
                "direct key single field near miss fingerprint matches");
        }
        engine_alias::Child twice = mutated(children[0], flip_acc);
        flip_value(twice);
        verdict_check(fixture, sample_node, twice,
            "direct key double flip matches");
        if (children.size() > 1)
        {
            engine_alias::Child swapped = children[0];
            flip_candidate(swapped, children[children.size() - 1]);
            verdict_check(fixture, sample_node, swapped,
                "direct key candidate swap matches");
            fingerprint_check(fixture, swapped,
                "direct key candidate swap fingerprint matches");
        }
        check(materialized_verdict(fixture, sample_node, children[0])
            == fixture.engine.direct_key_node_matches_for_test(sample_node,
                children[0]),
            "direct key exact pair agrees");
    }

    void run_probe_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key probe root is zero");
        auto grown = fixture.engine.expand(0);
        check(grown.size() >= 2, "direct key probe stages children");
        engine_alias::Child const *first = nullptr;
        engine_alias::Child const *second = nullptr;
        for (std::size_t i = 0; i < grown.size() && second == nullptr; ++i)
        {
            for (std::size_t j = i + 1; j < grown.size(); ++j)
            {
                if (materialized_fingerprint(fixture, grown[i])
                    != materialized_fingerprint(fixture, grown[j]))
                {
                    first = &grown[i];
                    second = &grown[j];
                    break;
                }
            }
        }
        check(first != nullptr && second != nullptr,
            "direct key probe finds distinct key children");
        if (first == nullptr || second == nullptr)
        {
            return;
        }
        engine_alias::NodeId const first_id = fixture.engine.materialize(*first);
        engine_alias::NodeId const second_id = fixture.engine.materialize(*second);
        check(first_id != engine_alias::no_node && second_id != engine_alias::no_node,
            "direct key probe nodes materialize");
        std::uint64_t const first_fp = materialized_fingerprint(fixture, *first);
        std::uint64_t const second_fp = materialized_fingerprint(fixture, *second);
        engine_alias::TranspositionKey const first_key =
            fixture.engine.build_key_for_test(*first);
        engine_alias::TranspositionKey const second_key =
            fixture.engine.build_key_for_test(*second);
        check(fixture.engine.transposition_reinsert_for_test(first_fp, first_key,
                  first_id),
            "direct key reinsert first accepts");
        bool merged = true;
        engine_alias::NodeId node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(first_fp, *first, merged, node);
        check(merged && node == first_id, "direct key probe merges own entry");
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(first_fp,
            first_key, merged, node);
        check(merged && node == first_id, "materialized probe merges own entry");
        bool const walk_accepted = fixture.engine.transposition_reinsert_for_test(
            first_fp, second_key, second_id);
        check(walk_accepted, "direct key colliding reinsert accepts");
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(first_fp, *second, merged, node);
        check(merged && node == second_id,
            "direct key probe walks past mismatching match");
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(first_fp,
            second_key, merged, node);
        check(merged && node == second_id,
            "materialized probe walks past mismatching match");
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(second_fp, *second, merged,
            node);
        check(!merged && node == engine_alias::no_node,
            "direct key probe finds empty slot");
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(second_fp,
            second_key, merged, node);
        check(!merged && node == engine_alias::no_node,
            "materialized probe finds empty slot");
        std::uint32_t const epoch = fixture.engine.transposition_epoch_for_test();
        fixture.engine.set_transposition_epoch_for_test(epoch + 1);
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(first_fp, *first, merged, node);
        check(!merged && node == engine_alias::no_node,
            "direct key probe skips stale epoch");
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(first_fp,
            first_key, merged, node);
        check(!merged && node == engine_alias::no_node,
            "materialized probe skips stale epoch");
        fixture.engine.set_transposition_epoch_for_test(epoch);
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(first_fp, *first, merged, node);
        check(merged && node == first_id, "direct key probe restores epoch");
        std::uint64_t const rebuilds_before =
            fixture.engine.search_stats().probe_rebuilds;
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(first_fp, *first, merged, node);
        check(fixture.engine.search_stats().probe_rebuilds == rebuilds_before + 1,
            "direct key probe counts rebuilds");
        merged = false;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(first_fp,
            first_key, merged, node);
        check(fixture.engine.search_stats().probe_rebuilds == rebuilds_before + 2,
            "materialized probe counts rebuilds");
    }

    void run_exhaustion_tests()
    {
        Fixture fixture = make_zero_fixture(true);
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key exhaustion root is zero");
        auto grown = fixture.engine.expand(0);
        check(!grown.empty(), "direct key exhaustion stages children");
        engine_alias::NodeId const node_id = fixture.engine.materialize(grown[0]);
        check(node_id != engine_alias::no_node, "direct key exhaustion node exists");
        engine_alias::TranspositionKey const key =
            fixture.engine.build_key_for_test(grown[0]);
        std::size_t const entries = fixture.engine.transposition_table_size_for_test();
        check(entries == 1048576, "direct key exhaustion table is full size");
        for (std::size_t i = 0; i < entries; ++i)
        {
            if (!fixture.engine.transposition_reinsert_for_test(i, key, node_id))
            {
                break;
            }
        }
        check(fixture.engine.transposition_used() == entries,
            "direct key exhaustion table fills");
        std::uint64_t const stranger_fp = 0xDEADBEEFDEADBEEFull;
        bool merged = true;
        engine_alias::NodeId node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(stranger_fp, key,
            merged, node);
        check(!merged && node == engine_alias::no_node,
            "materialized probe reports exhaustion");
        check(fixture.engine.search_stats().transposition_exhausted,
            "materialized exhaustion flag sets");
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(stranger_fp, grown[0],
            merged, node);
        check(!merged && node == engine_alias::no_node,
            "direct probe reports exhaustion");
        check(fixture.engine.search_stats().transposition_exhausted,
            "direct exhaustion flag sets");
    }

    void run_context_tests()
    {
        Fixture fixture = make_fixture(true, false);
        engine_alias::HoldState hold;
        hold.piece = engine_alias::Piece::T;
        hold.locked = false;
        check(make_root(fixture, "TIJLOSZT", hold) == 0,
            "direct key context root is zero");
        auto grown = fixture.engine.expand(0);
        check(!grown.empty(), "direct key context stages children");
        check(fixture.engine.direct_key_context_count_for_test() == 2,
            "direct key context holds one per source");
        for (auto const &child : grown)
        {
            engine_alias::TranspositionKey const key =
                fixture.engine.build_key_for_test(child);
            bool matched = false;
            for (std::size_t i = 0; i < fixture.engine.direct_key_context_count_for_test();
                ++i)
            {
                engine_alias::DirectKeySourceContext const &context =
                    fixture.engine.direct_key_context_for_test(i);
                if (context.cursor == static_cast<std::uint16_t>(child.cursor)
                    && context.hold_piece
                        == (child.hold.piece.has_value()
                                ? static_cast<std::uint8_t>(*child.hold.piece)
                                : engine_alias::no_piece_code)
                    && context.hold_available == !child.hold.locked)
                {
                    matched = true;
                    check(context.depth == key.depth,
                        "direct key context depth matches key");
                    check(context.cursor == key.cursor,
                        "direct key context cursor matches key");
                    check(context.boundary_count == key.boundary_count,
                        "direct key context boundary count matches key");
                    check(context.boundary_bits == key.boundary_bits,
                        "direct key context boundary bits match key");
                    check(context.active_piece == key.active_piece,
                        "direct key context active piece matches key");
                    check(context.hold_piece == key.hold_piece,
                        "direct key context hold piece matches key");
                    check(context.hold_available == key.hold_available,
                        "direct key context hold availability matches key");
                    check(!context.root_fixed,
                        "direct key root context derives per child");
                }
            }
            check(matched, "direct key every child resolves a context");
        }
        std::vector<engine_alias::NodeId> deep_ids;
        for (auto const &child : grown)
        {
            engine_alias::NodeId id = fixture.engine.materialize(child);
            if (id != engine_alias::no_node && id != 0)
            {
                deep_ids.push_back(id);
            }
            if (deep_ids.size() >= 2)
            {
                break;
            }
        }
        check(!deep_ids.empty(), "direct key deep nodes materialize");
        auto deep_grown = fixture.engine.expand(deep_ids[0]);
        check(!deep_grown.empty(), "direct key deep expansion stages children");
        check(fixture.engine.direct_key_context_count_for_test() >= 1
                && fixture.engine.direct_key_context_count_for_test() <= 3,
            "direct key context count stays bounded");
        for (auto const &child : deep_grown)
        {
            engine_alias::TranspositionKey const key =
                fixture.engine.build_key_for_test(child);
            bool matched = false;
            for (std::size_t i = 0; i < fixture.engine.direct_key_context_count_for_test();
                ++i)
            {
                engine_alias::DirectKeySourceContext const &context =
                    fixture.engine.direct_key_context_for_test(i);
                if (context.cursor == static_cast<std::uint16_t>(child.cursor)
                    && context.hold_piece
                        == (child.hold.piece.has_value()
                                ? static_cast<std::uint8_t>(*child.hold.piece)
                                : engine_alias::no_piece_code)
                    && context.hold_available == !child.hold.locked)
                {
                    matched = true;
                    check(context.root_fixed && context.fixed_root_child
                            == fixture.engine.node(deep_ids[0])->root_child,
                        "direct key deep context fixes root child");
                    check(context.depth == key.depth,
                        "direct key deep context depth matches key");
                }
            }
            check(matched, "direct key deep child resolves a context");
            fingerprint_check(fixture, child,
                "direct key deep fingerprint matches materialized");
        }
    }

    void run_cold_path_tests()
    {
        Fixture fixture = make_fixture(true, false);
        check(make_root(fixture, "TIJLOSZT") == 0, "direct key cold root is zero");
        auto grown = fixture.engine.expand(0);
        check(grown.size() >= 2, "direct key cold stages children");
        engine_alias::Child const &first = grown[0];
        engine_alias::NodeId const first_id = fixture.engine.materialize(first);
        check(first_id != engine_alias::no_node, "direct key cold node exists");
        std::uint64_t const first_fp = materialized_fingerprint(fixture, first);
        engine_alias::TranspositionKey const first_key =
            fixture.engine.build_key_for_test(first);
        check(fixture.engine.transposition_reinsert_for_test(first_fp, first_key,
                  first_id),
            "direct key cold reinsert accepts");
        check(!fixture.engine.transposition_reinsert_for_test(first_fp, first_key,
                  first_id),
            "direct key cold reinsert rejects merge");
        std::uint64_t const stranger_fp = 0x1234567890ABCDEFull;
        check(fixture.engine.transposition_reinsert_for_test(stranger_fp, first_key,
                  first_id),
            "direct key cold reinsert accepts new fingerprint");
        bool merged = true;
        engine_alias::NodeId node = engine_alias::no_node;
        fixture.engine.direct_key_probe_outcome_for_test(stranger_fp, first, merged,
            node);
        check(merged && node == first_id,
            "direct key probe matches reinserted entry");
        merged = true;
        node = engine_alias::no_node;
        fixture.engine.direct_key_probe_materialized_outcome_for_test(stranger_fp,
            first_key, merged, node);
        check(merged && node == first_id,
            "materialized probe matches reinserted entry");
    }

    bool same_stats(engine_alias::SearchStats const &a, engine_alias::SearchStats const &b)
    {
        return a.widening_passes == b.widening_passes
            && a.expanded_parents == b.expanded_parents
            && a.enumeration_calls == b.enumeration_calls
            && a.raw_kernel_landings == b.raw_kernel_landings
            && a.unique_candidates == b.unique_candidates
            && a.rule_applications == b.rule_applications
            && a.eval_requests == b.eval_requests
            && a.eval_memo_hits == b.eval_memo_hits
            && a.eval_computed == b.eval_computed
            && a.cache_requests == b.cache_requests && a.cache_hits == b.cache_hits
            && a.cache_misses == b.cache_misses
            && a.cache_replacements == b.cache_replacements
            && a.policy_transitions == b.policy_transitions
            && a.materialized_nodes == b.materialized_nodes
            && a.transposition_merges == b.transposition_merges
            && a.promotions_refused == b.promotions_refused
            && a.probe_steps == b.probe_steps && a.probe_histogram == b.probe_histogram
            && a.probe_rebuilds == b.probe_rebuilds
            && a.pending_occupancy == b.pending_occupancy
            && a.transposition_exhausted == b.transposition_exhausted;
    }

    void run_search_tests()
    {
        Fixture left = make_zero_fixture(true);
        Fixture right = make_zero_fixture(true);
        check(make_root(left, "III") == 0, "direct key search left root is zero");
        check(make_root(right, "III") == 0, "direct key search right root is zero");
        check(left.engine.run(500), "direct key search left runs");
        check(right.engine.run(500), "direct key search right runs");
        check(same_stats(left.engine.search_stats(), right.engine.search_stats()),
            "direct key search repeats work vectors");
        check(left.engine.arena_size() == right.engine.arena_size(),
            "direct key search repeats arena size");
        check(left.engine.select_best() == right.engine.select_best(),
            "direct key search repeats selection");
        check(left.engine.transposition_used() == right.engine.transposition_used(),
            "direct key search repeats table use");
        check(left.engine.search_stats().materialized_nodes > 0,
            "direct key search materializes nodes");
        check(left.engine.search_stats().transposition_merges > 0,
            "direct key search performs merges");
        check(left.engine.search_stats().expanded_parents > 100,
            "direct key search exercises batch and tail paths");
        check(left.engine.select_best().has_value(),
            "direct key search repeats selection");
        tetris::Placement const spawn = tetris::Placement::unchecked(
            tetris::toj::spawn_x, tetris::toj::spawn_y, 0);
        check(left.engine.finalize(spawn).path_ok,
            "direct key search finalizes a path");
        Fixture quiet = make_zero_fixture(false);
        check(make_root(quiet, "III") == 0, "direct key quiet root is zero");
        check(quiet.engine.run(500), "direct key quiet search runs");
        check(quiet.engine.search_stats().materialized_nodes == 0
                && quiet.engine.search_stats().transposition_merges == 0
                && quiet.engine.search_stats().probe_steps == 0
                && quiet.engine.search_stats().probe_rebuilds == 0,
            "direct key telemetry off keeps counters quiet");
        check(quiet.engine.arena_size() == left.engine.arena_size(),
            "direct key telemetry off keeps arena size");
    }
}

int main()
{
    run_layout_tests();
    run_fingerprint_corpus_tests();
    run_signed_zero_tests();
    run_equality_tests();
    run_probe_tests();
    run_exhaustion_tests();
    run_context_tests();
    run_cold_path_tests();
    run_search_tests();
    std::println("direct_key_trial_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
