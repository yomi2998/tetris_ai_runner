#include "profile_value_support.h"
#include "tetris_board.h"
#include "tetris_core.h"
#include "tetris_engine.h"
#include "tetris_types.h"
#include "toj_policy.h"
#include "toj_rule.h"
#include "ai_zzz.h"
#include "rule_toj.h"
#include "search_tspin.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <print>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace engine_alias = tetris_engine;
namespace toj_alias = tetris::toj;
namespace support = profile_value;

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

    struct LegacyRefill
    {
        std::mt19937 rng;
        std::vector<char> next;

        explicit LegacyRefill(std::uint32_t seed)
            : rng(seed)
        {
        }

        void step(std::size_t maxdepth, bool pop_extra, bool die)
        {
            if (die)
            {
                next.clear();
            }
            else
            {
                if (!next.empty())
                {
                    next.erase(next.begin());
                }
                if (pop_extra && !next.empty())
                {
                    next.erase(next.begin());
                }
            }
            while (next.size() <= maxdepth)
            {
                for (char c : support::bag_order)
                {
                    next.push_back(c);
                }
                std::shuffle(next.end() - support::bag_size, next.end(), rng);
            }
        }
    };

    void run_generator_tests()
    {
        for (std::uint32_t seed : { 1u, 2u, 3u })
        {
            for (std::size_t maxdepth : { 0u, 1u, 6u })
            {
                support::Scenario scenario(seed);
                LegacyRefill legacy(seed);
                bool ok = true;
                for (int move = 0; move < 60; ++move)
                {
                    bool die = (move % 17 == 16);
                    bool extra = move > 0 && (move - 1) % 5 == 4 && !die;
                    if (die)
                    {
                        scenario.reset_on_death();
                    }
                    else if (extra)
                    {
                        scenario.pop_played_extra();
                    }
                    scenario.start_move(maxdepth);
                    legacy.step(maxdepth, extra, die);
                    if (scenario.queue() != legacy.next)
                    {
                        ok = false;
                        break;
                    }
                }
                check(ok, "scenario stream matches the legacy refill schedule");
            }
        }
        support::Scenario scenario(7);
        scenario.start_move(6);
        check(scenario.queue().size() == 7, "first refill deals exactly one bag");
        check(scenario.current() == scenario.queue().front(), "current is the queue front");
    }

    int legacy_attack(int clear, int spin, bool board_empty, int &combo, int &b2b)
    {
        int attack = 0;
        switch (clear)
        {
        case 0:
            combo = 0;
            break;
        case 1:
            if (spin == 1) { attack += 1 + b2b; b2b = 1; }
            else if (spin == 2) { attack += 2 + b2b; b2b = 1; }
            else { b2b = 0; }
            attack += combo_table[std::min(9, ++combo)];
            break;
        case 2:
            if (spin != 0) { attack += 4 + b2b; b2b = 1; }
            else { attack += 1; b2b = 0; }
            attack += combo_table[std::min(9, ++combo)];
            break;
        case 3:
            if (spin != 0) { attack += 6 + b2b * 2; b2b = 1; }
            else { attack += 2; b2b = 0; }
            attack += combo_table[std::min(9, ++combo)];
            break;
        case 4:
            attack += combo_table[std::min(9, ++combo)] + 4 + b2b;
            b2b = 1;
            break;
        default:
            break;
        }
        if (board_empty) attack += 6;
        return attack;
    }

    void run_accounting_tests()
    {
        bool ok = true;
        for (int clear = 0; clear <= 4; ++clear)
        {
            for (int spin = 0; spin <= 2; ++spin)
            {
                for (int empty = 0; empty <= 1; ++empty)
                {
                    for (int combo_in = 0; combo_in <= 12; ++combo_in)
                    {
                        for (int b2b_in = 0; b2b_in <= 1; ++b2b_in)
                        {
                            int combo_a = combo_in;
                            int b2b_a = b2b_in;
                            int combo_b = combo_in;
                            int b2b_b = b2b_in;
                            int attack_a = support::score_attack(clear,
                                static_cast<support::SpinClass>(spin), empty != 0,
                                combo_a, b2b_a);
                            int attack_b = legacy_attack(
                                clear, spin, empty != 0, combo_b, b2b_b);
                            if (attack_a != attack_b || combo_a != combo_b
                                || b2b_a != b2b_b)
                            {
                                ok = false;
                            }
                        }
                    }
                }
            }
        }
        check(ok, "value accounting matches the legacy switch over the outcome space");
    }

    std::array<std::uint16_t, 48> shelf_rows()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 18; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        return rows;
    }

    std::array<std::uint16_t, 48> pocket_rows()
    {
        std::array<std::uint16_t, 48> rows = shelf_rows();
        rows[27] = 0x1f8;
        for (int y = 28; y <= 31; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x108;
        }
        rows[32] = 0x1f8;
        return rows;
    }

    void run_root_seed_tests()
    {
        using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ,
            search_tspin::Search>;
        Engine legacy;
        check(legacy.prepare(10, 40), "legacy engine prepares for seed comparison");
        toj_policy::Config config;
        config.combo_table = combo_table;
        config.combo_table_max = 10;
        config.safe = 0;
        config.parameters = toj_policy::Parameters::production_defaults();
        toj_policy::Policy policy;
        policy.init(&config);
        std::vector<std::array<std::uint16_t, 48>> boards = {
            {}, shelf_rows(), pocket_rows(),
        };
        bool ok = true;
        for (auto const &rows : boards)
        {
            tetris::Board board = tetris::Board::from_rows(rows);
            m_tetris::TetrisMap map(10, 40);
            int roof = 0;
            for (int y = 0; y < 40; ++y)
            {
                map.row[y] = rows[static_cast<std::size_t>(y)];
                for (int x = 0; x < 10; ++x)
                {
                    if (map.row[y] & (1u << x))
                    {
                        map.top[x] = y + 1;
                        roof = std::max(roof, y + 1);
                        ++map.count;
                    }
                }
            }
            map.roof = roof;
            for (char piece_char : { 'T', 'I', 'O' })
            {
                auto piece = tetris::try_from_char(piece_char);
                int8_t legacy_safe = legacy.ai()->get_safe(map, piece_char);
                int8_t value_safe = policy.safe_margin(board, *piece);
                if (legacy_safe != value_safe)
                {
                    ok = false;
                }
                std::int16_t legacy_t2 = 0;
                std::int16_t legacy_t3 = 0;
                ai_zzz::TOJ::Status::init_t_value(map, legacy_t2, legacy_t3);
                toj_policy::Evaluation eval = policy.evaluate(board);
                if (eval.t2_value != legacy_t2 || eval.t3_value != legacy_t3)
                {
                    ok = false;
                }
            }
        }
        check(ok, "root safe margin and T seed match the legacy policy reads");
    }

    void run_schema_tests()
    {
        support::V3Row row;
        row.moves = 3;
        row.mode = "iters";
        row.telemetry = "on";
        row.evals = 10;
        std::string line = support::format_v3(row);
        check(line.rfind("PROFILE_V3 ", 0) == 0, "V3 record carries the schema token");
        std::istringstream fields(line);
        std::string field;
        std::vector<std::string> keys;
        while (fields >> field)
        {
            auto pos = field.find('=');
            keys.push_back(pos == std::string::npos ? field : field.substr(0, pos));
        }
        std::vector<std::string> expected = {
            "PROFILE_V3", "moves", "total_s", "min_ms", "median_ms", "p95_ms",
            "p99_ms", "max_ms", "evals", "transitions", "searches", "dead_moves",
            "games", "node_live_delta_bytes", "evals_per_s", "transitions_per_s",
            "searches_per_s", "warmup_moves", "seed", "iters", "maxdepth",
            "budget_ms", "mode", "telemetry", "emove_min_ms", "emove_med_ms",
            "emove_p95_ms", "emove_p99_ms", "emove_max_ms", "setup_ms",
            "setup_eval_ms", "run_ms", "path_ms", "apply_ms", "init_ms", "parents",
            "parent_ns", "widening_iters", "enum_ns", "raw_landings",
            "unique_candidates", "rule_transitions", "rule_ns", "eval_hit_ns",
            "eval_miss_ns", "eval_memo_hits", "eval_computed", "cache_requests",
            "cache_hits", "cache_misses", "cache_replacements",
            "materialized_nodes", "materialize_ns", "policy_ns",
            "transposition_merges", "promotions_refused", "pending_end_max",
            "texhaust_moves", "path_calls", "path_states", "path_find_ns",
            "path_replay_ns", "replay_failures", "mem_retained_bytes",
            "arena_reserved_bytes", "idmap_reserved_bytes",
            "raw_unique_ratio_x1000",
        };
        check(keys == expected, "V3 field order matches the contract");
        check(line.find("evals=10") != std::string::npos, "numeric counts render");
        support::V3Row off;
        off.mode = "ms";
        off.telemetry = "off";
        std::string off_line = support::format_v3(off);
        check(off_line.find("evals=na") != std::string::npos, "disabled counts render na");
        check(off_line.find("total_s=0.000") != std::string::npos,
            "boundary wall time stays numeric when off");
    }

    struct ValueFixture
    {
        toj_policy::Config policy_config;
        engine_alias::EngineConfig engine_config;
        engine_alias::Engine engine;
    };

    ValueFixture make_value_fixture(bool telemetry)
    {
        ValueFixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 5;
        fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.telemetry_enabled = telemetry;
        check(fixture.engine.init(fixture.engine_config), "value fixture initializes");
        return fixture;
    }

    engine_alias::Queue test_queue(std::string_view pieces)
    {
        engine_alias::Queue queue;
        for (char c : pieces)
        {
            auto piece = tetris::try_from_char(c);
            if (piece.has_value())
            {
                queue.pieces.push_back(*piece);
                queue.boundary.push_back(false);
            }
        }
        return queue;
    }

    void run_lifecycle_tests()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 10; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        tetris::Board board = tetris::Board::from_rows(rows);
        toj_policy::State state;
        {
            ValueFixture fixture = make_value_fixture(true);
            engine_alias::HoldState hold;
            check(fixture.engine.set_root(board, state, test_queue("IIIII"), hold)
                    != engine_alias::no_node,
                "lifecycle root installs");
            check(fixture.engine.search_stats().widening_passes == 0,
                "fresh root reports zero search counters before running");
            fixture.engine.run(engine_alias::SearchBudget::by_iterations(4));
            auto stats = fixture.engine.search_stats();
            check(stats.widening_passes == 4, "completed root snapshot counts four passes");
            check(stats.eval_requests > 0, "completed root snapshot counts evaluations");
            auto timers = fixture.engine.component_timers();
            check(timers.enum_ns > 0, "enumeration timer accumulates while enabled");
            check(timers.eval_hit_ns + timers.eval_miss_ns > 0,
                "evaluation timers accumulate while enabled");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "lifecycle search selects");
            engine_alias::FinalResult result = fixture.engine.finalize(
                tetris::Placement::unchecked(
                    toj_alias::spawn_x, toj_alias::spawn_y, 0));
            check(result.has_selection, "lifecycle search finalizes");
            auto path_stats = fixture.engine.path_telemetry();
            check(path_stats.calls == 1, "path telemetry counts the finalization");
            check(fixture.engine.component_timers().path_find_ns > 0,
                "path find timer accumulates while enabled");
        }
        {
            ValueFixture on = make_value_fixture(true);
            ValueFixture off = make_value_fixture(false);
            engine_alias::HoldState hold;
            check(on.engine.set_root(board, state, test_queue("IIIII"), hold)
                        != engine_alias::no_node
                    && off.engine.set_root(board, state, test_queue("IIIII"), hold)
                        != engine_alias::no_node,
                "toggle roots install");
            on.engine.run(engine_alias::SearchBudget::by_iterations(4));
            off.engine.run(engine_alias::SearchBudget::by_iterations(4));
            auto on_stats = on.engine.search_stats();
            auto off_stats = off.engine.search_stats();
            check(off_stats.widening_passes == 0 && off_stats.eval_requests == 0
                    && off_stats.expanded_parents == 0,
                "disabled instrumentation reports zero search counters");
            check(off.engine.component_timers().enum_ns == 0
                    && off.engine.component_timers().materialize_ns == 0,
                "disabled instrumentation reports zero timers");
            auto on_selection = on.engine.select_best();
            auto off_selection = off.engine.select_best();
            check(on_selection.has_value() && off_selection.has_value()
                    && on_selection->root_child == off_selection->root_child
                    && on_selection->evidence == off_selection->evidence
                    && on.engine.arena_size() == off.engine.arena_size(),
                "telemetry toggle preserves fixed-iteration search decisions");
            check(on_stats.widening_passes == 4, "enabled counters still count");
        }
        {
            ValueFixture fixture = make_value_fixture(true);
            engine_alias::HoldState hold;
            check(fixture.engine.set_root(board, state, test_queue("IIIII"), hold)
                    != engine_alias::no_node,
                "first root installs");
            fixture.engine.run(engine_alias::SearchBudget::by_iterations(2));
            check(fixture.engine.search_stats().widening_passes == 2,
                "first root counts its own passes");
            check(fixture.engine.set_root(board, state, test_queue("IIIII"), hold)
                    != engine_alias::no_node,
                "second root installs");
            check(fixture.engine.search_stats().widening_passes == 0,
                "replacement root resets counters instead of differencing");
        }
        {
            ValueFixture fixture = make_value_fixture(true);
            engine_alias::HoldState hold;
            engine_alias::Queue empty;
            check(fixture.engine.set_root(board, state, empty, hold)
                    == engine_alias::no_node,
                "empty queue rejects the root");
            check(fixture.engine.search_stats().widening_passes == 0,
                "rejected root reports zero search counters");
            engine_alias::Queue huge;
            for (int i = 0; i < 257; ++i)
            {
                huge.pieces.push_back(tetris::Piece::T);
                huge.boundary.push_back(false);
            }
            check(fixture.engine.set_root(board, state, huge, hold)
                    == engine_alias::no_node,
                "oversized queue rejects the root");
        }
        {
            ValueFixture direct = make_value_fixture(true);
            ValueFixture assoc = make_value_fixture(true);
            direct.engine_config.cache.layout =
                engine_alias::CacheConfig::Layout::DirectMapped;
            direct.engine_config.cache.entries = 64;
            assoc.engine_config.cache.layout =
                engine_alias::CacheConfig::Layout::SetAssociative;
            assoc.engine_config.cache.entries = 64;
            assoc.engine_config.cache.ways = 4;
            check(direct.engine.init(direct.engine_config), "direct cache initializes");
            check(assoc.engine.init(assoc.engine_config), "associative cache initializes");
            for (ValueFixture *fixture : { &direct, &assoc })
            {
                engine_alias::HoldState hold;
                check(fixture->engine.set_root(board, state, test_queue("TIIII"), hold)
                        != engine_alias::no_node,
                    "cache-identity root installs");
                fixture->engine.run(engine_alias::SearchBudget::by_iterations(6));
                auto stats = fixture->engine.search_stats();
                bool identity = stats.eval_requests
                    == stats.eval_memo_hits + stats.cache_requests;
                identity = identity
                    && stats.cache_requests
                        == stats.cache_hits + stats.cache_misses;
                identity = identity && stats.cache_misses == stats.eval_computed;
                check(identity, "evaluation aggregation identities hold");
            }
        }
    }

    void run_failure_predicate_tests()
    {
        std::array<std::uint16_t, 48> rows = {};
        rows[20] = 0x3ff;
        rows[21] = 0x3ff;
        tetris::Board blocked = tetris::Board::from_rows(rows);
        tetris::Placement spawn = tetris::Placement::unchecked(
            toj_alias::spawn_x, toj_alias::spawn_y, 0);
        bool any_fit = false;
        for (char c : support::bag_order)
        {
            auto piece = tetris::try_from_char(c);
            if (piece.has_value() && toj_alias::fits(*piece, spawn, blocked))
            {
                any_fit = true;
            }
        }
        check(!any_fit, "a sealed spawn fits no piece and dies before search");
        tetris::Board empty;
        check(empty.empty(), "a default board is empty");
        auto empty_piece = tetris::try_from_char('T');
        check(empty_piece.has_value() && toj_alias::fits(*empty_piece, spawn, empty),
            "spawn fits on an empty board");
        tetris::Placement high = tetris::Placement::unchecked(
            toj_alias::spawn_x, 30, 0);
        check(toj_policy::Policy::is_lockout(tetris::Piece::T, high),
            "a placement above the lockout row reports lockout");
        tetris::Placement low = tetris::Placement::unchecked(
            toj_alias::spawn_x, 0, 0);
        check(!toj_policy::Policy::is_lockout(tetris::Piece::T, low),
            "a placement at the well bottom does not report lockout");
    }
}

int main()
{
    run_generator_tests();
    run_accounting_tests();
    run_root_seed_tests();
    run_schema_tests();
    run_lifecycle_tests();
    run_failure_predicate_tests();
    std::println("profile_value_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
