#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <span>
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

    static_assert(!std::is_copy_constructible_v<engine_alias::Engine>,
        "engine ownership is move-only");
    static_assert(!std::is_copy_assignable_v<engine_alias::Engine>,
        "engine ownership is move-only");
    static_assert(!std::is_move_assignable_v<engine_alias::Engine>,
        "engine ownership is construct-only");
    static_assert(std::is_move_constructible_v<engine_alias::Engine>,
        "a moved engine rebinds its heap to the destination arena");

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
        check(fixture.engine.init(fixture.engine_config), "fixture engine initializes");
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

    Fixture make_zero_fixture(std::size_t arena_capacity = engine_alias::default_arena_capacity)
    {
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 0;
        fixture.policy_config.parameters = toj_policy::Parameters{};
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.arena_capacity = arena_capacity;
        check(fixture.engine.init(fixture.engine_config), "zeroed fixture engine initializes");
        return fixture;
    }

    tetris::Board shelf_board()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 18; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        return tetris::Board::from_rows(rows);
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
        check(!second.empty(), "final held piece still expands");
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
        check(fixture.engine.arena_reserved_bytes() == 2 * sizeof(engine_alias::Node),
            "reservation matches the configured capacity exactly");
        auto const *first_node = fixture.engine.node(0);
        check(fixture.engine.materialize(children[0]) == 1, "second slot materializes");
        check(fixture.engine.node(0) == first_node, "no relocation during fill");
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

    void run_reservation_tests()
    {
        Fixture fixture = make_fixture(1000);
        check(fixture.engine.arena_reserved_bytes() == 1000 * sizeof(engine_alias::Node),
            "non-power-of-two capacity reserves exactly");
        tetris::Board empty;
        engine_alias::NodeId root = make_root(fixture, empty, "T", std::nullopt, false);
        check(root == 0, "reserved arena takes the root");
        auto children = fixture.engine.expand(root);
        auto const *stable = fixture.engine.node(root);
        std::size_t placed = 0;
        for (auto const &child : children)
        {
            if (fixture.engine.materialize(child) == engine_alias::no_node)
            {
                break;
            }
            ++placed;
        }
        check(placed > 0, "reservation fills with live nodes");
        check(fixture.engine.node(root) == stable, "addresses stay stable while filling");
        check(fixture.engine.arena_reserved_bytes() == 1000 * sizeof(engine_alias::Node),
            "no growth beyond the reservation");
        std::println("reservation: exact bytes, stable addresses, no growth peaks");
    }

    void run_init_validation_tests()
    {
        {
            Fixture fixture;
            fixture.policy_config.combo_table = combo_table;
            fixture.policy_config.combo_table_max = 10;
            fixture.policy_config.safe = 5;
            fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
            fixture.engine_config.policy = &fixture.policy_config;
            fixture.engine_config.arena_capacity =
                static_cast<std::size_t>(engine_alias::max_nodes) + 1;
            check(!fixture.engine.init(fixture.engine_config),
                "capacity beyond NodeId range is rejected");
            check(fixture.engine.arena_reserved_bytes() == 0,
                "rejected capacity allocates nothing");
        }
        {
            std::size_t over_bytes =
                static_cast<std::size_t>((engine_alias::engine_memory_budget
                    - engine_alias::engine_fixed_workspace)
                    / sizeof(engine_alias::Node))
                + 1;
            Fixture fixture;
            fixture.policy_config.combo_table = combo_table;
            fixture.policy_config.combo_table_max = 10;
            fixture.policy_config.safe = 5;
            fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
            fixture.engine_config.policy = &fixture.policy_config;
            fixture.engine_config.arena_capacity = over_bytes;
            check(over_bytes <= engine_alias::max_nodes, "byte probe stays in NodeId range");
            check(!fixture.engine.init(fixture.engine_config),
                "capacity beyond the byte allowance is rejected");
            check(fixture.engine.arena_reserved_bytes() == 0,
                "rejected byte allowance allocates nothing");
        }
        std::println("init validation: limits reject before allocation");
    }

    void run_marker_tests()
    {
        auto parsed = engine_alias::parse_queue("T??");
        check(parsed.has_value() && parsed->marker_count == 2,
            "repeated markers keep their raw count");
        parsed = engine_alias::parse_queue("TI?I");
        check(parsed.has_value() && parsed->marker_count == 1,
            "interior marker keeps its raw count");
        parsed = engine_alias::parse_queue("TISZ");
        check(parsed.has_value() && parsed->marker_count == 0,
            "marker-free queue counts zero");
        std::println("markers: raw counts survive the boundary collapse");
    }

    void run_horizon_tests()
    {
        Fixture fixture = make_zero_fixture();
        tetris::Board board = shelf_board();
        toj_policy::State state;
        auto make = [&](std::string_view text, std::optional<tetris::Piece> piece, bool locked) {
            auto queue = engine_alias::parse_queue(text);
            check(queue.has_value(), "horizon queue parses");
            engine_alias::HoldState hold;
            hold.piece = piece;
            hold.locked = locked;
            return fixture.engine.set_root(board, state, std::move(*queue), hold);
        };
        std::size_t const expected_capacity = fixture.engine.arena_reserved_bytes()
            / sizeof(engine_alias::Node);
        check(expected_capacity > 0, "horizon fixture has storage");
        check(make("TI", tetris::Piece::I, true) != engine_alias::no_node
            && fixture.engine.frontier_count() == 2,
            "occupied locked hold with one raw next keeps the horizon");
        check(make("TI?", tetris::Piece::I, true) != engine_alias::no_node
            && fixture.engine.frontier_count() == 3,
            "one marker extends the locked-hold horizon");
        check(make("T?", tetris::Piece::I, true) != engine_alias::no_node
            && fixture.engine.frontier_count() == 1,
            "a marker on the active piece keeps the horizon");
        check(make("T??", tetris::Piece::I, true) != engine_alias::no_node
            && fixture.engine.frontier_count() == 2,
            "two markers on the active piece extend the horizon");
        check(make("TI", tetris::Piece::I, false) != engine_alias::no_node
            && fixture.engine.frontier_count() == 3,
            "an unlocked hold extends the horizon");
        check(make("TI", std::nullopt, true) != engine_alias::no_node
            && fixture.engine.frontier_count() == 2,
            "an empty hold never extends the horizon");
        engine_alias::Queue bare;
        bare.pieces.push_back(tetris::Piece::T);
        check(fixture.engine.set_root(board, state, engine_alias::Queue{}, engine_alias::HoldState{})
            == engine_alias::no_node,
            "a directly built empty queue is rejected");
        engine_alias::Queue mismatched;
        mismatched.pieces.push_back(tetris::Piece::T);
        mismatched.marker_count = 0;
        check(fixture.engine.set_root(board, state, std::move(mismatched), engine_alias::HoldState{})
            == engine_alias::no_node,
            "boundary length mismatch is rejected");
        std::println("horizon: legacy raw-next predicate and queue validation");
    }

    void run_key_tests()
    {
        using engine_alias::TranspositionKey;
        using engine_alias::transposition_hash;
        TranspositionKey base;
        base.depth = 2;
        base.cursor = 1;
        base.boundary_count = 1;
        base.root_child = 7;
        base.boundary_bits[0] = 1;
        base.active_piece = 0;
        base.hold_piece = 3;
        base.hold_available = true;
        base.state.value = 1.5;
        base.state.acc_value = -2.0;
        base.state.like = 0.5;
        base.state.combo = 2;
        base.state.t2_value = 3;
        std::array<std::uint16_t, 48> rows = {};
        rows[0] = 0x100;
        base.occupancy = tetris::Board::from_rows(rows).occupancy();
        auto differs = [&](TranspositionKey const &key, char const *what) {
            check(!(key == base), what);
            check(transposition_hash(key) != transposition_hash(base), what);
        };
        auto variant = base;
        variant.depth = 3;
        differs(variant, "depth difference splits the key");
        variant = base;
        variant.cursor = 2;
        differs(variant, "cursor difference splits the key");
        variant = base;
        variant.boundary_count = 2;
        differs(variant, "boundary count difference splits the key");
        variant = base;
        variant.root_child = 8;
        differs(variant, "root identity difference splits the key");
        variant = base;
        variant.boundary_bits[0] = 0;
        differs(variant, "remaining marker difference splits the key");
        variant = base;
        variant.active_piece = 1;
        differs(variant, "active piece difference splits the key");
        variant = base;
        variant.hold_piece = 4;
        differs(variant, "hold piece difference splits the key");
        variant = base;
        variant.hold_available = false;
        differs(variant, "hold availability difference splits the key");
        variant = base;
        variant.state.value = 1.0;
        differs(variant, "state value difference splits the key");
        variant = base;
        variant.state.acc_value = -2.5;
        differs(variant, "accumulated value difference splits the key");
        variant = base;
        variant.state.like = 0.25;
        differs(variant, "like difference splits the key");
        variant = base;
        variant.state.combo = 3;
        differs(variant, "combo difference splits the key");
        variant = base;
        variant.state.t2_value = 4;
        differs(variant, "t2 value difference splits the key");
        variant = base;
        variant.state.t3_value = 4;
        differs(variant, "t3 value difference splits the key");
        variant = base;
        variant.state.death = 1;
        differs(variant, "death difference splits the key");
        variant = base;
        variant.state.under_attack = 1;
        differs(variant, "under-attack difference splits the key");
        variant = base;
        variant.state.map_rise = 1;
        differs(variant, "map-rise difference splits the key");
        variant = base;
        variant.state.b2b = 1;
        differs(variant, "b2b difference splits the key");
        variant = base;
        std::array<std::uint16_t, 48> other_rows = {};
        other_rows[0] = 0x080;
        variant.occupancy = tetris::Board::from_rows(other_rows).occupancy();
        differs(variant, "occupancy difference splits the key");
        TranspositionKey zero_positive = base;
        TranspositionKey zero_negative = base;
        zero_positive.state.value = 0.0;
        zero_negative.state.value = -0.0;
        check(zero_positive == zero_negative
            && transposition_hash(zero_positive) == transposition_hash(zero_negative),
            "signed zero agrees between equality and hashing");
        std::println("transposition key: every field participates exactly");
    }

    void run_widening_tests()
    {
        Fixture fixture = make_zero_fixture();
        tetris::Board board = shelf_board();
        engine_alias::NodeId root = make_root(fixture, board, "III", std::nullopt, true);
        check(root == 0, "widening root takes the first slot");
        check(fixture.engine.frontier_count() == 3, "three-piece queue has three frontiers");
        check(!fixture.engine.run(0), "zero budget performs no passes");
        check(fixture.engine.search_stats().widening_passes == 0,
            "zero budget counts no work");
        check(fixture.engine.arena_size() == 1, "zero budget materializes nothing");
        check(!fixture.engine.select_best().has_value(),
            "zero budget has no selection");
        check(!fixture.engine.run(1), "first pass leaves deferred work");
        check(fixture.engine.search_stats().widening_passes == 1, "one pass counted");
        check(fixture.engine.search_stats().expanded_parents == 1 + 4 + 4,
            "seeding plus width-two quotas on both active frontiers");
        check(fixture.engine.search_stats().pending_occupancy > 0,
            "deferred work survives the first pass");
        check(!fixture.engine.run(1), "second pass still has work");
        check(fixture.engine.search_stats().widening_passes == 2, "second pass counted");
        check(fixture.engine.search_stats().expanded_parents == 1 + 6 + 6,
            "width-three quotas add two promotions per active frontier");
        check(fixture.engine.run(500), "the search completes within the budget");
        check(fixture.engine.search_complete(), "completion flag is set");
        std::size_t const passes = fixture.engine.search_stats().widening_passes;
        check(fixture.engine.run(500), "completed search reports complete");
        check(fixture.engine.search_stats().widening_passes == passes,
            "completed search counts no nonexistent work");
        {
            Fixture stalled = make_zero_fixture();
            stalled.policy_config.parameters.ratio = -20.0;
            engine_alias::NodeId stalled_root =
                make_root(stalled, board, "III", std::nullopt, true);
            check(stalled_root != engine_alias::no_node, "stalled-quota root takes");
            check(!stalled.engine.run(20), "stalled quotas keep deferred work");
            check(stalled.engine.search_stats().promotions_refused > 0,
                "equal scores are refused at quota instead of promoted");
        }
        Fixture single = make_zero_fixture();
        engine_alias::NodeId single_root = make_root(single, board, "T", std::nullopt, true);
        check(single_root != engine_alias::no_node && single.engine.frontier_count() == 1,
            "single-piece queue has one frontier");
        check(single.engine.run(1), "one pass finishes the zero-horizon search");
        std::println("widening: outer passes, quotas, deferral, completion");
    }

    void run_transposition_tests()
    {
        Fixture fixture = make_zero_fixture();
        tetris::Board board = shelf_board();
        engine_alias::NodeId root = make_root(fixture, board, "III", std::nullopt, true);
        check(root != engine_alias::no_node, "transposition root takes");
        check(fixture.engine.run(500), "transposition search completes");
        check(fixture.engine.search_stats().transposition_merges > 0,
            "same-root equivalents materialize once");
        check(!fixture.engine.search_stats().transposition_exhausted,
            "the transposition table holds the full search");
        auto const &engine = fixture.engine;
        bool shared_board_two_roots = false;
        bool attribution_consistent = true;
        std::size_t const size = engine.arena_size();
        for (std::size_t id = 1; id < size; ++id)
        {
            auto const *node = engine.node(static_cast<engine_alias::NodeId>(id));
            auto const *parent = engine.node(node->parent);
            engine_alias::NodeId const expected =
                node->depth == 1 ? static_cast<engine_alias::NodeId>(id) : parent->root_child;
            if (node->root_child != expected)
            {
                attribution_consistent = false;
            }
        }
        check(attribution_consistent,
            "every node attributes to its depth-one ancestor");
        for (std::size_t a = 1; a < size && !shared_board_two_roots; ++a)
        {
            auto const *na = engine.node(static_cast<engine_alias::NodeId>(a));
            if (na->depth != 2)
            {
                continue;
            }
            for (std::size_t b = a + 1; b < size; ++b)
            {
                auto const *nb = engine.node(static_cast<engine_alias::NodeId>(b));
                if (nb->depth == 2 && nb->board.occupancy() == na->board.occupancy()
                    && nb->root_child != na->root_child)
                {
                    shared_board_two_roots = true;
                    break;
                }
            }
        }
        check(shared_board_two_roots,
            "cross-root equivalents deliberately stay separate");
        std::println("transposition: same-root once, cross-root separate, attribution stable");
    }

    bool policy_equal(toj_policy::State const &a, toj_policy::State const &b)
    {
        return a.death == b.death && a.combo == b.combo && a.under_attack == b.under_attack
            && a.map_rise == b.map_rise && a.b2b == b.b2b && a.t2_value == b.t2_value
            && a.t3_value == b.t3_value && a.acc_value == b.acc_value && a.like == b.like
            && a.value == b.value;
    }

    void run_projection_tests()
    {
        Fixture fixture = make_fixture();
        tetris::Board board = shelf_board();
        engine_alias::NodeId root = make_root(fixture, board, "III", std::nullopt, true);
        check(root != engine_alias::no_node, "projection root takes");
        check(fixture.engine.run(500), "projection search completes");
        auto selection = fixture.engine.select_best();
        check(selection.has_value(), "completed search selects a root move");
        if (!selection.has_value())
        {
            return;
        }
        auto const *root_child = fixture.engine.node(selection->root_child);
        auto const *evidence = fixture.engine.node(selection->evidence);
        check(root_child->parent == 0, "selection attributes to a root child");
        check(evidence->depth == 3, "evidence comes from the deepest frontier");
        check(evidence->depth > root_child->depth, "evidence is deeper than the root move");
        check(!policy_equal(evidence->policy, root_child->policy),
            "immediate state and deeper evidence stay distinct");
        engine_alias::NodeId expected_evidence = engine_alias::no_node;
        double expected_value = 0;
        bool have_expected = false;
        for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
        {
            auto const *node = fixture.engine.node(static_cast<engine_alias::NodeId>(id));
            if (node->depth != 3)
            {
                continue;
            }
            if (!have_expected || node->policy.value > expected_value
                || (node->policy.value == expected_value
                    && static_cast<std::size_t>(expected_evidence) > id))
            {
                expected_evidence = static_cast<engine_alias::NodeId>(id);
                expected_value = node->policy.value;
                have_expected = true;
            }
        }
        check(have_expected && selection->evidence == expected_evidence,
            "evidence matches the deepest-frontier maximum");
        engine_alias::NodeId walked = expected_evidence;
        while (fixture.engine.node(walked)->parent != 0)
        {
            walked = fixture.engine.node(walked)->parent;
        }
        check(selection->root_child == walked,
            "the root move is the evidence ancestor at depth one");
        std::println("projection: deepest evidence, immediate root state, hand-checked walk");
    }

    void run_search_exhaustion_tests()
    {
        {
            Fixture fixture = make_zero_fixture(40);
            tetris::Board board = shelf_board();
            engine_alias::NodeId root = make_root(fixture, board, "III", std::nullopt, true);
            check(root != engine_alias::no_node, "exhaustion root takes");
            fixture.engine.run(500);
            check(fixture.engine.arena_exhausted(), "a tiny arena stops the search");
            check(!fixture.engine.search_complete(), "an exhausted run is incomplete");
            check(fixture.engine.arena_size() <= 40, "the arena never exceeds capacity");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "best-so-far selection survives exhaustion");
        }
        {
            Fixture fixture = make_fixture();
            tetris::Board board;
            engine_alias::NodeId root = make_root(fixture, board, "TTTTT", std::nullopt, true);
            check(root != engine_alias::no_node, "table-exhaustion root takes");
            fixture.engine.run(2000);
            check(fixture.engine.search_stats().transposition_exhausted,
                "a full transposition table stops the search");
            check(!fixture.engine.search_complete(),
                "table exhaustion never pretends completeness");
            check(fixture.engine.select_best().has_value(),
                "best-so-far selection survives table exhaustion");
        }
        std::println("exhaustion: arena and table limits stop safely");
    }

    void run_search_determinism_tests()
    {
        Fixture first = make_zero_fixture();
        Fixture second = make_zero_fixture();
        tetris::Board board = shelf_board();
        make_root(first, board, "III", std::nullopt, true);
        make_root(second, board, "III", std::nullopt, true);
        check(first.engine.run(300), "first deterministic run completes");
        check(second.engine.run(300), "second deterministic run completes");
        auto const &a = first.engine;
        auto const &b = second.engine;
        check(a.arena_size() == b.arena_size(), "deterministic runs materialize equally");
        check(a.search_stats().materialized_nodes == b.search_stats().materialized_nodes
            && a.search_stats().transposition_merges
                == b.search_stats().transposition_merges
            && a.search_stats().widening_passes == b.search_stats().widening_passes,
            "deterministic runs count equally");
        auto sa = a.select_best();
        auto sb = b.select_best();
        check(sa.has_value() && sb.has_value() && sa->root_child == sb->root_child
            && sa->evidence == sb->evidence,
            "deterministic runs select identically");
        bool identical = true;
        for (std::size_t id = 0; id < a.arena_size(); ++id)
        {
            auto const *na = a.node(static_cast<engine_alias::NodeId>(id));
            auto const *nb = b.node(static_cast<engine_alias::NodeId>(id));
            if (na->board.occupancy() != nb->board.occupancy()
                || na->policy.value != nb->policy.value
                || na->cursor != nb->cursor || na->depth != nb->depth
                || na->root_child != nb->root_child)
            {
                identical = false;
            }
        }
        check(identical, "every materialized node matches bit for bit");
        std::println("search determinism: repeated runs match exactly");
    }


    void run_move_tests()
    {
        Fixture fixture = make_zero_fixture();
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        engine_alias::Engine moved(std::move(fixture.engine));
        check(moved.arena_size() == 1, "a moved engine keeps its arena");
        check(moved.run(500), "a moved engine runs a full search");
        check(moved.search_complete(), "a moved engine reaches completion");
        auto selection = moved.select_best();
        check(selection.has_value(), "a moved engine projects a selection");
        if (selection.has_value())
        {
            check(moved.node(selection->root_child)->parent == 0,
                "a moved engine attributes through its own arena");
        }
        {
            Fixture source = make_zero_fixture();
            make_root(source, shelf_board(), "III", std::nullopt, true);
            source.engine.run(3);
            check(!source.engine.search_complete() || source.engine.arena_size() > 1,
                "mid-search source has live state");
            check(source.engine.arena_size() > 1, "mid-search move has live state");
            engine_alias::Engine relocated(std::move(source.engine));
            check(source.engine.init(source.engine_config),
                "the source reinitializes after the move");
            check(relocated.run(500), "the mid-search moved engine completes");
            Fixture baseline = make_zero_fixture();
            make_root(baseline, shelf_board(), "III", std::nullopt, true);
            check(baseline.engine.run(500), "the unmoved baseline completes");
            auto moved_selection = relocated.select_best();
            auto base_selection = baseline.engine.select_best();
            check(moved_selection.has_value() && base_selection.has_value()
                && moved_selection->root_child == base_selection->root_child
                && moved_selection->evidence == base_selection->evidence,
                "a mid-search move preserves the selection");
            check(relocated.arena_size() == baseline.engine.arena_size(),
                "a mid-search move preserves the arena size");
            check(relocated.search_stats().widening_passes
                == baseline.engine.search_stats().widening_passes,
                "a mid-search move preserves the widening pass count");
        }
        std::println("engine move: heap rebinds to the destination arena");
    }

    void run_pending_heap_tests()
    {
        std::vector<engine_alias::Node> arena;
        engine_alias::PendingHeap heap(arena);
        arena.resize(600);
        for (std::size_t i = 0; i < arena.size(); ++i)
        {
            arena[i].policy.value = static_cast<double>(i % 7);
        }
        heap.reset(3);
        for (std::size_t i = 0; i < 500; ++i)
        {
            heap.push(static_cast<engine_alias::NodeId>(i), 1);
        }
        check(heap.size(1) == 500, "long sibling chain tracks its size");
        bool ordered = true;
        double last_value = 0;
        engine_alias::NodeId last_id = 0;
        bool first = true;
        for (std::size_t n = 500; n > 0; --n)
        {
            engine_alias::NodeId id = heap.pop_max(1);
            double const value = arena[id].policy.value;
            if (first)
            {
                first = false;
            }
            else if (value > last_value
                || (value == last_value && id < last_id))
            {
                ordered = false;
            }
            last_value = value;
            last_id = id;
        }
        check(ordered, "repeated extract-max orders value then node id");
        check(heap.size(1) == 0, "drained heap reports zero");
        for (auto &node : arena)
        {
            node.policy.value = 0.0;
        }
        heap.reset(2);
        for (std::size_t i = 0; i < 300; ++i)
        {
            heap.push(static_cast<engine_alias::NodeId>(i), 0);
        }
        bool id_order = true;
        for (engine_alias::NodeId expected = 0; expected < 300; ++expected)
        {
            if (heap.pop_max(0) != expected)
            {
                id_order = false;
            }
        }
        check(id_order, "equal scores extract in ascending node id order");
        heap.push(5, 0);
        heap.push(9, 1);
        check(heap.size(0) == 1 && heap.size(1) == 1,
            "frontier heaps stay independent");
        std::println("pending heap: equal scores, long chains, repeated extraction");
    }

    void run_memory_accounting_tests()
    {
        Fixture fixture = make_fixture();
        check(fixture.engine.retained_bytes() <= engine_alias::engine_memory_budget,
            "retained bytes stay within the budget");
        engine_alias::Queue fat;
        fat.pieces.reserve(1u << 20);
        fat.boundary.reserve(1u << 20);
        fat.pieces.push_back(tetris::Piece::T);
        fat.boundary.push_back(false);
        toj_policy::State state;
        engine_alias::HoldState hold;
        check(fixture.engine.set_root(tetris::Board{}, state, std::move(fat), hold)
            != engine_alias::no_node,
            "an oversized-reservation queue is accepted");
        check(fixture.engine.queue().pieces.capacity() <= engine_alias::max_queue_length,
            "adopted piece storage is bounded");
        check(fixture.engine.queue().boundary.capacity() <= engine_alias::max_queue_length,
            "adopted boundary storage is bounded");
        check(fixture.engine.retained_bytes() <= engine_alias::engine_memory_budget,
            "retained bytes stay bounded after adoption");
        std::println("memory accounting: complete inventory, bounded adoption");
    }

    void run_marker_boundary_tests()
    {
        Fixture fixture = make_zero_fixture();
        auto direct = [&](std::size_t markers) {
            engine_alias::Queue queue;
            queue.pieces = { tetris::Piece::T, tetris::Piece::I };
            queue.boundary = { false, false };
            queue.marker_count = markers;
            toj_policy::State state;
            engine_alias::HoldState hold;
            hold.piece = tetris::Piece::I;
            hold.locked = true;
            return fixture.engine.set_root(tetris::Board{}, state, std::move(queue), hold);
        };
        std::size_t const huge = std::numeric_limits<std::size_t>::max();
        check(direct(0) != engine_alias::no_node && fixture.engine.frontier_count() == 2,
            "zero markers keep the locked-hold horizon");
        check(direct(1) != engine_alias::no_node && fixture.engine.frontier_count() == 3,
            "one marker extends the horizon");
        check(direct(2) != engine_alias::no_node && fixture.engine.frontier_count() == 3,
            "two markers extend the horizon");
        check(direct(huge) != engine_alias::no_node
            && fixture.engine.frontier_count() == 3,
            "saturating marker count extends without overflow");
        check(direct(huge - 1) != engine_alias::no_node
            && fixture.engine.frontier_count() == 3,
            "near-saturating marker count extends without overflow");
        std::println("marker boundary: overflow-free raw-next predicate");
    }

    void run_reinit_tests()
    {
        Fixture fixture = make_zero_fixture();
        make_root(fixture, tetris::Board{}, "TTTTT", std::nullopt, true);
        fixture.engine.run(2000);
        check(fixture.engine.arena_exhausted()
            || fixture.engine.search_stats().transposition_exhausted,
            "exhaustion precedes reinitialization");
        check(fixture.engine.last_stats().enumerated > 0,
            "expansion counters precede reinitialization");
        check(fixture.engine.init(fixture.engine_config), "successful reinitialization");
        check(!fixture.engine.arena_exhausted(), "reinitialization clears exhaustion");
        check(fixture.engine.last_stats().enumerated == 0,
            "reinitialization clears expansion counters");
        check(!fixture.engine.search_complete(), "reinitialization clears completion");
        check(!fixture.engine.select_best().has_value(),
            "reinitialization clears stale selection");
        check(fixture.engine.arena_size() == 0, "reinitialization clears the arena");
        std::size_t const good_capacity = fixture.engine_config.arena_capacity;
        fixture.engine_config.arena_capacity = engine_alias::max_nodes + 1;
        check(!fixture.engine.init(fixture.engine_config), "rejected reinitialization");
        check(!fixture.engine.arena_exhausted(), "rejected reinit clears exhaustion");
        check(!fixture.engine.search_complete(), "rejected reinit clears completion");
        check(!fixture.engine.select_best().has_value(),
            "rejected reinit clears stale selection");
        fixture.engine_config.arena_capacity = good_capacity;
        check(fixture.engine.init(fixture.engine_config), "restored reinitialization");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(fixture.engine.run(500), "search runs after reinitialization");
        std::println("reinitialization: run state resets on success and rejection");
    }

    void run_exhaustion_projection_tests()
    {
        {
            Fixture fixture = make_zero_fixture(25);
            tetris::Board board = shelf_board();
            engine_alias::NodeId root = make_root(fixture, board, "III", std::nullopt, true);
            check(root != engine_alias::no_node, "exhaustion projection root takes");
            fixture.engine.run(500);
            check(fixture.engine.arena_exhausted(), "the small arena exhausts");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "the promoted parent stays selectable");
            if (!selection.has_value())
            {
                return;
            }
            auto const *evidence = fixture.engine.node(selection->evidence);
            bool dominated = false;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node =
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth != evidence->depth)
                {
                    continue;
                }
                if (node->policy.value > evidence->policy.value
                    || (node->policy.value == evidence->policy.value
                        && id < static_cast<std::size_t>(selection->evidence)))
                {
                    dominated = true;
                }
            }
            check(!dominated,
                "no same-depth node beats the selected evidence under the tie-break");
            check(fixture.engine.node(selection->root_child)->parent == 0,
                "exhausted selection attributes to a root child");
        }
        {
            Fixture fixture = make_fixture(18);
            engine_alias::NodeId root = make_root(fixture, tetris::Board{}, "III",
                std::nullopt, true);
            check(root != engine_alias::no_node, "production exhaustion root takes");
            fixture.engine.run(500);
            check(fixture.engine.arena_exhausted(), "the production probe exhausts");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "production exhaustion keeps a selection");
            if (!selection.has_value())
            {
                return;
            }
            auto const *evidence = fixture.engine.node(selection->evidence);
            bool dominated = false;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node =
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth != evidence->depth)
                {
                    continue;
                }
                if (node->policy.value > evidence->policy.value
                    || (node->policy.value == evidence->policy.value
                        && id < static_cast<std::size_t>(selection->evidence)))
                {
                    dominated = true;
                }
            }
            check(!dominated,
                "production exhaustion preserves the best equal-score evidence");
            check(fixture.engine.node(selection->root_child)->parent == 0,
                "production exhaustion attributes to a root child");
        }
        std::println("exhaustion projection: best evidence and attribution preserved");
    }

    void run_adapter_order_tests()
    {
        std::vector<tetris::Board> boards;
        boards.push_back(tetris::Board{});
        boards.push_back(shelf_board());
        std::array<std::uint16_t, 48> rows = {};
        rows[0] = 0x0ff;
        rows[1] = 0x1ff;
        rows[2] = 0x0f8;
        rows[3] = 0x3c0;
        boards.push_back(tetris::Board::from_rows(rows));
        toj_alias::MovementConfig config;
        bool keys_strict = true;
        bool canonical = true;
        std::size_t total = 0;
        for (auto const &board : boards)
        {
            for (tetris::Piece piece : { tetris::Piece::T, tetris::Piece::Z, tetris::Piece::S,
                     tetris::Piece::J, tetris::Piece::L, tetris::Piece::O, tetris::Piece::I })
            {
                std::vector<tetris::Candidate> buffer(toj_alias::max_candidates_per_source());
                auto batch = toj_alias::enumerate_candidates_into(board, piece, config,
                    std::span<tetris::Candidate>(buffer));
                check(batch.has_value(), "candidate batch stays within the domain bound");
                if (!batch.has_value())
                {
                    continue;
                }
                total += batch->count;
                for (std::size_t i = 1; i < batch->count; ++i)
                {
                    auto const &prev = buffer[i - 1];
                    auto const &cur = buffer[i];
                    auto const prev_cells = *toj_alias::cells(piece, prev.placement);
                    auto const cur_cells = *toj_alias::cells(piece, cur.placement);
                    int const prev_arrival =
                        piece == tetris::Piece::T ? static_cast<int>(prev.arrival) : 0;
                    int const cur_arrival =
                        piece == tetris::Piece::T ? static_cast<int>(cur.arrival) : 0;
                    bool const increasing = prev_cells < cur_cells
                        || (prev_cells == cur_cells && prev_arrival < cur_arrival);
                    if (!increasing)
                    {
                        keys_strict = false;
                    }
                }
                if (piece == tetris::Piece::S || piece == tetris::Piece::Z
                    || piece == tetris::Piece::I)
                {
                    for (std::size_t a = 0; a < batch->count; ++a)
                    {
                        for (std::size_t b = a + 1; b < batch->count; ++b)
                        {
                            auto const &ca = buffer[a];
                            auto const &cb = buffer[b];
                            if (*toj_alias::cells(piece, ca.placement)
                                == *toj_alias::cells(piece, cb.placement)
                                && ca.placement.rotation() != cb.placement.rotation())
                            {
                                canonical = false;
                            }
                        }
                    }
                }
            }
        }
        check(keys_strict, "distinct candidates never share a canonical key");
        check(canonical, "each occupied-cell set keeps exactly one rotation");
        check(total > 0, "adapter order probe enumerates candidates");
        std::println("adapter order: canonical keys strictly increasing, {} candidates", total);
    }

    void run_time_budget_tests()
    {
        std::int64_t calls = 0;
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 0;
        fixture.policy_config.parameters = toj_policy::Parameters{};
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.clock_nanos = [&calls]() {
            return calls++ * 4'000'000;
        };
        check(fixture.engine.init(fixture.engine_config), "timed fixture initializes");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(!fixture.engine.run(engine_alias::SearchBudget::by_time(10)),
            "a ten millisecond budget leaves deferred work");
        check(fixture.engine.search_stats().widening_passes == 3,
            "the fake clock yields exactly three passes at four milliseconds each");
        check(fixture.engine.search_stats().pending_occupancy > 0,
            "deferred work survives the timed stop");
        auto timed_selection = fixture.engine.select_best();
        check(timed_selection.has_value(), "a timed stop still projects");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(!fixture.engine.run(engine_alias::SearchBudget::by_time(0)),
            "an expired budget still runs the legacy single pass");
        check(fixture.engine.search_stats().widening_passes == 1,
            "an expired budget counts exactly one pass");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(!fixture.engine.run(engine_alias::SearchBudget::by_iterations(3)),
            "three iterations leave deferred work");
        check(fixture.engine.search_stats().widening_passes == 3,
            "the iteration budget runs exactly three passes");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(!fixture.engine.run(engine_alias::SearchBudget::by_iterations(0)),
            "a zero iteration budget still runs the legacy single pass");
        check(fixture.engine.search_stats().widening_passes == 1,
            "a zero iteration budget counts exactly one pass");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        check(fixture.engine.run(engine_alias::SearchBudget::by_iterations(500)),
            "an ample iteration budget completes the search");
        std::size_t const complete_passes = fixture.engine.search_stats().widening_passes;
        check(fixture.engine.run(engine_alias::SearchBudget::by_iterations(500)),
            "a completed search reports complete under a budget");
        check(fixture.engine.search_stats().widening_passes == complete_passes,
            "a budget counts no work after completion");
        {
            Fixture frozen = make_zero_fixture();
            frozen.engine_config.clock_nanos = []() { return 0; };
            check(frozen.engine.init(frozen.engine_config),
                "frozen-clock fixture initializes");
            make_root(frozen, shelf_board(), "III", std::nullopt, true);
            check(frozen.engine.run(engine_alias::SearchBudget::by_time(1000)),
                "a frozen clock behaves as an unbounded budget");
            check(frozen.engine.search_complete(), "the frozen-clock search completes");
            Fixture baseline = make_zero_fixture();
            make_root(baseline, shelf_board(), "III", std::nullopt, true);
            check(baseline.engine.run(500), "the deterministic baseline completes");
            auto frozen_selection = frozen.engine.select_best();
            auto base_selection = baseline.engine.select_best();
            check(frozen_selection.has_value() && base_selection.has_value()
                && frozen_selection->root_child == base_selection->root_child
                && frozen_selection->evidence == base_selection->evidence
                && frozen.engine.arena_size() == baseline.engine.arena_size()
                && frozen.engine.search_stats().widening_passes
                    == baseline.engine.search_stats().widening_passes,
                "a frozen clock matches the deterministic budget exactly");
        }
        {
            Fixture fixture_two;
            fixture_two.policy_config.combo_table = combo_table;
            fixture_two.policy_config.combo_table_max = 10;
            fixture_two.policy_config.safe = 0;
            fixture_two.policy_config.parameters = toj_policy::Parameters{};
            fixture_two.engine_config.policy = &fixture_two.policy_config;
            check(fixture_two.engine.init(fixture_two.engine_config),
                "real-clock fixture initializes");
            make_root(fixture_two, shelf_board(), "III", std::nullopt, true);
            bool const returned = fixture_two.engine.run(
                engine_alias::SearchBudget::by_time(50));
            check(fixture_two.engine.search_stats().widening_passes >= 1,
                "a real-clock budget runs at least one pass and returns");
            (void)returned;
        }
        {
            Fixture tiny = make_zero_fixture(25);
            make_root(tiny, tetris::Board{}, "III", std::nullopt, true);
            tiny.engine.run(engine_alias::SearchBudget::by_time(60000));
            check(tiny.engine.arena_exhausted(),
                "a large time budget still respects arena exhaustion");
            check(tiny.engine.select_best().has_value(),
                "a timed exhaustion keeps best-so-far selection");
        }
        {
            Fixture fixture_three = make_zero_fixture();
            make_root(fixture_three, shelf_board(), "III", std::nullopt, true);
            check(fixture_three.engine.run(engine_alias::SearchBudget::by_time(
                      std::numeric_limits<std::uint64_t>::max())),
                "a saturating time budget completes without overflow");
        }
        std::println("time budget: controllable clock, legacy do-while semantics");
    }

    void run_budget_tests()
    {
        std::size_t capacity = engine_alias::default_arena_capacity;
        std::size_t expect =
            static_cast<std::size_t>((engine_alias::engine_memory_budget
                - engine_alias::engine_fixed_workspace)
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
    run_reservation_tests();
    run_init_validation_tests();
    run_determinism_tests();
    run_terminal_tests();
    run_time_budget_tests();
    run_budget_tests();
    run_marker_tests();
    run_horizon_tests();
    run_key_tests();
    run_widening_tests();
    run_transposition_tests();
    run_projection_tests();
    run_search_exhaustion_tests();
    run_search_determinism_tests();
    run_move_tests();
    run_pending_heap_tests();
    run_memory_accounting_tests();
    run_marker_boundary_tests();
    run_reinit_tests();
    run_exhaustion_projection_tests();
    run_adapter_order_tests();
    std::println("tetris_engine_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
