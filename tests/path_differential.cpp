#include "toj_pathfinder.h"
#include "toj_rule.h"
#include "published_srs_replay.h"
#include "reach_corpus.h"
#include "scalar_arrival_oracle.h"

#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"
#include "random.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace tetris::toj;
using namespace tetris::path;
using namespace reachability;

namespace toj_alias = tetris::toj;

namespace
{
    constexpr std::size_t legacy_width = 10;
    constexpr std::size_t legacy_height = 40;
    constexpr std::size_t board_count = 24;
    constexpr std::uint32_t board_seed = 1;
    constexpr char const *piece_order = "TZSJLOI";

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

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

    Engine make_engine()
    {
        Engine engine;
        if (!engine.prepare(legacy_width, legacy_height))
        {
            std::println(stderr, "engine.prepare failed");
            std::exit(1);
        }
        return engine;
    }

    m_tetris::TetrisMap seeded_map(std::size_t board_id)
    {
        std::mt19937 rng(board_seed * 1000003u + static_cast<std::uint32_t>(board_id) * 7919u);
        m_tetris::TetrisMap map(legacy_width, legacy_height);
        std::size_t const roof = 4 + rng() % 17;
        for (std::size_t y = 0; y < roof; ++y)
        {
            map.row[y] = static_cast<std::uint32_t>(rng()) & 0x3ff;
        }
        return map;
    }

    std::array<std::uint16_t, 48> rows_of(m_tetris::TetrisMap const &map)
    {
        std::array<std::uint16_t, 48> rows = {};
        for (std::size_t y = 0; y < legacy_height; ++y)
        {
            rows[y] = static_cast<std::uint16_t>(map.row[y] & 0x3ff);
        }
        return rows;
    }

    Piece piece_of(char name)
    {
        return *tetris::try_from_char(name);
    }

    published_replay::Cells sorted_cells_of(Piece piece, Placement placement)
    {
        auto maybe = toj_alias::cells(piece, placement);
        if (!maybe)
        {
            check(false, "path candidate placement is piece-valid");
            return {};
        }
        return published_replay::sorted_cells(*maybe);
    }

    bool alphabet_clean(std::string_view commands)
    {
        for (char command : commands)
        {
            if (command != 'l' && command != 'r' && command != 'd' && command != 'z' && command != 'c'
                && command != 'x' && command != 'D')
            {
                return false;
            }
        }
        return true;
    }

