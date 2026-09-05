#include "toj_pathfinder.h"
#include "toj_rule.h"
#include "published_srs_replay.h"

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
            auto dropped = published_replay::replay(model, rows, 'T', 3, 21, 0, "cD", true, true);
            auto held = published_replay::replay(model, rows, 'T', 3, 21, 0, "cD", false, true);
            check(dropped.valid && held.valid && dropped.cells == held.cells,
                "directed sonic drop before lock does not change the placement");
        }
        std::println("directed interpreter: hand-authored command outcomes replay exactly");
    }

    void run_corpus_path_tests(Engine &engine, bool allow_180)
    {
        auto model = published_replay::measure_frames(engine);
        std::size_t candidates = 0;
        std::size_t terminal = 0;
        std::size_t longest = 0;
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
                PathConfig path_config{};
                path_config.allow_180 = allow_180;
                Pathfinder finder(board, piece, path_config);
                Placement const start = Placement::unchecked(toj_alias::spawn_x, toj_alias::spawn_y, 0);
                for (auto const &candidate : listed)
                {
                    ++candidates;
                    if (candidate.arrival == ArrivalClass::TerminalRotation)
                    {
                        ++terminal;
                    }
                    Path path = finder.find(candidate);
                    std::string what = std::string("path for ") + *p + " arrival "
                        + (candidate.arrival == ArrivalClass::TerminalRotation ? "terminal" : "normal")
                        + (allow_180 ? " 180 on" : " 180 off");
                    check(path.valid, what + " exists");
                    if (!path.valid)
                    {
                        continue;
                    }
                    check(alphabet_clean(path.view()), what + " uses only shipped commands");
                    check(path.size + 3 <= Path::capacity, what + " fits the exported buffer");
                    longest = std::max(longest, path.size);
                    ReplayResult replayed = replay_path(board, piece, start, path.view(), path_config, true);
                    bool const arrival_ok = piece == Piece::T
                        ? replayed.arrival == candidate.arrival
                        : true;
                    check(replayed.valid && replayed.placement == candidate.placement && arrival_ok,
                        what + " replays exactly");
                    auto independent = published_replay::replay(model, rows, *p, 3, 21, 0,
                        path.view(), true, allow_180);
                    auto expected_cells = sorted_cells_of(piece, candidate.placement);
                    bool const independent_arrival_ok = piece == Piece::T
                        ? independent.arrival
                            == (candidate.arrival == ArrivalClass::TerminalRotation ? 1 : 0)
                        : true;
                    check(independent.valid && independent.cells == expected_cells
                        && independent_arrival_ok,
                        what + " replays through the independent interpreter");
                    if (candidate.arrival == ArrivalClass::TerminalRotation)
                    {
                        auto unlocked = published_replay::replay(model, rows, *p, 3, 21, 0,
                            path.view(), false, allow_180);
                        check(unlocked.valid && unlocked.cells == expected_cells,
                            what + " hard drop does not move the terminal placement");
                    }
                }
            }
        }
        std::println("corpus paths{}: {} candidates, {} terminal, longest {} commands", allow_180 ? "" : " 180 off",
            candidates, terminal, longest);
    }
}

int main()
{
    Engine engine = make_engine();
    run_directed_interpreter_tests(engine);
    run_corpus_path_tests(engine, true);
    run_corpus_path_tests(engine, false);
    std::println("path_differential: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
