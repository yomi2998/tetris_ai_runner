#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

namespace engine_alias = tetris_engine;
namespace toj_alias = tetris::toj;

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

    Fixture make_fixture(std::size_t arena_capacity = engine_alias::default_arena_capacity)
    {
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 5;
        fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.arena_capacity = arena_capacity;
        fixture.engine.init(fixture.engine_config);
        return fixture;
    }

    engine_alias::NodeId make_root(Fixture &fixture, tetris::Board board, std::string_view queue_text,
        std::optional<tetris::Piece> hold_piece, bool hold_locked)
    {
        auto queue = engine_alias::parse_queue(queue_text);
        check(queue.has_value(), "root queue parses");
        if (!queue.has_value())
        {
            return engine_alias::no_node;
        }
        engine_alias::HoldState hold;
        hold.piece = hold_piece;
        hold.locked = hold_locked;
        toj_policy::State state;
        return fixture.engine.set_root(board, state, std::move(*queue), hold);
    }

    void run_queue_parsing_tests()
    {
        auto parsed = engine_alias::parse_queue("T");
        check(parsed.has_value() && parsed->pieces.size() == 1
            && parsed->pieces[0] == tetris::Piece::T && !parsed->boundary[0],
            "single piece queue parses without boundary");
        parsed = engine_alias::parse_queue("T?ISZ");
        check(parsed.has_value() && parsed->pieces.size() == 4 && parsed->boundary[0]
            && !parsed->boundary[1],
            "marker attaches to the preceding piece");
        parsed = engine_alias::parse_queue("IO??SZLT");
        check(parsed.has_value() && parsed->pieces.size() == 6 && parsed->boundary[1],
            "repeated markers attach to one piece");
        parsed = engine_alias::parse_queue("IOSZLT?");
        check(parsed.has_value() && parsed->pieces.size() == 6 && parsed->boundary[5],
            "trailing marker attaches to the last piece");
        check(!engine_alias::parse_queue("?IOSZLT").has_value(),
            "leading marker without a piece is rejected");
        check(!engine_alias::parse_queue("").has_value(), "empty queue is rejected");
        check(!engine_alias::parse_queue("TXZ").has_value(), "unknown piece is rejected");
        std::println("queue parsing: boundaries attach, bad input rejected");
    }

    void run_cursor_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "TZSJLOI", std::nullopt, false);
        check(root == 0, "root takes the first arena slot");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "empty board expansion yields children");
        bool current_advances = true;
        bool hold_consumes_two = true;
        for (auto const &child : children)
        {
            if (child.source == engine_alias::BranchSource::Current && child.cursor != 1)
            {
                current_advances = false;
            }
            if (child.source == engine_alias::BranchSource::Hold && child.cursor != 2)
            {
                hold_consumes_two = false;
            }
        }
        check(current_advances, "current branch consumes one queue position");
        check(hold_consumes_two, "empty-hold branch consumes two queue positions");
        bool hold_swaps_piece = true;
        bool hold_plays_next = true;
        for (auto const &child : children)
        {
            if (child.source != engine_alias::BranchSource::Hold)
            {
                continue;
            }
            if (!child.hold.piece.has_value() || *child.hold.piece != tetris::Piece::T)
            {
                hold_swaps_piece = false;
            }
            if (child.played != tetris::Piece::Z)
            {
                hold_plays_next = false;
            }
        }
        check(hold_swaps_piece, "empty-hold branch stores the current piece as hold");
        check(hold_plays_next, "empty-hold branch plays the next queue piece");
        std::println("queue consumption: cursors and hold swap match the legacy shape");
    }

    void run_hold_branch_tests()
    {
        {
            Fixture fixture = make_fixture();
            tetris::Board empty;
            engine_alias::NodeId root =
                make_root(fixture, empty, "TJ", tetris::Piece::L, false);
            auto children = fixture.engine.expand(root);
            bool saw_swap = false;
            bool swap_cursor = true;
            for (auto const &child : children)
            {
                if (child.source != engine_alias::BranchSource::Hold)
                {
                    continue;
                }
                saw_swap = true;
                if (child.played != tetris::Piece::L || child.cursor != 1)
                {
                    swap_cursor = false;
                }
                if (!child.hold.piece.has_value() || *child.hold.piece != tetris::Piece::T)
                {
                    swap_cursor = false;
                }
            }
            check(saw_swap, "stored hold piece is offered");
            check(swap_cursor, "hold swap plays hold with single advance");
        }
        {
            Fixture fixture = make_fixture();
            tetris::Board empty;
            engine_alias::NodeId root = make_root(fixture, empty, "TJ", tetris::Piece::L, true);
            auto children = fixture.engine.expand(root);
            bool hold_absent = true;
            for (auto const &child : children)
            {
                if (child.source == engine_alias::BranchSource::Hold)
                {
                    hold_absent = false;
                }
            }
            check(hold_absent, "locked hold offers the current piece only");
        }
        {
            Fixture fixture = make_fixture();
            tetris::Board empty;
            engine_alias::NodeId root = make_root(fixture, empty, "T", std::nullopt, false);
            auto children = fixture.engine.expand(root);
            bool hold_absent = true;
            bool current_present = false;
            for (auto const &child : children)
            {
                if (child.source == engine_alias::BranchSource::Hold)
                {
                    hold_absent = false;
                }
                else
                {
                    current_present = true;
                }
            }
            check(current_present, "last queue piece still expands");
            check(hold_absent, "empty hold without a next piece offers nothing");
        }
        {
            Fixture fixture = make_fixture();
            tetris::Board empty;
            engine_alias::NodeId root = make_root(fixture, empty, "TJ", std::nullopt, false);
            auto children = fixture.engine.expand(root);
            engine_alias::NodeId held = engine_alias::no_node;
            for (auto const &child : children)
            {
                if (child.source == engine_alias::BranchSource::Hold)
                {
                    held = fixture.engine.materialize(child);
                    break;
                }
            }
            check(held != engine_alias::no_node, "hold child materializes");
            auto const *node = fixture.engine.node(held);
            check(node != nullptr && !node->hold.locked,
                "hold availability resets after placement and lock");
            check(node != nullptr && node->depth == 1, "materialized child depth follows parent");
        }
        std::println("hold branches: swap, lock, exhaustion, and reset");
    }

    bool same_state(toj_policy::State const &a, toj_policy::State const &b)
    {
        return a.death == b.death && a.combo == b.combo && a.under_attack == b.under_attack
            && a.map_rise == b.map_rise && a.b2b == b.b2b && a.t2_value == b.t2_value
            && a.t3_value == b.t3_value && a.acc_value == b.acc_value && a.like == b.like
            && a.value == b.value;
    }

    void run_propagation_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        auto queue = engine_alias::parse_queue("TZI");
        check(queue.has_value(), "propagation queue parses");
        toj_policy::State parent;
        parent.combo = 2;
        parent.b2b = 1;
        parent.under_attack = 1;
        parent.t2_value = 3;
        parent.t3_value = 4;
        parent.acc_value = 100.5;
        parent.like = 50.25;
        parent.value = 10.125;
        engine_alias::HoldState hold;
        engine_alias::NodeId root =
            fixture.engine.set_root(empty, parent, std::move(*queue), hold);
        check(root == 0, "propagation root materializes");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "propagation fixture expands");
        auto stats = fixture.engine.last_stats();
        check(stats.transitions == children.size(), "every child runs exactly one transition");
        toj_policy::Policy direct;
        direct.init(&fixture.policy_config);
        auto expect_queue = engine_alias::parse_queue("TZI");
        bool ordered = true;
        bool saw_hold = false;
        std::size_t index = 0;
        for (auto const &child : children)
        {
            std::string what = "wired child " + std::to_string(index++);
            if (child.source == engine_alias::BranchSource::Hold)
            {
                saw_hold = true;
            }
            else if (saw_hold)
            {
                ordered = false;
            }
            auto applied = toj_alias::apply(empty, child.played, child.candidate);
            check(applied.has_value() && applied->board == child.board
                && applied->spin == child.outcome.spin
                && applied->clear_count == child.outcome.clear_count
                && applied->lockout == child.outcome.lockout,
                what + " outcome and board match the rule adapter");
            if (!applied.has_value())
            {
                continue;
            }
            toj_policy::Evaluation expect_eval = direct.evaluate(child.board);
            toj_policy::DecisionContext expect_context;
            std::vector<tetris::Piece> tail(
                expect_queue->pieces.begin() + 1, expect_queue->pieces.end());
            expect_context.next = tail;
            if (child.source == engine_alias::BranchSource::Current)
            {
                expect_context.hold = std::nullopt;
            }
            else
            {
                expect_context.hold = tetris::Piece::T;
            }
            expect_context.used_hold = child.source == engine_alias::BranchSource::Hold;
            expect_context.depth = 0;
            toj_policy::State expect_state = direct.transition(child.played, child.candidate,
                child.outcome, child.board, parent, expect_context, expect_eval);
            check(expect_eval.value == child.evaluation.value
                && expect_eval.t2_value == child.evaluation.t2_value
                && expect_eval.t3_value == child.evaluation.t3_value,
                what + " evaluation matches the direct call");
            check(same_state(expect_state, child.state),
                what + " state matches the direct call with explicit context");
            if (child.source == engine_alias::BranchSource::Current)
            {
                check(!child.hold.piece.has_value(), what + " current branch keeps empty hold");
            }
            else
            {
                check(child.hold.piece.has_value() && *child.hold.piece == tetris::Piece::T,
                    what + " hold branch stores the current piece");
            }
        }
        check(ordered, "current-branch children precede hold-branch children");
        std::println("propagation: full state wiring with explicit contexts");
    }

    void run_context_sequence_tests()
    {
        Fixture fixture = make_fixture();
        std::array<std::uint16_t, 48> rows = {};
        rows[0] = 0x251;
        rows[1] = 0x2a9;
        rows[2] = 0x3e1;
        rows[3] = 0x0f0;
        rows[4] = 0x29f;
        rows[5] = 0x3cb;
        rows[6] = 0x1ba;
        rows[7] = 0x0f1;
        tetris::Board board = tetris::Board::from_rows(rows);
        auto queue = engine_alias::parse_queue("STI");
        check(queue.has_value(), "sequence queue parses");
        toj_policy::State parent;
        engine_alias::HoldState hold;
        engine_alias::NodeId root =
            fixture.engine.set_root(board, parent, std::move(*queue), hold);
        check(root == 0, "sequence root materializes");
        auto children = fixture.engine.expand(root);
        toj_policy::Policy direct;
        direct.init(&fixture.policy_config);
        bool found = false;
        bool sensitive = false;
        for (auto const &child : children)
        {
            if (child.source != engine_alias::BranchSource::Hold
                || child.played != tetris::Piece::T || child.evaluation.t2_value == 0)
            {
                continue;
            }
            found = true;
            std::vector<tetris::Piece> right{ tetris::Piece::T, tetris::Piece::I };
            std::vector<tetris::Piece> wrong{ tetris::Piece::I };
            toj_policy::DecisionContext right_context;
            right_context.next = right;
            right_context.hold = tetris::Piece::S;
            right_context.used_hold = true;
            right_context.depth = 0;
            toj_policy::DecisionContext wrong_context = right_context;
            wrong_context.next = wrong;
            toj_policy::Evaluation evaluation = direct.evaluate(child.board);
            toj_policy::State right_state = direct.transition(child.played, child.candidate,
                child.outcome, child.board, parent, right_context, evaluation);
            toj_policy::State wrong_state = direct.transition(child.played, child.candidate,
                child.outcome, child.board, parent, wrong_context, evaluation);
            if (same_state(right_state, child.state)
                && right_state.like != wrong_state.like)
            {
                sensitive = true;
                break;
            }
        }
        check(found, "distinguishing T-gain hold branch exists");
        check(sensitive, "policy context keeps the played piece in sequence");
        std::println("context sequence: empty-hold keeps parent remainder");
    }

    void run_cursor_overflow_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "TJ", std::nullopt, false);
        auto children = fixture.engine.expand(root);
        engine_alias::NodeId held = engine_alias::no_node;
        for (auto const &child : children)
        {
            if (child.source == engine_alias::BranchSource::Hold)
            {
                check(child.cursor <= 2, "hold child cursor stays in range");
                held = fixture.engine.materialize(child);
                break;
            }
        }
        check(held != engine_alias::no_node, "hold child materializes");
        auto second = fixture.engine.expand(held);
        bool terminal = true;
        for (auto const &child : second)
        {
            check(child.cursor <= 2, "final play cursor stays in range");
            if (child.source != engine_alias::BranchSource::Hold
                || child.played != tetris::Piece::T || child.hold.piece.has_value())
            {
                terminal = false;
            }
            engine_alias::NodeId tip = fixture.engine.materialize(child);
            check(fixture.engine.expand(tip).empty(), "consumed queue terminates safely");
        }
        check(terminal, "final held piece plays with cleared hold");
        std::println("cursor overflow: consumed queue terminates safely");
    }

    void run_input_rejection_tests()
    {
        Fixture fixture = make_fixture();
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 20; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x3ff;
        }
        tetris::Board full_rows = tetris::Board::from_rows(rows);
        auto queue = engine_alias::parse_queue("T");
        toj_policy::State state;
        engine_alias::HoldState hold;
        check(fixture.engine.set_root(full_rows, state, std::move(*queue), hold)
            == engine_alias::no_node,
            "root with a full row is rejected");
        std::string long_text(300, 'T');
        auto long_queue = engine_alias::parse_queue(long_text);
        check(long_queue.has_value(), "long queue still parses");
        auto queue_two = engine_alias::parse_queue("T");
        check(fixture.engine.set_root(tetris::Board{}, state, std::move(*long_queue), hold)
                == engine_alias::no_node
            && fixture.engine.set_root(tetris::Board{}, state, std::move(*queue_two), hold) == 0,
            "oversize queue is rejected while normal roots work");
        Fixture narrow = make_fixture(0);
        auto queue_three = engine_alias::parse_queue("T");
        check(narrow.engine.set_root(tetris::Board{}, state, std::move(*queue_three), hold)
            == engine_alias::no_node,
            "zero capacity rejects the root");
        std::println("input rejection: full rows, oversize queue, zero capacity");
    }

    void run_stats_reset_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "T", std::nullopt, false);
        check(!fixture.engine.expand(root).empty(), "priming expansion works");
        check(fixture.engine.last_stats().enumerated > 0, "priming expansion records work");
        check(fixture.engine.expand(999).empty(), "invalid expansion stays empty");
        auto stats = fixture.engine.last_stats();
        check(stats.enumerated == 0 && stats.evaluated == 0 && stats.transitions == 0,
            "invalid expansion reports no work");
        std::println("stats reset: early returns clear the counters");
    }

    void run_dedup_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "T", std::nullopt, false);
        auto children = fixture.engine.expand(root);
        auto stats = fixture.engine.last_stats();
        auto listed = toj_alias::enumerate_candidates(empty, tetris::Piece::T, {});
        check(!listed.empty() && children.size() < listed.size(),
            "duplicate results collapse within one source");
        bool pair_collapsed = false;
        for (std::size_t a = 0; a < listed.size() && !pair_collapsed; ++a)
        {
            for (std::size_t b = 0; b < listed.size(); ++b)
            {
                if (a == b || listed[a].placement != listed[b].placement
                    || listed[a].arrival == listed[b].arrival)
                {
                    continue;
                }
                std::size_t hits = 0;
                bool kept_normal = false;
                for (auto const &child : children)
                {
                    auto cells = toj_alias::cells(tetris::Piece::T, child.candidate.placement);
                    auto want = toj_alias::cells(tetris::Piece::T, listed[a].placement);
                    if (cells.has_value() && want.has_value() && *cells == *want)
                    {
                        ++hits;
                        kept_normal = child.candidate.arrival == tetris::ArrivalClass::Normal;
                    }
                }
                pair_collapsed = hits == 1 && kept_normal;
                break;
            }
        }
        check(pair_collapsed, "same-placement pair keeps the normal arrival");
        check(stats.enumerated == listed.size(), "enumeration count covers every candidate");
        std::vector<tetris::Board> boards;
        for (auto const &child : children)
        {
            boards.push_back(child.board);
        }
        std::sort(boards.begin(), boards.end(), [](tetris::Board const &a, tetris::Board const &b) {
            return a.rows() < b.rows();
        });
        bool unique = true;
        for (std::size_t k = 1; k < boards.size(); ++k)
        {
            if (boards[k] == boards[k - 1])
            {
                unique = false;
            }
        }
        check(unique, "one board survives per equivalence class");
        check(stats.evaluated == boards.size(), "evaluation runs once per unique board");
        bool normal_kept = false;
        for (auto const &child : children)
        {
            auto again = toj_alias::apply(empty, child.played, child.candidate);
            if (again.has_value() && child.candidate.arrival == tetris::ArrivalClass::Normal)
            {
                normal_kept = true;
            }
        }
        check(normal_kept, "first representative keeps the normal arrival");
        std::println("dedup: first wins per occupancy, spin, clear, and lockout");
    }

    void run_arena_tests()
    {
        Fixture fixture = make_fixture(2);
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "T", std::nullopt, false);
        check(root == 0 && fixture.engine.arena_size() == 1, "tiny arena still takes the root");
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "expansion needs no arena space");
        check(fixture.engine.materialize(children[0]) == 1, "second slot materializes");
        check(fixture.engine.materialize(children[0]) == engine_alias::no_node,
            "full arena fails closed");
        check(fixture.engine.arena_exhausted(), "exhaustion is reported");
        check(fixture.engine.arena_size() == 2, "failed materialization keeps valid nodes");
        engine_alias::NodeId first = 1;
        fixture.engine.link_children(root, first, 1);
        auto const *node = fixture.engine.node(root);
        check(node != nullptr && node->first_child == first && node->child_count == 1,
            "parent links its child range");
        check(fixture.engine.node(99) == nullptr, "invalid ids read as null");
        std::println("arena: bounded storage with fail-closed materialization");
    }

    void run_determinism_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "TZSJLOI", tetris::Piece::O, false);
        auto first = fixture.engine.expand(root);
        auto second = fixture.engine.expand(root);
        check(first.size() == second.size(), "repeated expansion keeps count");
        bool same = first.size() == second.size();
        for (std::size_t k = 0; same && k < first.size(); ++k)
        {
            same = first[k].candidate == second[k].candidate && first[k].source == second[k].source
                && first[k].played == second[k].played && first[k].board == second[k].board
                && first[k].outcome == second[k].outcome && first[k].cursor == second[k].cursor
                && first[k].expandable == second[k].expandable
                && first[k].hold.piece == second[k].hold.piece
                && first[k].evaluation.value == second[k].evaluation.value
                && first[k].evaluation.t2_value == second[k].evaluation.t2_value
                && first[k].evaluation.t3_value == second[k].evaluation.t3_value
                && same_state(first[k].state, second[k].state);
        }
        check(same, "repeated expansion is bit-identical");
        check(fixture.engine.last_stats().enumerated > 0, "expansion does real work");
        std::println("determinism: repeated expansion matches exactly");
    }

    void run_terminal_tests()
    {
        Fixture fixture = make_fixture();
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 20; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        tetris::Board plateau = tetris::Board::from_rows(rows);
        engine_alias::NodeId root = make_root(fixture, plateau, "T", std::nullopt, false);
        auto children = fixture.engine.expand(root);
        check(!children.empty(), "plateau board still expands");
        bool saw_dead = false;
        bool rule_holds = true;
        for (auto const &child : children)
        {
            auto lowest =
                toj_alias::lowest_occupied_row(child.played, child.candidate.placement);
            if (!lowest.has_value() || child.expandable == (*lowest >= 20))
            {
                rule_holds = false;
            }
            if (!child.expandable)
            {
                saw_dead = true;
                auto replayed = toj_alias::apply(plateau, child.played, child.candidate);
                check(replayed.has_value() && replayed->lockout,
                    "unexpandable children carry lockout results");
            }
        }
        check(rule_holds, "expandable tracks the lowest mino row on every child");
        check(saw_dead, "lockout placements become dead results");
        for (auto const &child : children)
        {
            if (!child.expandable)
            {
                engine_alias::NodeId dead = fixture.engine.materialize(child);
                check(dead != engine_alias::no_node, "dead result materializes for ranking");
                check(fixture.engine.expand(dead).empty(), "dead nodes do not expand");
                break;
            }
        }
        std::array<std::uint16_t, 48> blocked = {};
        blocked[20] = 0x038;
        blocked[21] = 0x010;
        tetris::Board capped = tetris::Board::from_rows(blocked);
        engine_alias::NodeId stuck = make_root(fixture, capped, "T", std::nullopt, false);
        check(fixture.engine.expand(stuck).empty(), "spawn obstruction yields no children");
        std::println("terminal: lockout dead results and spawn death");
    }

    void run_budget_tests()
    {
        std::size_t capacity = engine_alias::default_arena_capacity;
        std::size_t expect =
            static_cast<std::size_t>((engine_alias::engine_memory_budget
                - engine_alias::engine_workspace_reserve)
                / sizeof(engine_alias::Node));
        check(capacity == expect && capacity > 1024, "arena capacity derives from the budget");
        check(capacity < engine_alias::max_nodes, "capacity stays in NodeId range");
        check(engine_alias::max_queue_length == 256, "queue bound is declared");
        std::println("node storage: {} bytes per node", sizeof(engine_alias::Node));
        std::println("budget: capacity derives from measured node cost");
    }
}

int main()
{
    run_queue_parsing_tests();
    run_cursor_tests();
    run_hold_branch_tests();
    run_propagation_tests();
    run_context_sequence_tests();
    run_cursor_overflow_tests();
    run_input_rejection_tests();
    run_stats_reset_tests();
    run_dedup_tests();
    run_arena_tests();
    run_determinism_tests();
    run_terminal_tests();
    run_budget_tests();
    std::println("tetris_engine_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