    void run_directed_interpreter_tests(Engine &engine)
    {
        auto model = published_replay::measure_frames(engine);
        std::array<std::uint16_t, 48> rows = {};
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "d", false, true);
            published_replay::Cells expected{{{3, 19}, {4, 19}, {4, 20}, {5, 19}}};
            check(result.valid && result.cells == expected && result.rotation == 0 && result.arrival == 0,
                "directed soft drop moves the spawn down one row");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "l", false, true);
            published_replay::Cells expected{{{2, 20}, {3, 20}, {3, 21}, {4, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 0 && result.arrival == 0,
                "directed left move shifts the spawn one column");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "c", false, true);
            published_replay::Cells expected{{{4, 19}, {4, 20}, {4, 21}, {5, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 1 && result.arrival == 1,
                "directed clockwise rotation keeps the box and marks the arrival");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 0, 5, 0, "c", false, true);
            published_replay::Cells expected{{{1, 3}, {1, 4}, {1, 5}, {2, 4}}};
            check(result.valid && result.cells == expected && result.rotation == 1 && result.arrival == 1,
                "directed wall rotation keeps the box");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "D", true, true);
            published_replay::Cells expected{{{3, 0}, {4, 0}, {4, 1}, {5, 0}}};
            check(result.valid && result.cells == expected && result.rotation == 0 && result.arrival == 0,
                "directed sonic drop rests on the floor without marking rotation");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "X", true, true);
            check(!result.valid, "directed removed command fails the replay");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "L", false, true);
            published_replay::Cells expected{{{0, 20}, {1, 20}, {1, 21}, {2, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 0 && result.arrival == 0,
                "directed wall command slides to the left wall");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "R", false, true);
            published_replay::Cells expected{{{7, 20}, {8, 20}, {8, 21}, {9, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 0 && result.arrival == 0,
                "directed wall command slides to the right wall");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "cL", false, true);
            published_replay::Cells expected{{{0, 19}, {0, 20}, {0, 21}, {1, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 1 && result.arrival == 0,
                "directed rotation then wall slide resets arrival");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "zR", false, true);
            published_replay::Cells expected{{{8, 20}, {9, 19}, {9, 20}, {9, 21}}};
            check(result.valid && result.cells == expected && result.rotation == 3 && result.arrival == 0,
                "directed counter rotation then wall slide resets arrival");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "x", true, false);
            check(!result.valid, "directed disabled 180 fails the replay");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "x", false, true);
            published_replay::Cells expected{{{3, 20}, {4, 19}, {4, 20}, {5, 20}}};
            check(result.valid && result.cells == expected && result.rotation == 2 && result.arrival == 1,
                "directed identity-kick 180 keeps the box");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "z", false, true);
            published_replay::Cells expected{{{3, 20}, {4, 19}, {4, 20}, {4, 21}}};
            check(result.valid && result.cells == expected && result.rotation == 3 && result.arrival == 1,
                "directed counter-clockwise rotation marks the arrival");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "c", true, true);
            published_replay::Cells expected{{{4, 0}, {4, 1}, {4, 2}, {5, 1}}};
            check(result.valid && result.cells == expected && result.rotation == 1 && result.arrival == 0,
                "directed moving lock resets arrival after clockwise rotation");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "z", true, true);
            published_replay::Cells expected{{{3, 1}, {4, 0}, {4, 1}, {4, 2}}};
            check(result.valid && result.cells == expected && result.rotation == 3 && result.arrival == 0,
                "directed moving lock resets arrival after counter-clockwise rotation");
        }
        {
            auto result = published_replay::replay(model, rows, 'T', 3, 21, 0, "x", true, true);
            published_replay::Cells expected{{{3, 1}, {4, 0}, {4, 1}, {5, 1}}};
            check(result.valid && result.cells == expected && result.rotation == 2 && result.arrival == 0,
                "directed moving lock resets arrival after 180 rotation");
        }
        {
            auto dropped = published_replay::replay(model, rows, 'T', 3, 21, 0, "cD", true, true);
            auto held = published_replay::replay(model, rows, 'T', 3, 21, 0, "cD", false, true);
            check(dropped.valid && held.valid && dropped.cells == held.cells,
                "directed sonic drop before lock does not change the placement");
        }
        std::println("directed interpreter: hand-authored command outcomes replay exactly");
    }

    void run_directed_replay_tests()
    {
        Board empty;
        Placement const spawn = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
        PathConfig on{};
        on.allow_180 = true;
        PathConfig off{};
        off.allow_180 = false;
        check(Path::capacity == 1024, "exported buffer capacity is pinned");
        check(Path::max_payload + 3 == Path::capacity,
            "exported payload leaves room for v, V, and null");
        for (char command : {'X', 'Z', 'C'})
        {
            std::string input(1, command);
            check(!replay_path(empty, Piece::T, spawn, input, on, true).valid,
                std::string("production replay rejects removed command ") + command);
        }
        {
            auto left = replay_path(empty, Piece::T, spawn, "L", on, false);
            check(left.valid && left.placement == Placement::unchecked(1, 20, 0)
                && left.arrival == ArrivalClass::Normal,
                "production replay slides wall commands to the wall");
            auto right = replay_path(empty, Piece::T, spawn, "R", on, false);
            check(right.valid && right.placement == Placement::unchecked(8, 20, 0)
                && right.arrival == ArrivalClass::Normal,
                "production replay slides wall commands to the right wall");
            auto rotated_left = replay_path(empty, Piece::T, spawn, "cL", on, false);
            check(rotated_left.valid && rotated_left.placement == Placement::unchecked(0, 20, 1)
                && rotated_left.arrival == ArrivalClass::Normal,
                "production replay resets arrival after rotation then wall slide");
            auto rotated_right = replay_path(empty, Piece::T, spawn, "zR", on, false);
            check(rotated_right.valid && rotated_right.placement == Placement::unchecked(9, 20, 3)
                && rotated_right.arrival == ArrivalClass::Normal,
                "production replay resets arrival after counter rotation then wall slide");
        }
        check(!replay_path(empty, Piece::T, spawn, "x", off, true).valid,
            "production replay rejects disabled 180");
        {
            auto accepted = replay_path(empty, Piece::T, spawn, "x", on, true);
            check(accepted.valid, "production replay accepts enabled 180");
        }
        {
            std::array<std::uint16_t, 48> rows = {};
            rows[20] |= static_cast<std::uint16_t>(1u << 2);
            Board blocked = Board::from_rows(rows);
            check(!replay_path(blocked, Piece::T, spawn, "l", on, true).valid,
                "production replay rejects blocked translations");
        }
        {
            std::array<std::uint16_t, 48> rows = {};
            rows[19] |= static_cast<std::uint16_t>(1u << 4);
            rows[19] |= static_cast<std::uint16_t>(1u << 3);
            rows[22] |= static_cast<std::uint16_t>(1u << 3);
            rows[17] |= static_cast<std::uint16_t>(1u << 4);
            rows[18] |= static_cast<std::uint16_t>(1u << 3);
            Board walled = Board::from_rows(rows);
            check(!replay_path(walled, Piece::T, spawn, "c", on, false).valid,
                "production replay rejects failed rotations");
        }
        {
            std::array<std::uint16_t, 48> rows = {};
            rows[20] |= static_cast<std::uint16_t>(1u << 4);
            Board covered = Board::from_rows(rows);
            check(!replay_path(covered, Piece::T, spawn, "", on, true).valid,
                "production replay rejects invalid starting placements");
        }
        {
            Placement bad = Placement::unchecked(4, 20, 2);
            check(!replay_path(empty, Piece::O, bad, "", on, true).valid,
                "production replay rejects piece-invalid rotations");
        }
        for (auto [command, locked_cells, locked_arrival] : {
                std::tuple<std::string, published_replay::Cells, ArrivalClass>{"c",
                    {{{4, 0}, {4, 1}, {4, 2}, {5, 1}}}, ArrivalClass::Normal},
                std::tuple<std::string, published_replay::Cells, ArrivalClass>{"z",
                    {{{3, 1}, {4, 0}, {4, 1}, {4, 2}}}, ArrivalClass::Normal},
                std::tuple<std::string, published_replay::Cells, ArrivalClass>{"x",
                    {{{3, 1}, {4, 0}, {4, 1}, {5, 1}}}, ArrivalClass::Normal},
            })
        {
            auto unlocked = replay_path(empty, Piece::T, spawn, command, on, false);
            check(unlocked.valid && unlocked.arrival == ArrivalClass::TerminalRotation,
                "production replay keeps terminal arrival without lock for " + command);
            auto locked = replay_path(empty, Piece::T, spawn, command, on, true);
            auto expected = sorted_cells_of(Piece::T, locked.placement);
            check(locked.valid && locked.arrival == locked_arrival && expected == locked_cells,
                "production replay resets arrival when lock moves the piece for " + command);
        }
        std::println("directed replay: production rejections and lock arrival reset");
    }

    struct PathTallies
    {
        std::size_t candidates = 0;
        std::size_t terminal = 0;
        std::size_t longest = 0;
        std::size_t skipped = 0;
        std::size_t fallbacks = 0;
    };

    void check_piece_paths(Board const &board, std::array<std::uint16_t, 48> const &rows,
        published_replay::Model const &model, Piece piece, char piece_char, bool allow_180,
        std::vector<Candidate> const &listed, PathTallies &tallies)
    {
        PathConfig path_config{};
        path_config.allow_180 = allow_180;
        Placement const spawn_pose =
            Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
        Pathfinder finder(board, piece, spawn_pose, path_config);
        Placement const start = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
        std::array<std::array<std::uint64_t, 8>, 4> oracle_normal{};
        call_with_block<SRS>(piece, [&]<block B>() {
            scalar_arrival::ScalarConfig oracle_config{};
            oracle_config.allow_180 = allow_180;
            oracle_config.allow_softdrop = true;
            oracle_config.allow_sonicdrop = true;
            oracle_config.allow_20g = false;
            auto geometry = scalar_arrival::make_geometry<B>();
            scalar_arrival::ScalarOracle<B> oracle{geometry, oracle_config, rows};
            oracle.run(reachability::coord{toj_alias::spawn_x, toj_alias::spawn_y}, 0);
            auto words = oracle.landable_words(0, true);
            for (int o = 0; o < B.orientations; ++o)
            {
                for (int w = 0; w < 8; ++w)
                {
                    oracle_normal[o][w] = words[o][w];
                }
            }
            return 0;
        });
        for (auto const &candidate : listed)
        {
            ++tallies.candidates;
            if (candidate.arrival == ArrivalClass::TerminalRotation)
            {
                ++tallies.terminal;
            }
            Path path = finder.find(candidate);
            std::string what = std::string("path for ") + piece_char + " arrival "
                + (candidate.arrival == ArrivalClass::TerminalRotation ? "terminal" : "normal")
                + (allow_180 ? " 180 on" : " 180 off");
            check(path.valid, what + " exists");
            if (!path.valid)
            {
                continue;
            }
            check(alphabet_clean(path.view()), what + " uses only shipped commands");
            check(path.size <= Path::max_payload, what + " fits the exported buffer");
            tallies.longest = std::max(tallies.longest, path.size);
            ReplayResult replayed = replay_path(board, piece, start, path.view(), path_config, true);
            ReplayResult open = replay_path(board, piece, start, path.view(), path_config, false);
            bool const arrival_ok = piece == Piece::T
                ? replayed.arrival == candidate.arrival
                : true;
            check(replayed.valid && replayed.placement == candidate.placement && arrival_ok,
                what + " replays exactly");
            check(open.valid, what + " replays without lock");
            auto independent = published_replay::replay(model, rows, piece_char, 3, 21, 0,
                path.view(), true, allow_180);
            auto independent_open = published_replay::replay(model, rows, piece_char, 3, 21, 0,
                path.view(), false, allow_180);
            auto expected_cells = sorted_cells_of(piece, candidate.placement);
            bool const independent_arrival_ok = piece == Piece::T
                ? independent.arrival
                    == (candidate.arrival == ArrivalClass::TerminalRotation ? 1 : 0)
                : true;
            check(independent.valid && independent.cells == expected_cells
                && independent_arrival_ok,
                what + " replays through the independent interpreter");
            check((open.arrival == ArrivalClass::TerminalRotation)
                    == (independent_open.arrival == 1),
                what + " replay layers agree on the pre-lock arrival");
            if (piece != Piece::T && open.arrival == ArrivalClass::TerminalRotation)
            {
                check(!finder.normal_path_exists(candidate),
                    what + " ends with rotation only where no normal path exists");
                int const rotation = candidate.placement.rotation();
                int const x = candidate.placement.x();
                int const y = candidate.placement.y();
                bool const oracle_has_normal = rotation >= 0 && rotation < 4
                    && (oracle_normal[rotation][y / 6]
                        & (std::uint64_t(1) << ((y % 6) * 10 + x))) != 0;
                check(!oracle_has_normal,
                    what + " independent channel oracle agrees no normal landing exists");
                ++tallies.fallbacks;
            }
            if (candidate.arrival == ArrivalClass::TerminalRotation)
            {
                auto unlocked = published_replay::replay(model, rows, piece_char, 3, 21, 0,
                    path.view(), false, allow_180);
                check(unlocked.valid && unlocked.cells == expected_cells,
                    what + " hard drop does not move the terminal placement");
            }
        }
    }

    PathTallies run_corpus_path_tests(Engine &engine, bool allow_180)
    {
        auto model = published_replay::measure_frames(engine);
        PathTallies tallies;
        std::vector<m_tetris::TetrisMap> maps;
        maps.push_back(m_tetris::TetrisMap(legacy_width, legacy_height));
        for (std::size_t b = 0; b < board_count; ++b)
        {
            maps.push_back(seeded_map(b));
        }
        for (auto const &map : maps)
        {
            Board const board = Board::from_rows(rows_of(map));
            auto const rows = rows_of(map);
            for (char const *p = piece_order; *p; ++p)
            {
                Piece const piece = piece_of(*p);
                MovementConfig movement{};
                movement.allow_180 = allow_180;
                auto listed = enumerate_candidates(board, piece, movement);
                check_piece_paths(board, rows, model, piece, *p, allow_180, listed, tallies);
            }
        }
        std::println("corpus paths{}: {} candidates, {} terminal, longest {} commands, {} fallbacks",
            allow_180 ? "" : " 180 off",
            tallies.candidates, tallies.terminal, tallies.longest, tallies.fallbacks);
        return tallies;
    }
    void run_start_placement_tests()
    {
        Board empty;
        PathConfig config{};
        Candidate const target{Placement::unchecked(4, 0, 0), ArrivalClass::Normal};
        {
            Placement const translated = Placement::unchecked(6, 20, 0);
            Pathfinder finder(empty, Piece::T, translated, config);
            Path path = finder.find(target);
            check(path.valid, "translated start reaches the floor candidate");
            if (path.valid)
            {
                auto replayed = replay_path(empty, Piece::T, translated, path.view(), config, true);
                check(replayed.valid && replayed.placement == target.placement
                    && replayed.arrival == ArrivalClass::Normal,
                    "translated start replays from the actual start");
            }
        }
        {
            Placement const rotated = Placement::unchecked(4, 21, 1);
            Pathfinder finder(empty, Piece::T, rotated, config);
            Path path = finder.find(target);
            check(path.valid, "rotated start reaches the floor candidate");
            if (path.valid)
            {
                auto replayed = replay_path(empty, Piece::T, rotated, path.view(), config, true);
                check(replayed.valid && replayed.placement == target.placement
                    && replayed.arrival == ArrivalClass::Normal,
                    "rotated start replays from the actual start");
            }
        }
        {
            std::array<std::uint16_t, 48> rows = {};
            rows[20] |= static_cast<std::uint16_t>(1u << 4);
            Board blocked = Board::from_rows(rows);
            Placement const spawn = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
            Pathfinder finder(blocked, Piece::T, spawn, config);
            check(!finder.find(target).valid, "obstructed start finds no path");
        }
        {
            Placement const spawn = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
            Pathfinder held(empty, Piece::T, spawn, config);
            Path held_path = held.find(target);
            check(held_path.valid, "post-hold spawn search reaches the floor candidate");
            Pathfinder other(empty, Piece::J, spawn, config);
            Candidate const other_target{Placement::unchecked(4, 0, 0), ArrivalClass::Normal};
            Path other_path = other.find(other_target);
            check(other_path.valid, "post-hold search for the held piece starts from its spawn");
            if (other_path.valid)
            {
                auto replayed = replay_path(empty, Piece::J, spawn, other_path.view(), config, true);
                check(replayed.valid && replayed.placement == other_target.placement,
                    "post-hold search for the held piece replays from its spawn");
            }
        }
        std::println("start placements: translated, rotated, obstructed, and post-hold starts");
    }

    void run_terminal_validity_tests()
    {
        Board empty;
        PathConfig config{};
        Placement const spawn = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
        {
            Candidate const floating{Placement::unchecked(1, 1, 0), ArrivalClass::TerminalRotation};
            Pathfinder finder(empty, Piece::T, spawn, config);
            check(!finder.find(floating).valid, "floating terminal candidates receive no path");
        }
        {
            auto listed = enumerate_candidates(empty, Piece::T, MovementConfig{true});
            bool saw_terminal = false;
            Pathfinder finder(empty, Piece::T, spawn, config);
            for (auto const &candidate : listed)
            {
                if (candidate.arrival != ArrivalClass::TerminalRotation)
                {
                    continue;
                }
                saw_terminal = true;
                Path path = finder.find(candidate);
                check(path.valid, "landed terminal candidates receive a path");
                if (path.valid)
                {
                    auto replayed = replay_path(empty, Piece::T, spawn, path.view(), config, true);
                    check(replayed.valid && replayed.placement == candidate.placement
                        && replayed.arrival == ArrivalClass::TerminalRotation,
                        "landed terminal paths replay exactly");
                }
                break;
            }
            check(saw_terminal, "empty board T enumeration holds a terminal candidate");
        }
        std::println("terminal validity: floating terminal rejected, landed terminal accepted");
    }

    void run_selection_integration_tests()
    {
        std::vector<std::pair<Board, Piece>> cases;
        cases.push_back({Board{}, Piece::T});
        cases.push_back({Board::from_rows(rows_of(seeded_map(0))), Piece::J});
        cases.push_back({Board::from_rows(rows_of(seeded_map(7))), Piece::S});
        cases.push_back({Board::from_rows(rows_of(seeded_map(12))), Piece::I});
        for (auto const &[board, piece] : cases)
        {
            auto listed = enumerate_candidates(board, piece, MovementConfig{true});
            check(!listed.empty(), "selection fixture has candidates");
            if (listed.empty())
            {
                continue;
            }
            Candidate selected = listed[0];
            for (auto const &candidate : listed)
            {
                if (candidate.arrival == ArrivalClass::TerminalRotation)
                {
                    selected = candidate;
                    break;
                }
            }
            Path selected_path;
            {
                PathConfig config{};
                Placement const spawn =
                    Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
                Pathfinder finder(board, piece, spawn, config);
                selected_path = finder.find(selected);
                Path again = finder.find(selected);
                check(again.valid == selected_path.valid && again.view() == selected_path.view(),
                    "repeated finds reuse the single search");
            }
            check(selected_path.valid, "selected candidate has a path");
            if (selected_path.valid)
            {
                PathConfig config{};
                Placement const spawn =
                    Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
                auto replayed = replay_path(board, piece, spawn, selected_path.view(), config, true);
                check(replayed.valid && replayed.placement == selected.placement,
                    "selected path replays to the selected placement");
            }
        }
        std::println("selection shape: one finder per selection with deterministic reuse");
    }

    PathTallies run_reach_corpus_path_tests(Engine &engine, bool allow_180)
    {
        auto model = published_replay::measure_frames(engine);
        auto corpus = reach_corpus::make();
        check(corpus.size() == 37, "reach corpus holds 37 boards");
        PathTallies tallies;
        for (auto const &rows : corpus)
        {
            Board const board = Board::from_rows(rows);
            for (char const *p = piece_order; *p; ++p)
            {
                Piece const piece = piece_of(*p);
                MovementConfig movement{};
                movement.allow_180 = allow_180;
                auto listed = enumerate_candidates(board, piece, movement);
                if (listed.empty())
                {
                    check(!toj_alias::can_spawn(board, piece),
                        "empty candidate set only under spawn obstruction");
                    ++tallies.skipped;
                    continue;
                }
                check_piece_paths(board, rows, model, piece, *p, allow_180, listed, tallies);
            }
        }
        std::println("reach corpus paths{}: {} candidates, {} terminal, longest {} commands, {} skipped, {} fallbacks",
            allow_180 ? "" : " 180 off", tallies.candidates, tallies.terminal, tallies.longest,
            tallies.skipped, tallies.fallbacks);
        return tallies;
    }
}

