// tetris_board_tests.cpp
// Phase 1 tests. Property tests for the canonical Board against a simple
// row-array oracle, and differential tests against legacy TetrisMap
// transitions on valid 10 by 40 states.

#include "tetris_board.h"
#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <cstring>
#include <print>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    size_t checks = 0;
    size_t failures = 0;

    void check(bool ok, std::string const &what)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::println(stderr, "FAIL: {}", what);
        }
    }

    using Board = tetris::Board;

    struct Oracle
    {
        uint16_t rows[Board::height] = {};

        unsigned roof() const
        {
            for (int y = Board::height - 1; y >= 0; --y)
            {
                if (rows[y] != 0)
                {
                    return static_cast<unsigned>(y + 1);
                }
            }
            return 0;
        }

        bool occupied(int x, int y) const
        {
            return (rows[y] >> x) & 1;
        }

        int clear_full()
        {
            int cleared = 0;
            for (int y = 0; y < Board::height;)
            {
                if (rows[y] == Board::row_mask)
                {
                    for (int j = y; j < Board::height - 1; ++j)
                    {
                        rows[j] = rows[j + 1];
                    }
                    rows[Board::height - 1] = 0;
                    ++cleared;
                }
                else
                {
                    ++y;
                }
            }
            return cleared;
        }

        void add_garbage(int lines, uint16_t hole)
        {
            for (int y = Board::height - 1; y >= lines; --y)
            {
                rows[y] = rows[y - lines];
            }
            for (int y = 0; y < lines; ++y)
            {
                rows[y] = hole;
            }
        }
    };

    void compare(Board const &board, Oracle const &oracle, std::string const &what)
    {
        auto rows = board.rows();
        bool rows_ok = true;
        for (int y = 0; y < Board::height; ++y)
        {
            rows_ok = rows_ok && rows[y] == oracle.rows[y];
        }
        check(rows_ok, what + " rows mismatch");
        check(board.roof() == oracle.roof(), what + " roof mismatch: board " + std::to_string(board.roof()) + " oracle " + std::to_string(oracle.roof()));
        bool emptiness_ok = board.empty() == (oracle.roof() == 0);
        check(emptiness_ok, what + " empty() mismatch");
    }

    void run_type_tests()
    {
        tetris::Placement p(9, 47, 3);
        check(p.x() == 9 && p.y() == 47 && p.rotation() == 3, "placement extremes round trip");
        tetris::Placement z;
        check(z.x() == 0 && z.y() == 0 && z.rotation() == 0, "placement default");
        std::mt19937 rng(7u);
        for (int i = 0; i < 1000; ++i)
        {
            int x = static_cast<int>(rng() % 10);
            int y = static_cast<int>(rng() % 48);
            int r = static_cast<int>(rng() % 4);
            tetris::Placement q(x, y, r);
            check(q.x() == x && q.y() == y && q.rotation() == r, "placement round trip");
        }
        check(tetris::piece_count == 7, "piece count");
        for (char c : std::string_view("TZSJLOI"))
        {
            tetris::Piece piece = tetris::from_char(c);
            check(tetris::to_char(piece) == c, "piece char round trip");
        }
        tetris::Candidate candidate;
        check(candidate.arrival == tetris::ArrivalClass::Normal, "candidate default arrival");
        tetris::Outcome outcome;
        check(outcome.spin == tetris::SpinType::None && outcome.clear_count == 0 && !outcome.lockout, "outcome defaults");
        tetris::DecisionContext context;
        check(!context.hold.has_value() && !context.used_hold && context.depth == 0 && context.next.empty(), "decision context defaults");
    }

    Board::occupancy_t mask_of(std::vector<std::pair<int, int>> const &cells)
    {
        Board::occupancy_t mask;
        for (auto [x, y] : cells)
        {
            mask.set(x, y);
        }
        return mask;
    }

    void run_property_tests()
    {
        std::mt19937 rng(20260904u);
        Board board;
        Oracle oracle;
        compare(board, oracle, "initial");
        check(board.hash() == Board{board}.hash(), "hash stable on copy");
        check(board == Board{}, "empty equality");

        for (int step = 0; step < 4000; ++step)
        {
            int op = static_cast<int>(rng() % 100);
            if (op < 60)
            {
                std::vector<std::pair<int, int>> cells;
                int const count = 1 + static_cast<int>(rng() % 4);
                for (int i = 0; i < count; ++i)
                {
                    int x = static_cast<int>(rng() % Board::width);
                    int y = static_cast<int>(rng() % Board::height);
                    if (!oracle.occupied(x, y))
                    {
                        cells.push_back({x, y});
                    }
                }
                if (!cells.empty())
                {
                    auto mask = mask_of(cells);
                    for (auto [x, y] : cells)
                    {
                        oracle.rows[y] |= static_cast<uint16_t>(1u << x);
                    }
                    board.apply(mask);
                }
            }
            else
            {
                int lines = 1 + static_cast<int>(rng() % 4);
                uint16_t hole = static_cast<uint16_t>(Board::row_mask & ~(1u << (rng() % Board::width)));
                oracle.add_garbage(lines, hole);
                board.add_garbage(lines, hole);
            }
            auto result = board.cleared();
            int expected = oracle.clear_full();
            check(result.count == expected, "clear count mismatch at step " + std::to_string(step));
            board = result.board;
            compare(board, oracle, "step " + std::to_string(step));
        }
    }

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (int y = 0; y < map.height; ++y)
        {
            for (int x = 0; x < map.width; ++x)
            {
                if (map.full(x, y))
                {
                    map.top[x] = y + 1;
                    map.roof = std::max(map.roof, y + 1);
                    ++map.count;
                }
            }
        }
    }

    std::vector<uint16_t> legacy_rows(m_tetris::TetrisMap const &map)
    {
        std::vector<uint16_t> rows(Board::height, 0);
        for (int y = 0; y < map.height; ++y)
        {
            rows[y] = static_cast<uint16_t>(map.row[y] & Board::row_mask);
        }
        return rows;
    }

    void run_legacy_differential()
    {
        Engine engine;
        if (!engine.prepare(10, 40))
        {
            std::println(stderr, "engine.prepare failed");
            std::exit(1);
        }
        std::mt19937 rng(424243u);
        char const *pieces = "TJLSZIO";
        for (int trial = 0; trial < 300; ++trial)
        {
            m_tetris::TetrisMap map(10, 40);
            unsigned roof = 4 + rng() % 17;
            for (unsigned y = 0; y < roof; ++y)
            {
                map.row[y] = static_cast<uint32_t>(rng()) & Board::row_mask;
                if (map.row[y] == Board::row_mask)
                {
                    map.row[y] &= ~(1u << (rng() % 10));
                }
            }
            rebuild_metadata(map);
            std::array<uint16_t, Board::height> rows = {};
            {
                auto legacy = legacy_rows(map);
                for (int y = 0; y < Board::height; ++y)
                {
                    rows[y] = legacy[y];
                }
            }
            Board board = Board::from_rows(rows);
            compare(board, [&] {
                Oracle o;
                for (int y = 0; y < Board::height; ++y)
                {
                    o.rows[y] = rows[y];
                }
                return o;
            }(), "trial " + std::to_string(trial) + " import");
            check(board.roof() == static_cast<unsigned>(map.roof), "trial " + std::to_string(trial) + " import roof vs legacy");

            for (int move = 0; move < 40; ++move)
            {
                char piece = pieces[rng() % 7];
                int x = static_cast<int>(rng() % 10);
                int y = static_cast<int>(rng() % 36);
                int r = static_cast<int>(rng() % 4);
                if (engine.context()->get_opertion(piece, static_cast<unsigned char>(r)).create == nullptr)
                {
                    continue;
                }
                m_tetris::TetrisNode node;
                if (!engine.context()->create(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)), node))
                {
                    continue;
                }
                if (!node.check(map))
                {
                    continue;
                }
                Board::occupancy_t mask;
                for (int ry = 0; ry < node.height; ++ry)
                {
                    for (int rx = 0; rx < node.width; ++rx)
                    {
                        if ((node.data[ry] >> (node.col + rx)) & 1)
                        {
                            mask.set(node.col + rx, node.row + ry);
                        }
                    }
                }
                size_t legacy_clear = node.attach(engine.context().get(), map);
                board.apply(mask);
                auto cleared = board.cleared();
                check(cleared.count == static_cast<int>(legacy_clear), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " clear count");
                board = cleared.board;
                auto board_rows = board.rows();
                bool rows_ok = true;
                for (int y = 0; y < 40; ++y)
                {
                    rows_ok = rows_ok && board_rows[y] == static_cast<uint16_t>(map.row[y] & Board::row_mask);
                }
                check(rows_ok, "trial " + std::to_string(trial) + " move " + std::to_string(move) + " rows vs legacy");
                check(board.roof() == static_cast<unsigned>(map.roof), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " roof vs legacy");
                check(board.empty() == (map.count == 0), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " emptiness vs legacy count");
            }
        }
    }

    void run_exported_import_test()
    {
        Engine engine;
        if (!engine.prepare(10, 40))
        {
            std::println(stderr, "engine.prepare failed");
            std::exit(1);
        }
        std::mt19937 rng(998877u);
        for (int trial = 0; trial < 100; ++trial)
        {
            m_tetris::TetrisMap map(10, 40);
            unsigned roof = rng() % 30;
            for (unsigned y = 0; y < roof; ++y)
            {
                map.row[y] = static_cast<uint32_t>(rng()) & Board::row_mask;
            }
            rebuild_metadata(map);
            std::array<uint32_t, 23> field = {};
            std::array<uint32_t, 8> overfield = {};
            for (int r = 0; r < 22; ++r)
            {
                field[r] = map.row[22 - r];
            }
            field[22] = map.row[0];
            for (int k = 0; k < 8; ++k)
            {
                overfield[k] = map.row[23 + k];
            }
            Board board = Board::from_exported(field, overfield);
            auto board_rows = board.rows();
            bool rows_ok = true;
            for (int y = 0; y <= 30; ++y)
            {
                rows_ok = rows_ok && board_rows[y] == static_cast<uint16_t>(map.row[y] & Board::row_mask);
            }
            for (int y = 31; y < Board::height; ++y)
            {
                rows_ok = rows_ok && board_rows[y] == 0;
            }
            check(rows_ok, "exported import trial " + std::to_string(trial) + " rows");
            check(board.roof() == static_cast<unsigned>(map.roof), "exported import trial " + std::to_string(trial) + " roof");
        }
    }
}

int main()
{
    run_type_tests();
    run_property_tests();
    run_legacy_differential();
    run_exported_import_test();
    std::println("tetris_board_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
