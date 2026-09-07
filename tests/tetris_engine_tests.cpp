#include "tetris_engine.h"
#include "toj_rule.h"
#include "toj_policy.h"
#include "scalar_arrival_oracle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

    tetris::Board pocket_board()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 18; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        rows[27] = 0x1f8;
        for (int y = 28; y <= 31; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x108;
        }
        rows[32] = 0x1f8;
        return tetris::Board::from_rows(rows);
    }

    tetris::Board lip_board()
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 16; ++y)
        {
            rows[static_cast<std::size_t>(y)] = 0x1ff;
        }
        rows[16] = 0x1ff & ~(0x038);
        rows[17] = 0x1ff & ~(0x038);
        rows[18] = 0x1ff & ~(0x010);
        rows[19] = 0x1ff;
        return tetris::Board::from_rows(rows);
    }

    void check_child_links(engine_alias::Engine const &engine, char const *what)
    {
        std::size_t const size = engine.arena_size();
        std::vector<char> seen(size, 0);
        for (std::size_t p = 0; p < size; ++p)
        {
            auto const *parent =
                engine.node(static_cast<engine_alias::NodeId>(p));
            std::size_t count = 0;
            engine_alias::NodeId id = parent->first_child;
            while (id != engine_alias::no_node && count <= size)
            {
                if (id >= size)
                {
                    check(false, what);
                    break;
                }
                auto const *node = engine.node(id);
                check(node->parent == p, what);
                check(!seen[id], what);
                if (seen[id])
                {
                    break;
                }
                seen[id] = 1;
                id = node->next_sibling;
                ++count;
            }
            check(id == engine_alias::no_node, what);
            check(count == parent->child_count, what);
        }
        for (std::size_t n = 0; n < size; ++n)
        {
            auto const *node =
                engine.node(static_cast<engine_alias::NodeId>(n));
            if (node->parent == engine_alias::no_node)
            {
                continue;
            }
            check(node->parent < size && seen[n], what);
        }
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
            "parent links its child chain");
        auto linkage = [&]() {
            auto const *linked = fixture.engine.node(root);
            return std::pair(linked->first_child, linked->child_count);
        };
        auto const good_linkage = linkage();
        fixture.engine.link_children(root, 99, 1);
        check(linkage() == good_linkage,
            "an out-of-range first child preserves linkage");
        fixture.engine.link_children(root, first, 2);
        check(linkage() == good_linkage,
            "an oversized range preserves linkage");
        fixture.engine.link_children(root, first,
            std::numeric_limits<std::size_t>::max());
        check(linkage() == good_linkage,
            "a wrapping range preserves linkage");
        fixture.engine.link_children(root, first, 1);
        check(linkage() == good_linkage,
            "an exact end-of-arena range applies cleanly");
        check_child_links(fixture.engine,
            "rejected ranges keep valid chains");
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

    std::size_t fixtureless_cache_bytes(engine_alias::CacheConfig::Layout layout)
    {
        engine_alias::CacheConfig config;
        config.layout = layout;
        return layout == engine_alias::CacheConfig::Layout::Disabled
            ? std::size_t{ 0 }
            : config.entries * sizeof(engine_alias::EvalCacheEntry);
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
            std::size_t const cache_bytes =
                fixtureless_cache_bytes(engine_alias::CacheConfig::Layout::Disabled);
            std::size_t over_bytes = static_cast<std::size_t>(
                (engine_alias::engine_memory_budget
                    - engine_alias::engine_buffer_reservation(0, 0, 0, 0, 0, 0, cache_bytes))
                / (sizeof(engine_alias::Node) + sizeof(engine_alias::NodeId)))
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
            check(fixture.engine.idmap_reserved_bytes() == 0,
                "rejected byte allowance keeps no idmap scratch");
        }
        {
            std::size_t const cache_bytes = fixtureless_cache_bytes(
                engine_alias::CacheConfig::Layout::DirectMapped);
            std::size_t over_bytes = static_cast<std::size_t>(
                (engine_alias::engine_memory_budget
                    - engine_alias::engine_buffer_reservation(0, 0, 0, 0, 0, 0, cache_bytes))
                / (sizeof(engine_alias::Node) + sizeof(engine_alias::NodeId)))
                + 1;
            Fixture fixture;
            fixture.policy_config.combo_table = combo_table;
            fixture.policy_config.combo_table_max = 10;
            fixture.policy_config.safe = 5;
            fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
            fixture.engine_config.policy = &fixture.policy_config;
            fixture.engine_config.cache.layout = engine_alias::CacheConfig::Layout::DirectMapped;
            fixture.engine_config.arena_capacity = over_bytes;
            check(!fixture.engine.init(fixture.engine_config),
                "the cache reservation participates in the byte allowance");
            check(fixture.engine.arena_reserved_bytes() == 0,
                "rejected cache-configured capacity allocates nothing");
        }
        std::println("init validation: limits reject before allocation");
    }

    void run_cache_budget_rejection_tests()
    {
        auto base_config = [](Fixture &fixture) {
            fixture.policy_config.combo_table = combo_table;
            fixture.policy_config.combo_table_max = 10;
            fixture.policy_config.safe = 5;
            fixture.policy_config.parameters = toj_policy::Parameters::production_defaults();
            fixture.engine_config.policy = &fixture.policy_config;
            fixture.engine_config.cache.layout = engine_alias::CacheConfig::Layout::SetAssociative;
            fixture.engine_config.cache.ways = 4;
        };
        {
            Fixture fixture;
            base_config(fixture);
            fixture.engine_config.cache.entries = 1ull << 22;
            fixture.engine_config.arena_capacity = 0;
            check(!fixture.engine.init(fixture.engine_config),
                "an oversized cache is rejected even with zero arena capacity");
            check(fixture.engine.arena_reserved_bytes() == 0,
                "the oversized cache allocates no arena");
        }
        {
            Fixture fixture;
            base_config(fixture);
            fixture.engine_config.cache.entries =
                std::numeric_limits<std::uint64_t>::max()
                / sizeof(engine_alias::EvalCacheEntry);
            fixture.engine_config.cache.ways =
                fixture.engine_config.cache.entries;
            fixture.engine_config.arena_capacity = 1;
            check(!fixture.engine.init(fixture.engine_config),
                "a reservation sum that would wrap is rejected before allocation");
            check(fixture.engine.arena_reserved_bytes() == 0,
                "the wrapping reservation allocates no arena");
        }
        {
            Fixture fixture;
            base_config(fixture);
            fixture.engine_config.cache.entries = std::numeric_limits<std::uint64_t>::max();
            fixture.engine_config.arena_capacity = 0;
            check(!fixture.engine.init(fixture.engine_config),
                "an entry count whose byte product overflows is rejected");
        }
        {
            Fixture fixture;
            base_config(fixture);
            fixture.engine_config.cache.entries = 0;
            fixture.engine_config.arena_capacity = 0;
            check(!fixture.engine.init(fixture.engine_config),
                "zero cache entries are rejected for an enabled cache");
        }
        {
            Fixture fixture;
            base_config(fixture);
            fixture.engine_config.cache.ways = 0;
            fixture.engine_config.arena_capacity = 0;
            check(!fixture.engine.init(fixture.engine_config),
                "zero ways are rejected for an enabled cache");
        }
        std::println("cache budget rejection: boundary configurations fail closed");
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
        tetris::Board const occupancy_board = tetris::Board::from_rows(rows);
        base.occupancy = occupancy_board.occupancy();
        bool same_words = true;
        for (int i = 0; i < tetris::Board::occupancy_t::word_count(); ++i)
        {
            same_words = same_words
                && base.occupancy.logical_word(i) == occupancy_board.occupancy().logical_word(i);
        }
        check(sizeof(engine_alias::TranspositionKey) == 152
                && sizeof(engine_alias::TranspositionEntry) == 160,
            "transposition slots omit kernel alignment padding");
        check(same_words, "compact transposition occupancy preserves every logical word");
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
            engine_alias::NodeId const expected = node->depth == 1
                ? engine_alias::first_move_fingerprint(node->played, node->incoming,
                    node->source)
                : parent->root_child;
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
            auto const pre_move_stats = source.engine.search_stats();
            check(pre_move_stats.cache_requests > 0,
                "the mid-search source has cache activity");
            std::uint64_t const pre_move_retained = source.engine.retained_bytes();
            std::size_t const pre_move_idmap = source.engine.idmap_reserved_bytes();
            engine_alias::Engine relocated(std::move(source.engine));
            auto const post_move_stats = relocated.search_stats();
            check(post_move_stats.cache_requests == pre_move_stats.cache_requests
                && post_move_stats.cache_hits == pre_move_stats.cache_hits
                && post_move_stats.cache_misses == pre_move_stats.cache_misses,
                "a mid-search move transfers cache counters");
            check(relocated.retained_bytes() == pre_move_retained,
                "a mid-search move preserves retained storage");
            check(relocated.idmap_reserved_bytes() == pre_move_idmap,
                "a mid-search move transfers idmap scratch");
            check(source.engine.init(source.engine_config),
                "the source reinitializes after the move");
            check(relocated.run(500), "the mid-search moved engine completes");
            auto const relocated_stats = relocated.search_stats();
            check(relocated_stats.cache_requests > pre_move_stats.cache_requests,
                "cache activity continues after the move");
            check(relocated_stats.cache_requests
                == relocated_stats.cache_hits + relocated_stats.cache_misses,
                "a moved engine keeps its cache accounting consistent");
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

    void run_move_clock_tests()
    {
        struct ClockState
        {
            std::int64_t ticks = 0;
            bool armed = false;
        };
        struct Guard
        {
            std::shared_ptr<ClockState> state;

            explicit Guard(std::shared_ptr<ClockState> s)
                : state(std::move(s))
            {
            }
            Guard(Guard const &other)
                : state(other.state)
            {
                if (state && state->armed)
                {
                    throw std::runtime_error("clock copy");
                }
            }
            std::int64_t operator()() const
            {
                return state->ticks;
            }
        };
        auto state = std::make_shared<ClockState>();
        Fixture fixture;
        fixture.policy_config.combo_table = combo_table;
        fixture.policy_config.combo_table_max = 10;
        fixture.policy_config.safe = 0;
        fixture.policy_config.parameters = toj_policy::Parameters{};
        fixture.engine_config.policy = &fixture.policy_config;
        fixture.engine_config.clock_nanos = Guard{ state };
        check(fixture.engine.init(fixture.engine_config),
            "a throwing-clock fixture initializes while unarmed");
        make_root(fixture, shelf_board(), "III", std::nullopt, true);
        fixture.engine.run(3);
        state->armed = true;
        engine_alias::Engine moved(std::move(fixture.engine));
        check(moved.arena_size() > 1, "a moved clock engine keeps its arena");
        check(moved.run(engine_alias::SearchBudget::by_time(1000)),
            "a moved clock engine completes a timed search");
        check(moved.search_complete(), "the timed search reaches completion");
        check(moved.select_best().has_value(),
            "a moved clock engine projects a selection");
        std::println("engine move: the clock callable moves without copying");
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
        {
            Fixture fresh = make_fixture(1000);
            Fixture cycled = make_fixture();
            check(cycled.engine.init(cycled.engine_config),
                "default engine initializes before cycling");
            cycled.engine_config.arena_capacity = 1000;
            check(cycled.engine.init(cycled.engine_config),
                "cycling to a smaller arena succeeds");
            check(cycled.engine.idmap_reserved_bytes()
                == fresh.engine.idmap_reserved_bytes(),
                "cycled scratch matches a fresh engine at the same capacity");
            check(cycled.engine.retained_bytes() == fresh.engine.retained_bytes(),
                "cycled retained bytes match a fresh engine at the same capacity");
        }
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
        {
            Fixture big = make_zero_fixture();
            std::size_t const before_idmap = big.engine.idmap_reserved_bytes();
            check(before_idmap > 0, "a fresh engine reserves idmap scratch");
            big.engine_config.arena_capacity = 1000;
            check(big.engine.init(big.engine_config), "shrinking reinit succeeds");
            check(big.engine.idmap_reserved_bytes() < before_idmap,
                "reinitialization releases oversized idmap scratch");
            check(big.engine.arena_size() == 0,
                "shrinking reinit clears the arena");
            check(big.engine.retained_bytes() <= engine_alias::engine_memory_budget,
                "shrunk retained bytes stay within the budget");
        }
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

    void run_cache_unit_tests()
    {
        engine_alias::EvalCache cache;
        cache.init(engine_alias::CacheConfig::Layout::SetAssociative, 64, 1);
        auto board_at = [](std::uint16_t row0) {
            std::array<std::uint16_t, 48> rows = {};
            rows[0] = row0;
            return tetris::Board::from_rows(rows);
        };
        std::mt19937_64 rng(0xC0FFEE);
        auto hash_of = [](tetris::Board const &board) {
            return engine_alias::occupancy_hash(board.occupancy());
        };
        tetris::Board first;
        tetris::Board second;
        bool found_collision = false;
        for (int attempt = 0; attempt < 100000 && !found_collision; ++attempt)
        {
            std::uint16_t a = static_cast<std::uint16_t>(rng());
            std::uint16_t b = static_cast<std::uint16_t>(rng());
            if (a == b || (a & 0x3ff) == 0 || (b & 0x3ff) == 0)
            {
                continue;
            }
            tetris::Board board_a = board_at(static_cast<std::uint16_t>(a & 0x3ff));
            tetris::Board board_b = board_at(static_cast<std::uint16_t>(b & 0x3ff));
            if ((hash_of(board_a) & 63) == (hash_of(board_b) & 63))
            {
                first = board_a;
                second = board_b;
                found_collision = true;
            }
        }
        check(found_collision, "a forced index collision was constructed");
        if (!found_collision)
        {
            return;
        }
        toj_policy::Evaluation one;
        one.value = 11.5;
        toj_policy::Evaluation two;
        two.value = -7.25;
        cache.insert(first, one);
        check(cache.find(second) == std::nullopt,
            "a same-bucket distinct board never produces a false hit");
        cache.insert(second, two);
        check(cache.replacements() == 1,
            "a same-set insert without a free way replaces exactly once");
        check(cache.find(first) == std::nullopt,
            "the replaced entry is gone");
        auto hit = cache.find(second);
        check(hit.has_value() && hit->value == two.value,
            "the surviving entry verifies exactly and returns its value");
        check(cache.hits() == 1 && cache.misses() == 2,
            "collision counters account requests exactly");
        cache.insert(second, one);
        check(cache.find(second).has_value()
            && cache.find(second)->value == one.value,
            "repeated hits return the refreshed evaluation");
        cache.clear();
        check(cache.requests() == 0 && cache.hits() == 0 && cache.misses() == 0
            && cache.replacements() == 0,
            "clear invalidates entries and counters");
        check(cache.find(second) == std::nullopt,
            "cleared entries miss");
        {
            engine_alias::EvalCache assoc;
            assoc.init(engine_alias::CacheConfig::Layout::SetAssociative, 64, 4);
            std::mt19937_64 set_rng(0xBEEF);
            std::vector<tetris::Board> ways_boards;
            while (ways_boards.size() < 5)
            {
                std::uint16_t row = static_cast<std::uint16_t>(set_rng() & 0x3ff);
                if (row == 0)
                {
                    continue;
                }
                tetris::Board candidate = board_at(row);
                bool duplicate = false;
                for (auto const &existing : ways_boards)
                {
                    if (existing.occupancy() == candidate.occupancy())
                    {
                        duplicate = true;
                    }
                }
                if (!duplicate && (engine_alias::occupancy_hash(candidate.occupancy()) & 15)
                    == (ways_boards.empty()
                            ? (engine_alias::occupancy_hash(candidate.occupancy()) & 15)
                            : (engine_alias::occupancy_hash(ways_boards[0].occupancy()) & 15)))
                {
                    ways_boards.push_back(candidate);
                }
            }
            bool same_set = true;
            for (std::size_t i = 1; i < ways_boards.size(); ++i)
            {
                if ((engine_alias::occupancy_hash(ways_boards[i].occupancy()) & 15)
                    != (engine_alias::occupancy_hash(ways_boards[0].occupancy()) & 15))
                {
                    same_set = false;
                }
            }
            check(same_set, "the four-way probe boards share one set");
            if (same_set)
            {
                for (std::size_t i = 0; i < 4; ++i)
                {
                    toj_policy::Evaluation value;
                    value.value = static_cast<double>(i);
                    assoc.insert(ways_boards[i], value);
                }
                check(assoc.replacements() == 0,
                    "a four-way set absorbs four inserts without replacement");
                toj_policy::Evaluation fifth;
                fifth.value = 99.0;
                assoc.insert(ways_boards[4], fifth);
                check(assoc.replacements() == 1,
                    "the fifth same-set insert replaces the lowest stamp");
                check(assoc.find(ways_boards[0]) == std::nullopt,
                    "the lowest-stamp way was replaced");
                for (std::size_t i = 1; i < 4; ++i)
                {
                    check(assoc.find(ways_boards[i]).has_value(),
                        "the newer ways survive the rollover replacement");
                }
            }
        }
        {
            engine_alias::EvalCache rollover;
            std::uint64_t const near_wrap = std::numeric_limits<std::uint64_t>::max() - 2;
            rollover.init(engine_alias::CacheConfig::Layout::SetAssociative, 64, 4,
                near_wrap);
            std::mt19937_64 wrap_rng(0x5EED);
            std::vector<tetris::Board> wrap_boards;
            while (wrap_boards.size() < 5)
            {
                std::uint16_t row = static_cast<std::uint16_t>(wrap_rng() & 0x3ff);
                if (row == 0)
                {
                    continue;
                }
                tetris::Board candidate = board_at(row);
                bool duplicate = false;
                for (auto const &existing : wrap_boards)
                {
                    if (existing.occupancy() == candidate.occupancy())
                    {
                        duplicate = true;
                    }
                }
                if (!duplicate
                    && (wrap_boards.empty()
                            || (engine_alias::occupancy_hash(candidate.occupancy()) & 15)
                                == (engine_alias::occupancy_hash(wrap_boards[0].occupancy())
                                    & 15)))
                {
                    wrap_boards.push_back(candidate);
                }
            }
            for (std::size_t i = 0; i < 4; ++i)
            {
                toj_policy::Evaluation value;
                value.value = static_cast<double>(i);
                rollover.insert(wrap_boards[i], value);
            }
            toj_policy::Evaluation wrapped;
            wrapped.value = 123.0;
            rollover.insert(wrap_boards[4], wrapped);
            check(rollover.replacements() == 1,
                "the stamp crossing zero replaces once by circular age");
            check(rollover.find(wrap_boards[0]) == std::nullopt,
                "the circular-oldest way was replaced across the wrap");
            for (std::size_t i = 1; i < 4; ++i)
            {
                check(rollover.find(wrap_boards[i]).has_value(),
                    "entries stamped after the wrap stay reachable");
            }
            auto wrapped_hit = rollover.find(wrap_boards[4]);
            check(wrapped_hit.has_value() && wrapped_hit->value == wrapped.value,
                "the wrapped insert verifies exactly");
        }
        {
            engine_alias::EvalCache domain;
            domain.init(engine_alias::CacheConfig::Layout::DirectMapped, 64, 1);
            std::array<std::uint16_t, 48> upper_a = {};
            upper_a[0] = 0x001;
            upper_a[40] = 0x100;
            std::array<std::uint16_t, 48> upper_b = {};
            upper_b[0] = 0x001;
            upper_b[40] = 0x200;
            tetris::Board board_a = tetris::Board::from_rows(upper_a);
            tetris::Board board_b = tetris::Board::from_rows(upper_b);
            toj_policy::Evaluation value_a;
            value_a.value = 42.0;
            toj_policy::Evaluation value_b;
            value_b.value = 43.0;
            domain.insert(board_a, value_a);
            check(domain.find(board_b) == std::nullopt,
                "upper-domain boards differing in one high row never alias");
            auto hit = domain.find(board_a);
            check(hit.has_value() && hit->value == value_a.value,
                "an upper-storage-domain board hits its own entry");
            domain.insert(board_b, value_b);
            auto hit_b = domain.find(board_b);
            check(hit_b.has_value() && hit_b->value == value_b.value,
                "a second upper-domain board keeps its own evaluation");
            std::println("eval cache: exact verification, replacement, counters, clear");
        }
    }

    struct NodeFingerprint
    {
        std::array<std::uint64_t, 8> occupancy{};
        double eval_value = 0;
        std::int16_t eval_t2 = 0;
        std::int16_t eval_t3 = 0;
        double state_value = 0;
        double state_acc = 0;
        double state_like = 0;
        std::int8_t state_death = 0;
        std::int8_t state_combo = 0;
        std::int8_t state_under_attack = 0;
        std::int8_t state_map_rise = 0;
        std::int8_t state_b2b = 0;
        std::int16_t state_t2 = 0;
        std::int16_t state_t3 = 0;
        std::size_t cursor = 0;

        bool operator==(NodeFingerprint const &) const = default;
    };

    NodeFingerprint fingerprint(engine_alias::Node const *node)
    {
        NodeFingerprint print;
        for (int i = 0; i < 8; ++i)
        {
            print.occupancy[static_cast<std::size_t>(i)] =
                node->board.occupancy().logical_word(i);
        }
        print.eval_value = node->evaluation.value;
        print.eval_t2 = node->evaluation.t2_value;
        print.eval_t3 = node->evaluation.t3_value;
        print.state_value = node->policy.value;
        print.state_acc = node->policy.acc_value;
        print.state_like = node->policy.like;
        print.state_death = node->policy.death;
        print.state_combo = node->policy.combo;
        print.state_under_attack = node->policy.under_attack;
        print.state_map_rise = node->policy.map_rise;
        print.state_b2b = node->policy.b2b;
        print.state_t2 = node->policy.t2_value;
        print.state_t3 = node->policy.t3_value;
        print.cursor = node->cursor;
        return print;
    }

    bool same_node_print(engine_alias::Node const *warm_node,
        engine_alias::Node const *cold_node, bool is_root)
    {
        if (!(fingerprint(warm_node) == fingerprint(cold_node))
            || warm_node->depth != cold_node->depth
            || warm_node->parent != cold_node->parent
            || warm_node->hold.piece != cold_node->hold.piece
            || warm_node->hold.locked != cold_node->hold.locked)
        {
            return false;
        }
        if (is_root)
        {
            return true;
        }
        return warm_node->played == cold_node->played
            && warm_node->source == cold_node->source
            && warm_node->has_incoming == cold_node->has_incoming
            && warm_node->incoming.placement == cold_node->incoming.placement
            && warm_node->incoming.arrival == cold_node->incoming.arrival;
    }

    tetris::Placement canonical_spawn()
    {
        return tetris::Placement::unchecked(tetris::toj::spawn_x,
            tetris::toj::spawn_y, 0);
    }

    tetris::Placement buried_start()
    {
        return tetris::Placement::unchecked(4, 10, 0);
    }

    bool alphabet_clean(std::string_view commands)
    {
        for (char command : commands)
        {
            if (command != 'l' && command != 'r' && command != 'd'
                && command != 'z' && command != 'c' && command != 'x'
                && command != 'D')
            {
                return false;
            }
        }
        return true;
    }

    void check_oracle_landable(tetris::Board const &board, tetris::Piece piece,
        engine_alias::Candidate const &candidate, bool allow_180, char const *what)
    {
        std::array<std::uint16_t, 48> rows = {};
        for (int y = 0; y < 48; ++y)
        {
            rows[static_cast<std::size_t>(y)] = board.row(y);
        }
        reachability::call_with_block<tetris::toj::SRS>(piece,
            [&]<reachability::block B>() {
                scalar_arrival::ScalarConfig oracle_config{};
                oracle_config.allow_180 = allow_180;
                oracle_config.allow_softdrop = true;
                oracle_config.allow_sonicdrop = true;
                oracle_config.allow_20g = false;
                auto geometry = scalar_arrival::make_geometry<B>();
                scalar_arrival::ScalarOracle<B> oracle{geometry, oracle_config, rows};
                oracle.run(
                    reachability::coord{tetris::toj::spawn_x, tetris::toj::spawn_y}, 0);
                int channel = candidate.arrival
                        == tetris::ArrivalClass::TerminalRotation
                    ? 1
                    : 0;
                auto words = oracle.landable_words(channel, true);
                int const o = candidate.placement.rotation();
                int const x = candidate.placement.x();
                int const y = candidate.placement.y();
                check(o >= 0 && o < B.orientations && x >= 0
                    && x < tetris::Board::width && y >= 0 && y < 48
                    && (words[static_cast<std::size_t>(o)][static_cast<std::size_t>(y / 6)]
                        & (std::uint64_t(1)
                            << ((y % 6) * tetris::Board::width + x)))
                        != 0,
                    what);
                return 0;
            });
    }

    void check_final_replay(tetris::Board const &board, tetris::Board const &expected,
        tetris::Piece piece, tetris::Placement start,
        engine_alias::Candidate const &candidate, std::string_view commands,
        bool allow_180, bool expandable, char const *what)
    {
        tetris::path::PathConfig path_config{};
        path_config.allow_180 = allow_180;
        check(alphabet_clean(commands), what);
        check(commands.size() <= tetris::path::Path::max_payload, what);
        auto locked = tetris::path::replay_path(board, piece, start, commands,
            path_config, true);
        check(locked.valid && locked.placement == candidate.placement, what);
        if (piece == tetris::Piece::T)
        {
            check(locked.arrival == candidate.arrival, what);
        }
        auto open = tetris::path::replay_path(board, piece, start, commands,
            path_config, false);
        check(open.valid, what);
        if (piece == tetris::Piece::T)
        {
            check(open.arrival == candidate.arrival, what);
        }
        auto applied = tetris::toj::apply(board, piece, candidate);
        check(applied.has_value(), what);
        if (applied.has_value())
        {
            check(applied->board.occupancy() == expected.occupancy(), what);
            check(expandable == !applied->lockout, what);
        }
        check_oracle_landable(board, piece, candidate, allow_180, what);
    }

    void run_cache_parity_tests()
    {
        std::vector<tetris::Board> boards;
        boards.push_back(shelf_board());
        boards.push_back(tetris::Board{});
        std::array<std::uint16_t, 48> rows = {};
        rows[0] = 0x0ff;
        rows[1] = 0x1ff;
        rows[2] = 0x0f8;
        rows[3] = 0x3c0;
        boards.push_back(tetris::Board::from_rows(rows));
        std::string const queue_text = "III";
        auto run_variant = [&](engine_alias::CacheConfig::Layout layout) {
            Fixture fixture = make_fixture();
            fixture.engine_config.cache.layout = layout;
            check(fixture.engine.init(fixture.engine_config),
                "parity fixture initializes");
            std::vector<engine_alias::SearchSelection> selections;
            std::vector<std::size_t> arena_sizes;
            std::vector<NodeFingerprint> prints;
            for (auto const &board : boards)
            {
                make_root(fixture, board, queue_text, std::nullopt, true);
                fixture.engine.run(300);
                auto selection = fixture.engine.select_best();
                check(selection.has_value(), "parity search selects");
                selections.push_back(selection.value_or(engine_alias::SearchSelection{}));
                arena_sizes.push_back(fixture.engine.arena_size());
                for (std::size_t id = 0; id < fixture.engine.arena_size(); ++id)
                {
                    prints.push_back(fingerprint(
                        fixture.engine.node(static_cast<engine_alias::NodeId>(id))));
                }
            }
            return std::tuple(selections, arena_sizes, prints);
        };
        auto const disabled = run_variant(engine_alias::CacheConfig::Layout::Disabled);
        auto const direct = run_variant(engine_alias::CacheConfig::Layout::DirectMapped);
        auto const assoc = run_variant(engine_alias::CacheConfig::Layout::SetAssociative);
        check(disabled == direct && disabled == assoc,
            "cached and disabled runs produce identical materialized trees");
        std::println("cache parity: disabled, direct-mapped, set-associative agree");
    }

    void run_cache_counter_tests()
    {
        {
            Fixture fixture = make_fixture();
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            auto stats = fixture.engine.search_stats();
            check(stats.cache_requests == stats.cache_hits + stats.cache_misses,
                "cache requests split into hits and misses");
            check(stats.cache_misses == stats.eval_computed,
                "every cache miss computes exactly one evaluation");
            check(stats.eval_requests == stats.eval_memo_hits + stats.cache_requests,
                "requests split into memo hits and cache lookups");
            check(stats.cache_hits > 0,
                "the workload exercises cache hits");
            stats = fixture.engine.search_stats();
            check(stats.cache_requests == stats.cache_hits + stats.cache_misses,
                "counters stay consistent after completion");
        }
        {
            Fixture fixture = make_fixture();
            fixture.engine_config.cache.layout = engine_alias::CacheConfig::Layout::Disabled;
            check(fixture.engine.init(fixture.engine_config),
                "disabled-cache fixture initializes");
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            auto stats = fixture.engine.search_stats();
            check(stats.cache_requests == 0 && stats.cache_hits == 0
                && stats.cache_misses == 0 && stats.cache_replacements == 0,
                "a disabled cache reports no counters");
            check(stats.eval_computed == stats.eval_requests - stats.eval_memo_hits,
                "disabled-cache evaluations match requests minus memo hits");
        }
        {
            Fixture fixture = make_fixture();
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(3);
            check(fixture.engine.arena_size() > 1,
                "pre-reinitialization search materializes");
            check(fixture.engine.init(fixture.engine_config),
                "reinitialization with the same policy");
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            auto stats = fixture.engine.search_stats();
            check(stats.cache_hits >= 0 && stats.cache_requests
                    == stats.cache_hits + stats.cache_misses,
                "reinitialization invalidates without stale hits corrupting counts");
        }
        {
            Fixture fixture = make_zero_fixture();
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            engine_alias::Engine moved(std::move(fixture.engine));
            auto before = moved.search_stats();
            auto queue = engine_alias::parse_queue("III");
            check(queue.has_value(), "moved-engine queue parses");
            toj_policy::State root_state;
            engine_alias::HoldState root_hold;
            check(moved.set_root(shelf_board(), root_state, std::move(*queue), root_hold)
                != engine_alias::no_node,
                "a moved engine takes a new root");
            moved.run(300);
            auto after = moved.search_stats();
            check(after.cache_requests >= before.cache_requests,
                "a moved engine keeps its cache functional");
            check(after.cache_requests == after.cache_hits + after.cache_misses,
                "a moved engine keeps its cache accounting consistent");
        }
        {
            Fixture direct = make_fixture();
            Fixture assoc = make_fixture();
            assoc.engine_config.cache.layout = engine_alias::CacheConfig::Layout::SetAssociative;
            check(assoc.engine.init(assoc.engine_config),
                "set-associative fixture initializes");
            std::uint64_t direct_replacements = 0;
            std::uint64_t assoc_replacements = 0;
            std::uint64_t direct_requests = 0;
            std::uint64_t assoc_requests = 0;
            for (int r = 0; r < 2; ++r)
            {
                make_root(direct, shelf_board(), "III", std::nullopt, true);
                direct.engine.run(300);
                make_root(assoc, shelf_board(), "III", std::nullopt, true);
                assoc.engine.run(300);
                auto dm_stats = direct.engine.search_stats();
                auto sa_stats = assoc.engine.search_stats();
                direct_replacements += dm_stats.cache_replacements;
                assoc_replacements += sa_stats.cache_replacements;
                direct_requests += dm_stats.cache_requests;
                assoc_requests += sa_stats.cache_requests;
            }
            check(direct_replacements > assoc_replacements,
                "direct-mapped and set-associative layouts behave differently");
            check(assoc_replacements > 0,
                "the set-associative layout also replaces under conflicts");
            check(direct_requests == assoc_requests,
                "both layouts see the same lookup stream");
        }
        {
            Fixture fixture = make_fixture();
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            auto const warm_selection = fixture.engine.select_best();
            std::vector<NodeFingerprint> warm_prints;
            for (std::size_t id = 0; id < fixture.engine.arena_size(); ++id)
            {
                warm_prints.push_back(fingerprint(
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id))));
            }
            fixture.policy_config.parameters.base = 999.0;
            check(fixture.engine.init(fixture.engine_config),
                "reinitialization with changed evaluation parameters");
            make_root(fixture, shelf_board(), "III", std::nullopt, true);
            fixture.engine.run(300);
            auto const changed_selection = fixture.engine.select_best();
            std::vector<NodeFingerprint> changed_prints;
            for (std::size_t id = 0; id < fixture.engine.arena_size(); ++id)
            {
                changed_prints.push_back(fingerprint(
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id))));
            }
            Fixture fresh = make_fixture();
            fresh.policy_config.parameters.base = 999.0;
            check(fresh.engine.init(fresh.engine_config),
                "fresh changed-parameter engine initializes");
            make_root(fresh, shelf_board(), "III", std::nullopt, true);
            fresh.engine.run(300);
            auto const fresh_selection = fresh.engine.select_best();
            std::vector<NodeFingerprint> fresh_prints;
            for (std::size_t id = 0; id < fresh.engine.arena_size(); ++id)
            {
                fresh_prints.push_back(fingerprint(
                    fresh.engine.node(static_cast<engine_alias::NodeId>(id))));
            }
            check(changed_selection == fresh_selection
                && changed_prints == fresh_prints,
                "changed-parameter results match a fresh engine exactly");
            check(changed_prints != warm_prints,
                "stale warm-cache results cannot survive the parameter change");
        }
        std::println("cache counters: requests, hits, misses, replacements, memo");
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

    std::string queue_text(engine_alias::Queue const &queue)
    {
        std::string text;
        for (auto piece : queue.pieces)
        {
            text += reachability::rules::Tetromino::name_of(piece);
        }
        return text;
    }

    void run_reuse_tests()
    {
        toj_policy::State state;
        engine_alias::HoldState no_hold;
        no_hold.locked = true;
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "reuse first-turn queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold)
                != engine_alias::no_node,
                "reuse first turn takes");
            check(fixture.engine.run(2000), "reuse first search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "first search selects");
            std::size_t const first_arena = fixture.engine.arena_size();
            check(first_arena > 8, "the first search materializes a nontrivial tree");
            auto const *child = fixture.engine.node(selection->root_child);
            engine_alias::Queue next = remaining_queue(fixture.engine.queue(), 1);
            toj_policy::State const next_policy = child->policy;
            engine_alias::Board const next_board = child->board;
            engine_alias::HoldState const next_hold = child->hold;
            std::string const next_text = queue_text(next);

            check(fixture.engine.set_root(next_board, next_policy, std::move(next),
                next_hold)
                != engine_alias::no_node,
                "the matched position reroots");
            check(fixture.engine.arena_size() > 2,
                "rerooting retains a nontrivial subtree");
            check(fixture.engine.arena_size() <= first_arena,
                "rerooting does not grow the arena");
            check(fixture.engine.node(0)->first_child != engine_alias::no_node,
                "the new root retains children");
            {
                auto fresh_stats = fixture.engine.search_stats();
                check(fresh_stats.eval_requests == 0 && fresh_stats.eval_computed == 0
                    && fresh_stats.cache_requests == 0 && fresh_stats.cache_hits == 0
                    && fresh_stats.cache_misses == 0
                    && fresh_stats.materialized_nodes == 0
                    && fresh_stats.transposition_merges == 0
                    && fresh_stats.expanded_parents == 0
                    && fresh_stats.widening_passes == 0,
                    "a matching root change resets per-search telemetry");
            }

            check(fixture.engine.run(2000), "the warm second search completes");
            auto warm_stats = fixture.engine.search_stats();
            check_child_links(fixture.engine,
                "warm child chains enumerate ownership");
            check(warm_stats.cache_requests
                == warm_stats.cache_hits + warm_stats.cache_misses,
                "warm cache lookups split into hits and misses");
            check(warm_stats.cache_hits > 0,
                "the warm search reuses cached evaluations");
            check(warm_stats.eval_computed < warm_stats.eval_requests,
                "the warm search avoids evaluation work");
            check(warm_stats.transposition_merges > 0,
                "the warm search merges retained nodes instead of rematerializing");

            Fixture cold = make_fixture();
            auto cold_queue = engine_alias::parse_queue(next_text);
            check(cold_queue.has_value(), "cold queue parses");
            check(cold.engine.set_root(next_board, next_policy, std::move(*cold_queue),
                next_hold)
                != engine_alias::no_node,
                "cold second turn takes");
            check(cold.engine.run(2000), "the cold second search completes");
            auto cold_stats = cold.engine.search_stats();
            check_child_links(cold.engine,
                "cold child chains enumerate ownership");
            check(cold_stats.eval_computed > warm_stats.eval_computed,
                "the warm search avoids evaluation work against a cold search");
            check(cold_stats.materialized_nodes > warm_stats.materialized_nodes,
                "the warm search materializes fewer nodes than a cold search");
            auto warm_selection = fixture.engine.select_best();
            auto cold_selection = cold.engine.select_best();
            check(warm_selection.has_value() && cold_selection.has_value()
                && warm_selection->root_child == cold_selection->root_child
                && warm_selection->evidence == cold_selection->evidence,
                "warm and cold searches select the same move");
            std::vector<NodeFingerprint> warm_prints;
            std::vector<NodeFingerprint> cold_prints;
            for (std::size_t id = 0; id < fixture.engine.arena_size(); ++id)
            {
                warm_prints.push_back(fingerprint(
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id))));
            }
            for (std::size_t id = 0; id < cold.engine.arena_size(); ++id)
            {
                cold_prints.push_back(fingerprint(
                    cold.engine.node(static_cast<engine_alias::NodeId>(id))));
            }
            check(warm_prints.size() == cold_prints.size(),
                "warm and cold materialize the same node count");
            bool semantics_match = warm_prints.size() == cold_prints.size();
            for (std::size_t i = 0; i < warm_prints.size() && semantics_match; ++i)
            {
                auto const *warm_node = fixture.engine.node(
                    static_cast<engine_alias::NodeId>(i));
                auto const *cold_node = cold.engine.node(
                    static_cast<engine_alias::NodeId>(i));
                if (!(warm_prints[i] == cold_prints[i])
                    || !same_node_print(warm_node, cold_node, i == 0))
                {
                    semantics_match = false;
                }
            }
            check(semantics_match,
                "retained node semantics match the fresh-root construction");
            bool attribution_ok = true;
            std::size_t bad_count = 0;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node =
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                auto const *parent = fixture.engine.node(node->parent);
                engine_alias::NodeId const expected = node->depth == 1
                    ? engine_alias::first_move_fingerprint(node->played, node->incoming,
                        node->source)
                    : parent->root_child;
                if (node->root_child != expected)
                {
                    attribution_ok = false;
                    if (bad_count < 3)
                    {
                        ++bad_count;
                    }
                }
            }
            check(attribution_ok,
                "attribution is rooted in the new root after reuse");
        }
        {
            auto populate_shallow = [&]() {
                Fixture f = make_fixture();
                auto queue = engine_alias::parse_queue("TIS");
                check(queue.has_value(), "negative-population queue parses");
                check(f.engine.set_root(shelf_board(), state, std::move(*queue),
                    no_hold) != engine_alias::no_node,
                    "negative-population root takes");
                f.engine.run(2);
                return f;
            };
            auto reuse_target = [&](Fixture &f) {
                for (std::size_t id = 1; id < f.engine.arena_size(); ++id)
                {
                    auto const *node = f.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (node->depth == 1 && node->child_count > 0)
                    {
                        return static_cast<engine_alias::NodeId>(id);
                    }
                }
                return engine_alias::no_node;
            };
            {
                Fixture control = populate_shallow();
                engine_alias::NodeId target = reuse_target(control);
                check(target != engine_alias::no_node,
                    "the control population expands a root child");
                if (target != engine_alias::no_node)
                {
                    auto const *child = control.engine.node(target);
                    engine_alias::Queue next = remaining_queue(control.engine.queue(),
                        child->cursor);
                    check(control.engine.set_root(child->board, child->policy,
                        std::move(next), child->hold) != engine_alias::no_node,
                        "the control position reroots");
                    check(control.engine.arena_size() > 1,
                        "the control reroot retains the subtree");
                }
            }
            auto attempt = [&](auto vary, char const *what) {
                Fixture f = populate_shallow();
                engine_alias::NodeId target = reuse_target(f);
                check(target != engine_alias::no_node, what);
                if (target == engine_alias::no_node)
                {
                    return;
                }
                auto const *child = f.engine.node(target);
                engine_alias::Queue next = remaining_queue(f.engine.queue(),
                    child->cursor);
                auto [v_board, v_policy, v_queue, v_hold] =
                    vary(child, std::move(next));
                check(f.engine.set_root(v_board, v_policy, std::move(v_queue), v_hold)
                    != engine_alias::no_node,
                    what);
                check(f.engine.arena_size() == 1, what);
            };
            auto occupancy_vary = [&](auto const *child, auto queue) {
                std::array<std::uint16_t, 48> rows = {};
                for (int y = 0; y < 48; ++y)
                {
                    rows[static_cast<std::size_t>(y)] = child->board.row(y);
                }
                std::uint16_t &row = rows[40];
                row = row != 0
                    ? static_cast<std::uint16_t>(row & (row - 1))
                    : static_cast<std::uint16_t>(0x001);
                return std::tuple(tetris::Board::from_rows(rows), child->policy,
                    std::move(queue), child->hold);
            };
            auto death_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.death = 1;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto combo_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.combo = 1;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto under_attack_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.under_attack = 1;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto map_rise_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.map_rise = 1;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto b2b_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.b2b = 1;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto t2_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.t2_value = 7;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto t3_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.t3_value = 7;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto acc_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.acc_value = policy.acc_value + 1.0;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto like_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.like = policy.like + 1.0;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto value_vary = [&](auto const *child, auto queue) {
                toj_policy::State policy = child->policy;
                policy.value = policy.value + 1.0;
                return std::tuple(child->board, policy, std::move(queue), child->hold);
            };
            auto active_vary = [&](auto const *child, auto queue) {
                queue.pieces[0] = queue.pieces[0] == tetris::Piece::T
                    ? tetris::Piece::I
                    : tetris::Piece::T;
                return std::tuple(child->board, child->policy, std::move(queue),
                    child->hold);
            };
            auto boundary_vary = [&](auto const *child, auto queue) {
                queue.boundary[0] = !queue.boundary[0];
                return std::tuple(child->board, child->policy, std::move(queue),
                    child->hold);
            };
            auto hold_piece_vary = [&](auto const *child, auto queue) {
                engine_alias::HoldState hold = child->hold;
                hold.piece = hold.piece == tetris::Piece::T
                    ? tetris::Piece::I
                    : tetris::Piece::T;
                return std::tuple(child->board, child->policy, std::move(queue), hold);
            };
            auto hold_lock_vary = [&](auto const *child, auto queue) {
                engine_alias::HoldState hold = child->hold;
                hold.locked = !hold.locked;
                return std::tuple(child->board, child->policy, std::move(queue), hold);
            };
            auto shorter_vary = [&](auto const *child, auto queue) {
                queue.pieces.pop_back();
                queue.boundary.pop_back();
                return std::tuple(child->board, child->policy, std::move(queue),
                    child->hold);
            };
            auto longer_vary = [&](auto const *child, auto queue) {
                queue.pieces.push_back(tetris::Piece::T);
                queue.boundary.push_back(false);
                return std::tuple(child->board, child->policy, std::move(queue),
                    child->hold);
            };
            auto marker_vary = [&](auto const *child, auto queue) {
                queue.marker_count += 1;
                return std::tuple(child->board, child->policy, std::move(queue),
                    child->hold);
            };
            attempt(occupancy_vary, "a row-40 occupancy change misses reuse");
            attempt(death_vary, "a death difference misses reuse");
            attempt(combo_vary, "a combo difference misses reuse");
            attempt(under_attack_vary, "an under-attack difference misses reuse");
            attempt(map_rise_vary, "a map-rise difference misses reuse");
            attempt(b2b_vary, "a b2b difference misses reuse");
            attempt(t2_vary, "a t2 difference misses reuse");
            attempt(t3_vary, "a t3 difference misses reuse");
            attempt(acc_vary, "an accumulated value difference misses reuse");
            attempt(like_vary, "a like difference misses reuse");
            attempt(value_vary, "a value difference misses reuse");
            attempt(active_vary, "an active-piece difference misses reuse");
            attempt(boundary_vary, "a boundary-bit change misses reuse");
            attempt(hold_piece_vary, "a hold-piece difference misses reuse");
            attempt(hold_lock_vary, "a hold-availability change misses reuse");
            attempt(shorter_vary, "a shorter remaining sequence misses reuse");
            attempt(longer_vary, "an extended remaining sequence misses reuse");
            attempt(marker_vary, "an invented marker misses reuse");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TI");
            toj_policy::State state;
            engine_alias::HoldState hold;
            hold.piece = tetris::Piece::I;
            hold.locked = false;
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue), hold)
                != engine_alias::no_node,
                "hold-active first turn takes");
            check(fixture.engine.run(2000), "hold-active search completes");
            engine_alias::NodeId hold_child = engine_alias::no_node;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node =
                    fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->source == engine_alias::BranchSource::Hold)
                {
                    hold_child = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(hold_child != engine_alias::no_node, "the hold-swap child exists");
            if (hold_child != engine_alias::no_node)
            {
                auto const *child = fixture.engine.node(hold_child);
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                engine_alias::Queue cold_next = next;
                auto const post_swap_hold = child->hold;
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the hold-swap position reroots");
                check(fixture.engine.arena_size() > 2,
                    "the hold-swap reroot retains a subtree");
                check(fixture.engine.node(0)->hold.piece == post_swap_hold.piece,
                    "the rerooted root keeps the post-swap hold");
                check(fixture.engine.run(2000),
                    "the hold-swap second search completes");
                auto warm_selection = fixture.engine.select_best();
                Fixture cold = make_fixture();
                auto const *kept = fixture.engine.node(0);
                check(cold.engine.set_root(kept->board, kept->policy,
                    std::move(cold_next), kept->hold) != engine_alias::no_node,
                    "hold-swap cold second turn takes");
                check(cold.engine.run(2000), "the hold-swap cold search completes");
                auto cold_selection = cold.engine.select_best();
                check(warm_selection.has_value() && cold_selection.has_value()
                    && warm_selection->root_child == cold_selection->root_child
                    && warm_selection->evidence == cold_selection->evidence,
                    "hold-swap warm and cold searches select the same move");
                check(fixture.engine.arena_size() == cold.engine.arena_size(),
                    "hold-swap warm and cold materialize the same node count");
                bool hold_parity =
                    fixture.engine.arena_size() == cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && hold_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        hold_parity = false;
                    }
                }
                check(hold_parity,
                    "hold-swap warm and cold share full node semantics");
                check_child_links(fixture.engine,
                    "hold-swap child chains enumerate ownership");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TI");
            toj_policy::State state;
            engine_alias::HoldState empty_hold;
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                empty_hold)
                != engine_alias::no_node,
                "empty-hold first turn takes");
            check(fixture.engine.run(2000), "empty-hold search completes");
            engine_alias::NodeId empty_child = engine_alias::no_node;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node = fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->cursor == 2
                    && node->source == engine_alias::BranchSource::Hold)
                {
                    empty_child = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(empty_child != engine_alias::no_node,
                "an empty-hold consumption child exists");
            if (empty_child != engine_alias::no_node)
            {
                auto const *child = fixture.engine.node(empty_child);
                auto fresh = engine_alias::parse_queue("SZ");
                check(fresh.has_value(), "the fresh second-turn queue parses");
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(*fresh), child->hold) != engine_alias::no_node,
                    "the exhausted position takes a clean root");
                check(fixture.engine.arena_size() == 1,
                    "an exhausted queue misses reuse");
                check(fixture.engine.node(0)->cursor == 0,
                    "the clean root cursor is zero");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            toj_policy::State state;
            engine_alias::HoldState empty_hold;
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                empty_hold)
                != engine_alias::no_node,
                "advancing empty-hold first turn takes");
            check(fixture.engine.run(2000), "advancing empty-hold search completes");
            engine_alias::NodeId empty_child = engine_alias::no_node;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node = fixture.engine.node(static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->cursor == 2
                    && node->source == engine_alias::BranchSource::Hold)
                {
                    empty_child = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(empty_child != engine_alias::no_node,
                "an advancing empty-hold consumption child exists");
            if (empty_child != engine_alias::no_node)
            {
                auto const *child = fixture.engine.node(empty_child);
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                check(!next.pieces.empty(),
                    "the empty-hold advance leaves queue behind");
                engine_alias::Queue cold_next = next;
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the empty-hold position reroots");
                check(fixture.engine.arena_size() > 1,
                    "the empty-hold reroot retains a subtree");
                check(fixture.engine.node(0)->cursor == 0,
                    "the rerooted root cursor is zero");
                check(fixture.engine.run(2000),
                    "the empty-hold second search completes");
                auto warm_selection = fixture.engine.select_best();
                Fixture cold = make_fixture();
                auto const *kept = fixture.engine.node(0);
                check(cold.engine.set_root(kept->board, kept->policy,
                    std::move(cold_next), kept->hold) != engine_alias::no_node,
                    "empty-hold cold second turn takes");
                check(cold.engine.run(2000),
                    "the empty-hold cold search completes");
                auto cold_selection = cold.engine.select_best();
                check(warm_selection.has_value() && cold_selection.has_value()
                    && warm_selection->root_child == cold_selection->root_child
                    && warm_selection->evidence == cold_selection->evidence,
                    "empty-hold warm and cold searches select the same move");
                check(fixture.engine.arena_size() == cold.engine.arena_size(),
                    "empty-hold warm and cold materialize the same node count");
                bool empty_parity =
                    fixture.engine.arena_size() == cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && empty_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        empty_parity = false;
                    }
                }
                check(empty_parity,
                    "empty-hold warm and cold share full node semantics");
                check_child_links(fixture.engine,
                    "empty-hold child chains enumerate ownership");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto marked = engine_alias::parse_queue("T?IS");
            check(marked.has_value() && marked->marker_count == 1,
                "marker queue parses with one marker");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*marked), no_hold)
                != engine_alias::no_node,
                "marker first turn takes");
            check(fixture.engine.run(2000), "marker search completes");
            auto const *first_child = fixture.engine.node(1);
            engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                first_child->cursor);
            next.boundary[0] = !next.boundary[0];
            toj_policy::State state_four;
            check(fixture.engine.set_root(first_child->board, state_four,
                std::move(next), no_hold)
                != engine_alias::no_node,
                "the marker-shifted position takes a clean root");
            check(fixture.engine.arena_size() == 1,
                "a marker-shifted boundary misses reuse");
        }
        {
            Fixture fixture = make_fixture();
            auto marked = engine_alias::parse_queue("T?IS");
            check(marked.has_value() && marked->marker_count == 1,
                "marked reuse queue parses with one marker");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*marked),
                no_hold) != engine_alias::no_node,
                "marked reuse first turn takes");
            check(fixture.engine.run(2000), "marked reuse search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "marked reuse search selects");
            if (selection.has_value())
            {
                auto const *child = fixture.engine.node(selection->root_child);
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the marked position reroots");
                check(fixture.engine.arena_size() > 1,
                    "the marked reroot retains a subtree");
                check(fixture.engine.run(2000),
                    "the marked second search completes");
                auto marked_selection = fixture.engine.select_best();
                Fixture cold = make_fixture();
                auto cold_queue = engine_alias::parse_queue("IS");
                check(cold_queue.has_value(), "marked cold queue parses");
                auto const *kept = fixture.engine.node(0);
                check(cold.engine.set_root(kept->board, kept->policy,
                    std::move(*cold_queue), kept->hold) != engine_alias::no_node,
                    "marked cold second turn takes");
                check(cold.engine.run(2000), "the marked cold search completes");
                auto cold_selection = cold.engine.select_best();
                check(marked_selection.has_value() && cold_selection.has_value()
                    && marked_selection->root_child == cold_selection->root_child
                    && marked_selection->evidence == cold_selection->evidence,
                    "marked warm and cold searches select the same move");
                check(fixture.engine.arena_size() == cold.engine.arena_size(),
                    "marked warm and cold materialize the same node count");
                bool marked_parity =
                    fixture.engine.arena_size() == cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && marked_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        marked_parity = false;
                    }
                }
                check(marked_parity,
                    "marked warm and cold share full node semantics");
                check_child_links(fixture.engine,
                    "marked child chains enumerate ownership");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto marked = engine_alias::parse_queue("T?IS");
            check(marked.has_value() && marked->marker_count == 1,
                "consumed-marker queue parses with one marker");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*marked),
                no_hold) != engine_alias::no_node,
                "consumed-marker first turn takes");
            check(fixture.engine.run(2000), "consumed-marker search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "consumed-marker search selects");
            if (selection.has_value())
            {
                auto const *child = fixture.engine.node(selection->root_child);
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                check(next.marker_count > 0,
                    "the played prefix owns the marker");
                next.marker_count -= 1;
                engine_alias::Queue cold_next = next;
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the consumed-marker position reroots");
                check(fixture.engine.arena_size() > 1,
                    "the consumed-marker reroot retains a subtree");
                check(fixture.engine.run(2000),
                    "the consumed-marker second search completes");
                auto warm_selection = fixture.engine.select_best();
                Fixture cold = make_fixture();
                auto const *kept = fixture.engine.node(0);
                check(cold.engine.set_root(kept->board, kept->policy,
                    std::move(cold_next), kept->hold) != engine_alias::no_node,
                    "consumed-marker cold second turn takes");
                check(cold.engine.run(2000),
                    "the consumed-marker cold search completes");
                auto cold_selection = cold.engine.select_best();
                check(warm_selection.has_value() && cold_selection.has_value()
                    && warm_selection->root_child == cold_selection->root_child
                    && warm_selection->evidence == cold_selection->evidence,
                    "consumed-marker warm and cold select the same move");
                check(fixture.engine.arena_size() == cold.engine.arena_size(),
                    "consumed-marker warm and cold match node counts");
                bool consumed_parity =
                    fixture.engine.arena_size() == cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && consumed_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        consumed_parity = false;
                    }
                }
                check(consumed_parity,
                    "consumed-marker warm and cold share full node semantics");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TI");
            toj_policy::State state;
            engine_alias::HoldState hold;
            hold.piece = tetris::Piece::I;
            hold.locked = true;
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                hold) != engine_alias::no_node,
                "locked-hold first turn takes");
            check(fixture.engine.run(2000), "locked-hold search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "locked-hold search selects");
            if (selection.has_value())
            {
                auto const *child = fixture.engine.node(selection->root_child);
                check(child->cursor == 1,
                    "the locked-hold advance plays the current piece");
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                engine_alias::Queue cold_next = next;
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the locked-hold position reroots");
                check(fixture.engine.arena_size() > 1,
                    "the locked-hold reroot retains a subtree");
                check(fixture.engine.run(2000),
                    "the locked-hold second search completes");
                auto warm_selection = fixture.engine.select_best();
                Fixture cold = make_fixture();
                auto const *kept = fixture.engine.node(0);
                check(cold.engine.set_root(kept->board, kept->policy,
                    std::move(cold_next), kept->hold) != engine_alias::no_node,
                    "locked-hold cold second turn takes");
                check(cold.engine.run(2000),
                    "the locked-hold cold search completes");
                auto cold_selection = cold.engine.select_best();
                check(warm_selection.has_value() && cold_selection.has_value()
                    && warm_selection->root_child == cold_selection->root_child
                    && warm_selection->evidence == cold_selection->evidence,
                    "locked-hold warm and cold select the same move");
                check(fixture.engine.arena_size() == cold.engine.arena_size(),
                    "locked-hold warm and cold match node counts");
                bool locked_parity =
                    fixture.engine.arena_size() == cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && locked_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        locked_parity = false;
                    }
                }
                check(locked_parity,
                    "locked-hold warm and cold share full node semantics");
                check_child_links(fixture.engine,
                    "locked-hold child chains enumerate ownership");
            }
        }
        {
            Fixture fixture = make_fixture();
            auto first = engine_alias::parse_queue("TIS");
            check(first.has_value(), "multi-turn first queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*first),
                no_hold) != engine_alias::no_node,
                "multi-turn first turn takes");
            check(fixture.engine.run(2000), "multi-turn first search completes");
            auto first_selection = fixture.engine.select_best();
            check(first_selection.has_value(), "multi-turn first search selects");
            if (!first_selection.has_value())
            {
                return;
            }
            auto const *first_child =
                fixture.engine.node(first_selection->root_child);
            engine_alias::Queue second_text = remaining_queue(fixture.engine.queue(),
                first_child->cursor);
            engine_alias::Queue second_cold_text = second_text;
            toj_policy::State second_policy = first_child->policy;
            engine_alias::Board second_board = first_child->board;
            engine_alias::HoldState second_hold = first_child->hold;
            check(fixture.engine.set_root(second_board, second_policy,
                std::move(second_text), second_hold) != engine_alias::no_node,
                "multi-turn second turn reroots");
            check(fixture.engine.arena_size() > 1,
                "the second turn retains a subtree");
            check(fixture.engine.run(2000), "multi-turn second search completes");
            auto second_selection = fixture.engine.select_best();
            check(second_selection.has_value(), "multi-turn second search selects");
            Fixture second_cold = make_fixture();
            check(second_cold.engine.set_root(second_board, second_policy,
                std::move(second_cold_text), second_hold) != engine_alias::no_node,
                "multi-turn cold second turn takes");
            check(second_cold.engine.run(2000), "multi-turn cold search completes");
            auto second_cold_selection = second_cold.engine.select_best();
            check(second_selection.has_value() && second_cold_selection.has_value()
                && second_selection->root_child == second_cold_selection->root_child
                && second_selection->evidence == second_cold_selection->evidence,
                "second-turn warm and cold searches select the same move");
            check(fixture.engine.arena_size() == second_cold.engine.arena_size(),
                "second-turn warm and cold materialize the same node count");
            bool second_parity = fixture.engine.arena_size()
                == second_cold.engine.arena_size();
            for (std::size_t id = 0;
                id < fixture.engine.arena_size() && second_parity; ++id)
            {
                auto const *warm_node = fixture.engine.node(
                    static_cast<engine_alias::NodeId>(id));
                auto const *cold_node = second_cold.engine.node(
                    static_cast<engine_alias::NodeId>(id));
                if (!same_node_print(warm_node, cold_node, id == 0))
                {
                    second_parity = false;
                }
            }
            check(second_parity,
                "second-turn warm and cold share full node semantics");
            if (!second_selection.has_value())
            {
                return;
            }
            auto const *second_child =
                fixture.engine.node(second_selection->root_child);
            engine_alias::Queue third_text = remaining_queue(fixture.engine.queue(),
                second_child->cursor);
            toj_policy::State third_policy = second_child->policy;
            engine_alias::Board third_board = second_child->board;
            engine_alias::HoldState third_hold = second_child->hold;
            if (third_text.pieces.empty())
            {
                std::size_t const live_arena = fixture.engine.arena_size();
                check(fixture.engine.set_root(third_board, third_policy,
                    std::move(third_text), third_hold) == engine_alias::no_node,
                    "an exhausted third turn is rejected");
                check(fixture.engine.arena_size() == live_arena,
                    "the rejected third turn keeps the live tree");
            }
            else
            {
                engine_alias::Queue third_cold_text = third_text;
                check(fixture.engine.set_root(third_board, third_policy,
                    std::move(third_text), third_hold) != engine_alias::no_node,
                    "multi-turn third turn reroots");
                check(fixture.engine.arena_size() > 1,
                    "the third turn retains a subtree");
                check(fixture.engine.run(2000), "multi-turn third search completes");
                auto third_selection = fixture.engine.select_best();
                check(third_selection.has_value(),
                    "multi-turn third search selects");
                Fixture third_cold = make_fixture();
                check(third_cold.engine.set_root(third_board, third_policy,
                    std::move(third_cold_text), third_hold) != engine_alias::no_node,
                    "multi-turn cold third turn takes");
                check(third_cold.engine.run(2000),
                    "multi-turn third cold completes");
                auto third_cold_selection = third_cold.engine.select_best();
                check(third_selection.has_value()
                    && third_cold_selection.has_value()
                    && third_selection->root_child
                        == third_cold_selection->root_child
                    && third_selection->evidence == third_cold_selection->evidence,
                    "third-turn warm and cold searches select the same move");
                check(fixture.engine.arena_size() == third_cold.engine.arena_size(),
                    "third-turn warm and cold materialize the same node count");
                bool third_parity = fixture.engine.arena_size()
                    == third_cold.engine.arena_size();
                for (std::size_t id = 0;
                    id < fixture.engine.arena_size() && third_parity; ++id)
                {
                    auto const *warm_node = fixture.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    auto const *cold_node = third_cold.engine.node(
                        static_cast<engine_alias::NodeId>(id));
                    if (!same_node_print(warm_node, cold_node, id == 0))
                    {
                        third_parity = false;
                    }
                }
                check(third_parity,
                    "third-turn warm and cold share full node semantics");
                check_child_links(fixture.engine,
                    "third-turn child chains enumerate ownership");
            }
        }
        {
            Fixture source = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "move-reuse queue parses");
            check(source.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "move-reuse first turn takes");
            source.engine.run(2);
            engine_alias::NodeId target = engine_alias::no_node;
            for (std::size_t id = 1; id < source.engine.arena_size(); ++id)
            {
                auto const *node = source.engine.node(
                    static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->child_count > 0)
                {
                    target = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(target != engine_alias::no_node,
                "the move-reuse population expands a root child");
            if (target == engine_alias::no_node)
            {
                return;
            }
            auto const *child = source.engine.node(target);
            engine_alias::Queue next = remaining_queue(source.engine.queue(),
                child->cursor);
            toj_policy::State next_policy = child->policy;
            engine_alias::Board next_board = child->board;
            engine_alias::HoldState next_hold = child->hold;
            std::size_t pre_move_idmap = source.engine.idmap_reserved_bytes();
            check(pre_move_idmap > 0, "the source holds idmap scratch");
            engine_alias::Engine moved(std::move(source.engine));
            check(moved.idmap_reserved_bytes() == pre_move_idmap,
                "the move transfers idmap scratch");
            check(moved.set_root(next_board, next_policy, std::move(next),
                next_hold) != engine_alias::no_node,
                "the moved engine reroots");
            check(moved.arena_size() > 1,
                "the moved engine retains the subtree");
            check(moved.idmap_reserved_bytes() == pre_move_idmap,
                "the matching reroot allocates no new scratch");
            check(source.engine.init(source.engine_config),
                "the source reinitializes after the move");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "rejection-probe queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "rejection-probe root takes");
            fixture.engine.run(2);
            engine_alias::NodeId target = engine_alias::no_node;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node = fixture.engine.node(
                    static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->child_count > 0)
                {
                    target = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(target != engine_alias::no_node,
                "the rejection probe expands a root child");
            if (target == engine_alias::no_node)
            {
                return;
            }
            std::size_t const live_arena = fixture.engine.arena_size();
            engine_alias::Queue empty;
            check(fixture.engine.set_root(shelf_board(), state, std::move(empty),
                no_hold) == engine_alias::no_node,
                "an empty queue is rejected");
            check(fixture.engine.arena_size() == live_arena,
                "a rejected root keeps the live tree");
            std::array<std::uint16_t, 48> rows = {};
            rows[0] = tetris::Board::row_mask;
            check(fixture.engine.set_root(tetris::Board::from_rows(rows), state,
                *engine_alias::parse_queue("TIS"), no_hold) == engine_alias::no_node,
                "a full-row board is rejected");
            check(fixture.engine.arena_size() == live_arena,
                "a rejected board keeps the live tree");
            auto const *child = fixture.engine.node(target);
            engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                child->cursor);
            check(fixture.engine.set_root(child->board, child->policy,
                std::move(next), child->hold) != engine_alias::no_node,
                "the live tree still reuses after rejections");
            check(fixture.engine.arena_size() > 1,
                "reuse survives rejected inputs");
        }
        {
            Fixture fixture = make_fixture(37);
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "partial-link queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "partial-link root takes");
            fixture.engine.run(2);
            check(fixture.engine.arena_exhausted(),
                "the small arena exhausts mid-batch");
            check(fixture.engine.arena_size() == 37,
                "the exhausted arena fills its capacity");
            auto const *target = fixture.engine.node(1);
            check(target != nullptr && target->depth == 1,
                "the partial-link target is a root child");
            engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                target->cursor);
            check(fixture.engine.set_root(target->board, target->policy,
                std::move(next), target->hold) != engine_alias::no_node,
                "the partial-link position reroots");
            check(fixture.engine.arena_size() == 2,
                "the partial reroot retains root and child");
            check_child_links(fixture.engine,
                "partial reroot chains enumerate ownership");
            fixture.engine.run(1);
            check(fixture.engine.arena_exhausted(),
                "the resumed search exhausts again");
            check_child_links(fixture.engine,
                "resumed child chains enumerate ownership");
            auto const *root = fixture.engine.node(0);
            std::size_t owned = 0;
            for (std::size_t id = 0; id < fixture.engine.arena_size(); ++id)
            {
                if (fixture.engine.node(static_cast<engine_alias::NodeId>(id))->parent
                    == 0)
                {
                    ++owned;
                }
            }
            check(owned == root->child_count,
                "the resumed root counts every owned child");
            bool keeps_first = false;
            for (engine_alias::NodeId id = root->first_child;
                id != engine_alias::no_node;
                id = fixture.engine.node(id)->next_sibling)
            {
                if (id == 1)
                {
                    keeps_first = true;
                }
            }
            check(keeps_first,
                "the resumed root keeps its retained first child");
        }
        {
            Fixture fixture = make_fixture(64);
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "promotion-link queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "promotion-link root takes");
            fixture.engine.run(2);
            engine_alias::NodeId target = engine_alias::no_node;
            for (std::size_t id = 1; id < fixture.engine.arena_size(); ++id)
            {
                auto const *node = fixture.engine.node(
                    static_cast<engine_alias::NodeId>(id));
                if (node->depth == 1 && node->child_count > 0)
                {
                    target = static_cast<engine_alias::NodeId>(id);
                    break;
                }
            }
            check(target != engine_alias::no_node,
                "the promotion-link population expands a root child");
            if (target != engine_alias::no_node)
            {
                auto const *child = fixture.engine.node(target);
                engine_alias::Queue next = remaining_queue(fixture.engine.queue(),
                    child->cursor);
                check(fixture.engine.set_root(child->board, child->policy,
                    std::move(next), child->hold) != engine_alias::no_node,
                    "the promotion-link position reroots");
                check(fixture.engine.arena_size() > 1,
                    "the promotion-link reroot retains a subtree");
                fixture.engine.run(3);
                check(fixture.engine.arena_exhausted()
                    || fixture.engine.search_complete(),
                    "the resumed promotion search stops cleanly");
                check_child_links(fixture.engine,
                    "promotion child chains enumerate ownership");
            }
        }
        std::println("root reuse: retained subtrees, identity negatives, advancement");
    }

    void run_finalize_tests()
    {
        toj_policy::State state;
        engine_alias::HoldState no_hold;
        no_hold.locked = true;
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "finalize queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "finalize root takes");
            check(fixture.engine.path_telemetry().calls == 0,
                "search performs no pathfinder calls");
            check(fixture.engine.run(2000), "finalize search completes");
            check(fixture.engine.path_telemetry().calls == 0,
                "search and stats leave path telemetry alone");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "finalize search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            auto const *evidence = fixture.engine.node(selection->evidence);
            check(evidence->depth > 1,
                "the finalized selection carries deeper evidence");
            auto before_stats = fixture.engine.search_stats();
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            auto after_stats = fixture.engine.search_stats();
            check(after_stats.widening_passes == before_stats.widening_passes
                && after_stats.expanded_parents == before_stats.expanded_parents
                && after_stats.materialized_nodes == before_stats.materialized_nodes
                && after_stats.eval_computed == before_stats.eval_computed,
                "finalization leaves search counters alone");
            check(result.has_selection && result.path_ok,
                "the immediate result materializes a path");
            check(result.candidate.has_value()
                && result.candidate->placement == child->incoming.placement
                && result.candidate->arrival == child->incoming.arrival,
                "the result candidate matches the root child");
            check(result.played == child->played,
                "the result piece matches the root child");
            check(same_state(result.state, child->policy),
                "the result state matches the root child");
            check(!result.used_hold,
                "the current-piece result uses no hold");
            check(result.states_expanded > 0 && result.elapsed_nanos >= 0,
                "the result carries path telemetry");
            check(fixture.engine.path_telemetry().calls == 1,
                "one finalization performs one path search");
            check(fixture.engine.path_telemetry().failures == 0,
                "the searching finalization records no failure");
            check(fixture.engine.path_telemetry().states_expanded
                == result.states_expanded,
                "engine telemetry accumulates the path search");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the immediate path replays exactly");
            engine_alias::FinalResult again =
                fixture.engine.finalize(canonical_spawn());
            check(again.path_ok
                && again.path.view() == result.path.view(),
                "finalization output is deterministic");
            check(fixture.engine.path_telemetry().calls == 2,
                "each finalization request searches once");
            tetris::Placement shifted =
                tetris::Placement::unchecked(2, 20, 0);
            check(tetris::toj::fits(child->played, shifted,
                fixture.engine.node(0)->board),
                "the shifted start fits the root board");
            engine_alias::FinalResult moved_start =
                fixture.engine.finalize(shifted);
            check(moved_start.path_ok,
                "a non-spawn start materializes a path");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, shifted, child->incoming,
                moved_start.path.view(), true, child->expandable,
                "the shifted path replays exactly");
            check(child->incoming.arrival == tetris::ArrivalClass::Normal,
                "the shelf selection arrives normally");
            engine_alias::FinalResult zero_move =
                fixture.engine.finalize(child->incoming.placement);
            check(zero_move.path_ok && zero_move.path.size == 0,
                "a settled start yields a valid zero-movement path");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, child->incoming.placement, child->incoming,
                zero_move.path.view(), true, child->expandable,
                "the zero-movement path replays exactly");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "pocket queue parses");
            check(fixture.engine.set_root(pocket_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "pocket root takes");
            check(fixture.engine.run(2000), "pocket search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "pocket search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            tetris::Placement pocket_start =
                tetris::Placement::unchecked(5, 29, 0);
            check(tetris::toj::fits(child->played, pocket_start,
                fixture.engine.node(0)->board),
                "the pocket start fits the root board");
            engine_alias::FinalResult trapped =
                fixture.engine.finalize(pocket_start);
            check(trapped.has_selection && !trapped.path_ok,
                "an unreachable target fails explicitly");
            check(!trapped.path.valid && trapped.path.size == 0,
                "failure carries no stale commands");
            check(fixture.engine.path_telemetry().calls == 1
                && fixture.engine.path_telemetry().failures == 1,
                "the failed search counts once with one failure");
            engine_alias::FinalResult from_spawn =
                fixture.engine.finalize(canonical_spawn());
            check(from_spawn.path_ok,
                "the same selection paths from spawn");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                from_spawn.path.view(), true, child->expandable,
                "the spawn path replays exactly");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            toj_policy::State swap_state;
            engine_alias::HoldState hold;
            hold.piece = tetris::Piece::I;
            hold.locked = false;
            check(fixture.engine.set_root(shelf_board(), swap_state,
                std::move(*queue), hold) != engine_alias::no_node,
                "swap root takes");
            fixture.engine.run(2000);
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "swap search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            check(child->source == engine_alias::BranchSource::Hold,
                "the swap selection uses hold");
            check(!tetris::toj::fits(child->played, buried_start(),
                fixture.engine.node(0)->board),
                "the buried start cannot work if misused");
            engine_alias::FinalResult result =
                fixture.engine.finalize(buried_start());
            check(result.has_selection && result.path_ok && result.used_hold,
                "the occupied-hold result keeps the hold operation");
            check(result.played == child->played,
                "the swap result plays the held piece");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the swap path replays exactly from spawn");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TTI");
            toj_policy::State empty_state;
            engine_alias::HoldState empty_hold;
            check(fixture.engine.set_root(lip_board(), empty_state,
                std::move(*queue), empty_hold) != engine_alias::no_node,
                "equal-piece root takes");
            check(fixture.engine.run(2000), "equal-piece search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "equal-piece search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            check(child->source == engine_alias::BranchSource::Hold
                && child->cursor == 2,
                "the equal-piece selection consumes two pieces");
            check(child->hold.piece.has_value()
                && *child->hold.piece == child->played,
                "the hold piece equals the played piece");
            check(!tetris::toj::fits(child->played, buried_start(),
                fixture.engine.node(0)->board),
                "the buried start cannot work if misused");
            engine_alias::FinalResult result =
                fixture.engine.finalize(buried_start());
            check(result.has_selection && result.path_ok && result.used_hold,
                "equal pieces keep the hold operation");
            check(result.candidate.has_value()
                && result.candidate->arrival
                    == tetris::ArrivalClass::TerminalRotation,
                "the equal-piece selection arrives terminally");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the equal-piece path replays exactly from spawn");
            auto open = tetris::path::replay_path(fixture.engine.node(0)->board,
                child->played, canonical_spawn(), result.path.view(),
                tetris::path::PathConfig{}, false);
            check(open.valid
                && open.arrival == tetris::ArrivalClass::TerminalRotation,
                "the equal-piece path travels the terminal channel");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            toj_policy::State distinct_state;
            engine_alias::HoldState empty_hold;
            check(fixture.engine.set_root(lip_board(), distinct_state,
                std::move(*queue), empty_hold) != engine_alias::no_node,
                "distinct-piece root takes");
            check(fixture.engine.run(2000), "distinct-piece search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "distinct-piece search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            check(child->source == engine_alias::BranchSource::Hold
                && child->cursor == 2,
                "the distinct-piece selection consumes two pieces");
            check(child->played == fixture.engine.queue().pieces[1],
                "the empty-hold path plays the next concrete piece");
            check(child->hold.piece.has_value()
                && *child->hold.piece == fixture.engine.queue().pieces[0]
                && *child->hold.piece != child->played,
                "the hold slot keeps the distinct current piece");
            check(!tetris::toj::fits(child->played, buried_start(),
                fixture.engine.node(0)->board),
                "the buried start cannot work if misused");
            engine_alias::FinalResult result =
                fixture.engine.finalize(buried_start());
            check(result.has_selection && result.path_ok && result.used_hold,
                "the distinct-piece result keeps the hold operation");
            check(result.played == child->played,
                "the distinct-piece result plays the next piece");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the distinct-piece path replays exactly from spawn");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TST");
            check(queue.has_value(), "spin queue parses");
            check(fixture.engine.set_root(lip_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "spin root takes");
            check(fixture.engine.run(2000), "spin search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "spin search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            check(child->incoming.arrival
                == tetris::ArrivalClass::TerminalRotation,
                "the spin selection arrives terminally");
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(result.has_selection && result.path_ok,
                "the spin result materializes a path");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the spin path replays exactly");
            auto open = tetris::path::replay_path(fixture.engine.node(0)->board,
                child->played, canonical_spawn(), result.path.view(),
                tetris::path::PathConfig{}, false);
            check(open.valid
                && open.arrival == tetris::ArrivalClass::TerminalRotation,
                "the spin path travels the terminal channel");
        }
        {
            for (bool allow_180 : {true, false})
            {
                Fixture fixture = make_fixture();
                fixture.engine_config.movement.allow_180 = allow_180;
                check(fixture.engine.init(fixture.engine_config),
                    "spin-180 fixture initializes");
                auto queue = engine_alias::parse_queue("STS");
                check(queue.has_value(), "spin-180 queue parses");
                check(fixture.engine.set_root(lip_board(), state,
                    std::move(*queue), no_hold) != engine_alias::no_node,
                    "spin-180 root takes");
                check(fixture.engine.run(2000),
                    "spin-180 search completes");
                auto selection = fixture.engine.select_best();
                check(selection.has_value(), "spin-180 search selects");
                auto const *child = fixture.engine.node(selection->root_child);
                check(child->incoming.placement.rotation() == 1
                    && child->incoming.placement.x() == 9
                    && child->incoming.placement.y() == 20,
                    "both configurations select the same placement");
                engine_alias::FinalResult result =
                    fixture.engine.finalize(canonical_spawn());
                check(result.has_selection && result.path_ok,
                    "the spin-180 result materializes a path");
                bool uses_spin = false;
                for (char command : result.path.view())
                {
                    if (command == 'x')
                    {
                        uses_spin = true;
                    }
                }
                check(uses_spin == allow_180,
                    "the pathfinder follows the configured 180 rule");
                check_final_replay(fixture.engine.node(0)->board, child->board,
                    child->played, canonical_spawn(), child->incoming,
                    result.path.view(), allow_180, child->expandable,
                    "the spin-180 path replays exactly");
            }
        }
        {
            Fixture fixture = make_fixture();
            fixture.engine_config.movement.allow_180 = false;
            check(fixture.engine.init(fixture.engine_config),
                "180-off fixture initializes");
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "180-off queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "180-off root takes");
            check(fixture.engine.run(2000), "180-off search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "180-off search selects");
            auto const *child = fixture.engine.node(selection->root_child);
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(result.has_selection && result.path_ok,
                "the 180-off result materializes a path");
            bool spin_free = true;
            for (char command : result.path.view())
            {
                if (command == 'x')
                {
                    spin_free = false;
                }
            }
            check(spin_free, "the 180-off path uses no 180 rotation");
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), false, child->expandable,
                "the 180-off path replays exactly");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "lifecycle queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "lifecycle root takes");
            check(fixture.engine.run(2000), "lifecycle search completes");
            auto selection = fixture.engine.select_best();
            check(selection.has_value(), "lifecycle search selects");
            engine_alias::FinalResult first =
                fixture.engine.finalize(canonical_spawn());
            check(first.has_selection && first.path_ok,
                "the first turn finalizes before reroot");
            auto const *child = fixture.engine.node(selection->root_child);
            engine_alias::Queue next =
                remaining_queue(fixture.engine.queue(), child->cursor);
            check(fixture.engine.set_root(child->board, child->policy,
                std::move(next), child->hold) != engine_alias::no_node,
                "lifecycle second turn reroots");
            check(fixture.engine.arena_size() > 1,
                "the lifecycle reroot retains a subtree");
            check(fixture.engine.path_telemetry().calls == 1,
                "reroot preserves path telemetry");
            check(fixture.engine.run(2000), "lifecycle second search completes");
            engine_alias::FinalResult warm =
                fixture.engine.finalize(canonical_spawn());
            check(warm.has_selection && warm.path_ok,
                "finalization follows successful reuse");
            check(fixture.engine.path_telemetry().calls == 2,
                "telemetry accumulates across root changes");
            auto const *warm_child = fixture.engine.node(
                fixture.engine.select_best()->root_child);
            check_final_replay(fixture.engine.node(0)->board, warm_child->board,
                warm_child->played, canonical_spawn(), warm_child->incoming,
                warm.path.view(), true, warm_child->expandable,
                "the post-reuse path replays exactly");
            int blocked_x = -1;
            int blocked_y = 0;
            for (int y = 0; y < 48 && blocked_x < 0; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    if ((fixture.engine.node(0)->board.row(y)
                        >> static_cast<unsigned>(x))
                        & 1u)
                    {
                        blocked_x = x;
                        blocked_y = y;
                        break;
                    }
                }
            }
            check(blocked_x >= 0, "the searched board stays nonempty");
            engine_alias::FinalResult failed = fixture.engine.finalize(
                tetris::Placement::unchecked(blocked_x, blocked_y, 0));
            check(failed.has_selection && !failed.path_ok,
                "a bad start fails after success");
            check(!failed.path.valid && failed.path.size == 0,
                "the failed path carries no stale commands");
            check(failed.candidate.has_value()
                && failed.candidate->placement == warm_child->incoming.placement
                && failed.candidate->arrival == warm_child->incoming.arrival
                && failed.played == warm_child->played
                && same_state(failed.state, warm_child->policy)
                && failed.used_hold
                    == (warm_child->source == engine_alias::BranchSource::Hold),
                "the failed result preserves selected metadata");
            check(fixture.engine.path_telemetry().calls == 3
                && fixture.engine.path_telemetry().failures == 1,
                "success-then-failure counts both outcomes");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "miss queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "miss root takes");
            check(fixture.engine.run(2000), "miss search completes");
            std::array<std::uint16_t, 48> rows = {};
            for (int y = 0; y < 48; ++y)
            {
                rows[static_cast<std::size_t>(y)] =
                    fixture.engine.node(0)->board.row(y);
            }
            rows[40] = rows[40] != 0
                ? static_cast<std::uint16_t>(rows[40] & (rows[40] - 1))
                : static_cast<std::uint16_t>(0x001);
            auto moved_queue = engine_alias::parse_queue("TIS");
            check(moved_queue.has_value(), "miss second queue parses");
            check(fixture.engine.set_root(tetris::Board::from_rows(rows), state,
                std::move(*moved_queue), no_hold) != engine_alias::no_node,
                "the miss takes a clean root");
            check(fixture.engine.arena_size() == 1,
                "the miss retains nothing");
            check(fixture.engine.run(2000), "miss second search completes");
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(result.has_selection && result.path_ok,
                "finalization follows a reuse miss");
            auto selection = fixture.engine.select_best();
            auto const *child = fixture.engine.node(selection->root_child);
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the post-miss path replays against the new root");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "partial queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "partial root takes");
            fixture.engine.run(2);
            check(!fixture.engine.search_complete(),
                "the partial search stays incomplete");
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(result.has_selection && result.path_ok,
                "finalization follows a partial search");
            auto selection = fixture.engine.select_best();
            auto const *child = fixture.engine.node(selection->root_child);
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the partial path replays exactly");
        }
        {
            Fixture fixture = make_fixture(37);
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "exhausted queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "exhausted root takes");
            fixture.engine.run(2);
            check(fixture.engine.arena_exhausted(),
                "the small arena exhausts");
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(result.has_selection && result.path_ok,
                "finalization succeeds after exhaustion");
            check(fixture.engine.path_telemetry().calls == 1,
                "the exhausted finalization searches once");
            auto selection = fixture.engine.select_best();
            auto const *child = fixture.engine.node(selection->root_child);
            check_final_replay(fixture.engine.node(0)->board, child->board,
                child->played, canonical_spawn(), child->incoming,
                result.path.view(), true, child->expandable,
                "the exhausted path replays exactly");
        }
        {
            Fixture source = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "move queue parses");
            check(source.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "move root takes");
            check(source.engine.run(2000), "move search completes");
            engine_alias::FinalResult before =
                source.engine.finalize(canonical_spawn());
            check(before.path_ok, "the source finalizes before moving");
            engine_alias::Engine moved(std::move(source.engine));
            check(moved.path_telemetry().calls == 1,
                "the move transfers path telemetry");
            engine_alias::FinalResult after =
                moved.finalize(canonical_spawn());
            check(after.path_ok
                && after.path.view() == before.path.view(),
                "the moved engine finalizes identically");
            check(source.engine.init(source.engine_config),
                "the source reinitializes after the move");
            engine_alias::FinalResult cleared =
                source.engine.finalize(canonical_spawn());
            check(!cleared.has_selection
                && source.engine.path_telemetry().calls == 0,
                "reinitialization clears selection and telemetry");
        }
        {
            Fixture fixture = make_fixture(0);
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "zero-capacity queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) == engine_alias::no_node,
                "zero capacity takes no root");
            engine_alias::FinalResult result =
                fixture.engine.finalize(canonical_spawn());
            check(!result.has_selection && !result.path_ok,
                "an empty engine finalizes without selection");
            check(fixture.engine.path_telemetry().calls == 0,
                "an empty finalization performs no path search");
        }
        {
            Fixture fixture = make_fixture();
            auto queue = engine_alias::parse_queue("TIS");
            check(queue.has_value(), "invalid-start queue parses");
            check(fixture.engine.set_root(shelf_board(), state, std::move(*queue),
                no_hold) != engine_alias::no_node,
                "invalid-start root takes");
            check(fixture.engine.run(2000), "invalid-start search completes");
            tetris::Placement buried =
                tetris::Placement::unchecked(4, 10, 0);
            engine_alias::FinalResult result =
                fixture.engine.finalize(buried);
            check(result.has_selection && !result.path_ok,
                "an unfitting start fails explicitly");
            check(!result.path.valid && result.path.size == 0,
                "an unfitting failure carries no stale commands");
            check(!tetris::Placement::try_make(10, 47, 0).has_value()
                && !tetris::Placement::try_make(4, 48, 0).has_value()
                && !tetris::Placement::try_make(4, 20, 4).has_value(),
                "out-of-range starts are unrepresentable");
            engine_alias::FinalResult ranged =
                fixture.engine.finalize(tetris::Placement::unchecked(9, 47, 3));
            check(ranged.has_selection,
                "the sky-corner start finalizes a searched selection");
            check(fixture.engine.path_telemetry().calls == 2
                && fixture.engine.path_telemetry().failures == 1,
                "searches count once with only real failures");
            check(fixture.engine.retained_bytes()
                <= engine_alias::engine_memory_budget,
                "finalize flows stay within the budget");
            check(3 * sizeof(tetris::path::Pathfinder) + sizeof(tetris::path::Path)
                <= engine_alias::engine_stack_peak_allowance,
                "the structural pathfinder sizes fit the stack allowance");
        }
        std::println("finalize: selected result with exact replayed path");
    }

    void run_budget_tests()
    {
        std::size_t capacity = engine_alias::default_arena_capacity;
        std::size_t expect =
            static_cast<std::size_t>((engine_alias::engine_memory_budget
                - engine_alias::engine_fixed_workspace)
                / (sizeof(engine_alias::Node) + sizeof(engine_alias::NodeId)));
        check(capacity == expect && capacity > 1024, "arena capacity derives from the budget");
        check(capacity < engine_alias::max_nodes, "capacity stays in NodeId range");
        check(engine_alias::max_queue_length == 256, "queue bound is declared");
        std::println("node storage: {} bytes per node", sizeof(engine_alias::Node));
        std::println("budget: capacity derives from measured node cost");
    }

    void run_epoch_isolation_tests()
    {
        Fixture first = make_zero_fixture();
        check(first.engine.transposition_epoch_for_test() == 1,
            "epoch isolation starts at the reserved never-current successor");
        engine_alias::NodeId root =
            make_root(first, shelf_board(), "III", std::nullopt, true);
        check(root != engine_alias::no_node, "epoch isolation root takes");
        std::uint32_t const epoch_after_first_root =
            first.engine.transposition_epoch_for_test();
        check(epoch_after_first_root == 2, "first cold root advances the epoch once");
        check(first.engine.run(500), "epoch isolation first search completes");
        std::size_t const merges_first =
            first.engine.search_stats().transposition_merges;
        std::size_t const used_first = first.engine.transposition_used();
        check(merges_first > 0, "epoch isolation population merges within one root");
        check(used_first > 0, "epoch isolation population occupies slots");
        auto first_selection = first.engine.select_best();
        check(first_selection.has_value(), "epoch isolation first search selects");
        engine_alias::NodeId second =
            make_root(first, shelf_board(), "III", std::nullopt, true);
        check(second != engine_alias::no_node,
            "identical re-root takes a clean cold root");
        check(first.engine.arena_size() == 1,
            "identical parameters miss reuse and retain nothing");
        check(first.engine.transposition_used() == 0,
            "cold boundary clears the logical transposition occupancy");
        check(first.engine.transposition_epoch_for_test() == epoch_after_first_root + 1,
            "cold boundary advances the epoch exactly once");
        check(first.engine.run(500), "epoch isolation second search completes");
        std::size_t const merges_second =
            first.engine.search_stats().transposition_merges;
        std::size_t const used_second = first.engine.transposition_used();
        check(merges_second > 0, "second root merges within its own epoch");
        auto second_selection = first.engine.select_best();
        check(second_selection.has_value(), "epoch isolation second search selects");
        Fixture baseline = make_zero_fixture();
        make_root(baseline, shelf_board(), "III", std::nullopt, true);
        check(baseline.engine.run(500), "epoch isolation baseline completes");
        check(merges_second
                == baseline.engine.search_stats().transposition_merges,
            "identical key across a set_root boundary produces zero cross-root merges");
        check(used_second == baseline.engine.transposition_used(),
            "identical re-root materializes exactly the fresh slot count");
        auto base_selection = baseline.engine.select_best();
        check(second_selection.has_value() && base_selection.has_value()
            && second_selection->root_child == base_selection->root_child
            && second_selection->evidence == base_selection->evidence,
            "identical re-root selects exactly like a fresh engine");
        std::println("epoch isolation: identical keys never merge across roots");
    }

    void run_epoch_wrap_tests()
    {
        constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
        Fixture fresh = make_zero_fixture();
        check(fresh.engine.transposition_epoch_for_test() == 1,
            "wrap fixture starts at epoch one");
        check(fresh.engine.transposition_physical_entries_for_test() == 0,
            "fresh table holds no physical stamps");
        Fixture staged = make_zero_fixture();
        engine_alias::NodeId root =
            make_root(staged, shelf_board(), "III", std::nullopt, true);
        check(root != engine_alias::no_node, "wrap population root takes");
        check(staged.engine.transposition_epoch_for_test() == 2,
            "wrap population root advances to epoch two");
        staged.engine.set_transposition_epoch_for_test(kMax);
        check(staged.engine.transposition_epoch_for_test() == kMax,
            "wrap population forced to the maximum stamp");
        check(staged.engine.run(500), "wrap population completes at maximum epoch");
        check(staged.engine.transposition_used() > 0,
            "maximum-epoch search occupies slots");
        check(staged.engine.transposition_physical_entries_for_test() > 0,
            "maximum-epoch search stamps physical entries");
        engine_alias::NodeId wrapped =
            make_root(staged, shelf_board(), "III", std::nullopt, true);
        check(wrapped != engine_alias::no_node, "wrapping cold root takes");
        check(staged.engine.arena_size() == 1, "wrapping root retains nothing");
        check(staged.engine.transposition_epoch_for_test() == 1,
            "wrap restarts at epoch one");
        check(staged.engine.transposition_used() == 0,
            "wrap clears the logical occupancy");
        check(staged.engine.transposition_physical_entries_for_test() == 0,
            "wrap performs the single full clear of physical stamps");
        check(staged.engine.run(500), "post-wrap search completes");
        check(staged.engine.search_stats().transposition_merges > 0,
            "post-wrap search merges normally");
        check(staged.engine.transposition_used() > 0,
            "post-wrap search occupies slots normally");
        Fixture baseline = make_zero_fixture();
        make_root(baseline, shelf_board(), "III", std::nullopt, true);
        check(baseline.engine.run(500), "wrap baseline completes");
        check(staged.engine.search_stats().transposition_merges
                == baseline.engine.search_stats().transposition_merges,
            "post-wrap merge counts match a fresh engine exactly");
        check(staged.engine.transposition_used()
                == baseline.engine.transposition_used(),
            "post-wrap slot counts match a fresh engine exactly");
        check(staged.engine.arena_size() == baseline.engine.arena_size(),
            "post-wrap arena size matches a fresh engine exactly");
        std::size_t const physical_before =
            staged.engine.transposition_physical_entries_for_test();
        check(physical_before > 0, "post-wrap run leaves physical stamps");
        std::uint32_t const epoch_before =
            staged.engine.transposition_epoch_for_test();
        make_root(staged, shelf_board(), "III", std::nullopt, true);
        check(staged.engine.transposition_epoch_for_test() == epoch_before + 1,
            "normal advance steps the epoch once");
        check(staged.engine.transposition_used() == 0,
            "normal advance clears the logical occupancy");
        check(staged.engine.transposition_physical_entries_for_test() == physical_before,
            "normal advance keeps stale physical stamps without clearing");
        std::println("epoch wrap: maximum advances through one clear back to one");
    }

    void run_reroot_epoch_parity_tests()
    {
        toj_policy::State state;
        engine_alias::HoldState no_hold;
        no_hold.locked = true;
        Fixture warm = make_fixture();
        auto first_queue = engine_alias::parse_queue("TIS");
        check(first_queue.has_value(), "reroot-epoch first queue parses");
        check(warm.engine.set_root(shelf_board(), state, std::move(*first_queue),
            no_hold) != engine_alias::no_node,
            "reroot-epoch first turn takes");
        std::uint32_t const epoch_first_root =
            warm.engine.transposition_epoch_for_test();
        check(warm.engine.run(2000), "reroot-epoch first search completes");
        check(warm.engine.transposition_used() > 0,
            "reroot-epoch first search occupies slots");
        auto selection = warm.engine.select_best();
        check(selection.has_value(), "reroot-epoch first search selects");
        if (!selection.has_value())
        {
            return;
        }
        auto const *child = warm.engine.node(selection->root_child);
        engine_alias::Queue next = remaining_queue(warm.engine.queue(), child->cursor);
        engine_alias::Queue cold_next = next;
        toj_policy::State const next_policy = child->policy;
        engine_alias::Board const next_board = child->board;
        engine_alias::HoldState const next_hold = child->hold;
        check(warm.engine.set_root(next_board, next_policy, std::move(next),
            next_hold) != engine_alias::no_node,
            "reroot-epoch second turn reroots");
        check(warm.engine.arena_size() > 2,
            "reroot-epoch reroot retains a nontrivial subtree");
        check(warm.engine.transposition_epoch_for_test() == epoch_first_root + 1,
            "reroot advances the epoch exactly once");
        check(warm.engine.transposition_used() > 0,
            "reroot carries retained transposition entries under the new epoch");
        check(warm.engine.run(2000), "reroot-epoch warm second search completes");
        auto warm_stats = warm.engine.search_stats();
        check(warm_stats.transposition_merges > 0,
            "reroot-epoch warm search merges retained nodes");
        auto warm_selection = warm.engine.select_best();
        Fixture cold = make_fixture();
        check(cold.engine.set_root(next_board, next_policy, std::move(cold_next),
            next_hold) != engine_alias::no_node,
            "reroot-epoch cold second turn takes");
        check(cold.engine.run(2000), "reroot-epoch cold second search completes");
        auto cold_stats = cold.engine.search_stats();
        auto cold_selection = cold.engine.select_best();
        check(warm_selection.has_value() && cold_selection.has_value()
            && warm_selection->root_child == cold_selection->root_child
            && warm_selection->evidence == cold_selection->evidence,
            "reroot-epoch warm and cold select the same move");
        check(warm.engine.arena_size() == cold.engine.arena_size(),
            "reroot-epoch warm and cold materialize the same node count");
        bool parity = warm.engine.arena_size() == cold.engine.arena_size();
        for (std::size_t id = 0; id < warm.engine.arena_size() && parity; ++id)
        {
            auto const *warm_node = warm.engine.node(
                static_cast<engine_alias::NodeId>(id));
            auto const *cold_node = cold.engine.node(
                static_cast<engine_alias::NodeId>(id));
            if (!same_node_print(warm_node, cold_node, id == 0))
            {
                parity = false;
            }
        }
        check(parity, "reroot-epoch warm and cold share full node semantics");
        check(cold_stats.materialized_nodes > 0,
            "reroot-epoch cold search materializes within its own epoch");
        check(warm.engine.transposition_used() > 0
            && cold.engine.transposition_used() > 0,
            "both reroot-epoch searches leave occupied tables");
        check_child_links(warm.engine, "reroot-epoch warm chains enumerate ownership");
        std::println("reroot epoch: warm and cold agree under epoch retention");
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
    run_cache_budget_rejection_tests();
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
    run_move_clock_tests();
    run_pending_heap_tests();
    run_memory_accounting_tests();
    run_marker_boundary_tests();
    run_reinit_tests();
    run_exhaustion_projection_tests();
    run_adapter_order_tests();
    run_cache_unit_tests();
    run_cache_parity_tests();
    run_cache_counter_tests();
    run_reuse_tests();
    run_epoch_isolation_tests();
    run_epoch_wrap_tests();
    run_reroot_epoch_parity_tests();
    run_finalize_tests();
    std::println("tetris_engine_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