int main()
{
    Engine engine = make_engine();
    run_directed_interpreter_tests(engine);
    run_directed_replay_tests();
    run_start_placement_tests();
    run_terminal_validity_tests();
    run_selection_integration_tests();
    PathTallies on = run_corpus_path_tests(engine, true);
    check(on.candidates == 4980 && on.terminal == 721 && on.longest == 17 && on.fallbacks == 81,
        "seeded corpus candidate totals are pinned");
    PathTallies off = run_corpus_path_tests(engine, false);
    check(off.candidates == 4935 && off.terminal == 721 && off.longest == 13 && off.fallbacks == 62,
        "seeded corpus 180-off candidate totals are pinned");
    PathTallies reach_on = run_reach_corpus_path_tests(engine, true);
    check(reach_on.candidates == 4535 && reach_on.terminal == 640 && reach_on.longest == 24
        && reach_on.skipped == 114 && reach_on.fallbacks == 116,
        "reach corpus candidate totals are pinned");
    PathTallies reach_off = run_reach_corpus_path_tests(engine, false);
    check(reach_off.candidates == 4416 && reach_off.terminal == 640 && reach_off.longest == 26
        && reach_off.skipped == 114 && reach_off.fallbacks == 85,
        "reach corpus 180-off candidate totals are pinned");
    std::println("reach corpus totals: {} candidates 180 on, {} candidates 180 off",
        reach_on.candidates, reach_off.candidates);
    std::println("path_differential: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
