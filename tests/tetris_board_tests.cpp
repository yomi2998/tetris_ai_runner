// tetris_board_tests.cpp
// Phase 1 tests. Property tests for the canonical Board against a simple
// row-array oracle, directed boundary tests, and differential tests against
// legacy TetrisMap transitions on valid 10 by 40 states.

#include "tetris_board.h"
#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

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
    using occupancy_t = Board::occupancy_t;

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

        void add_garbage(int lines, uint16_t garbage_row)
        {
            int const effective = lines >= Board::height ? Board::height : lines;
            uint16_t shifted[Board::height] = {};
            for (int y = Board::height - 1; y >= effective; --y)
            {
                shifted[y] = rows[y - effective];
            }
            for (int y = 0; y < effective; ++y)
            {
                shifted[y] = garbage_row;
            }
            for (int y = 0; y < Board::height; ++y)
            {
                rows[y] = shifted[y];
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
        check(board.empty() == (oracle.roof() == 0), what + " empty() mismatch");
    }

    void run_type_tests()
    {
        auto extreme = tetris::Placement::try_make(9, 47, 3);
        check(extreme.has_value() && extreme->x() == 9 && extreme->y() == 47 && extreme->rotation() == 3, "placement extremes round trip");
        tetris::Placement z;
        check(z.x() == 0 && z.y() == 0 && z.rotation() == 0, "placement default");
        std::mt19937 rng(7u);
        for (int i = 0; i < 1000; ++i)
        {
            int x = static_cast<int>(rng() % 10);
            int y = static_cast<int>(rng() % 48);
            int r = static_cast<int>(rng() % 4);
            auto q = tetris::Placement::try_make(x, y, r);
            check(q.has_value() && q->x() == x && q->y() == y && q->rotation() == r, "placement round trip");
        }
        int const bad_args[][3] = {{-1, -1, -1}, {10, 0, 0}, {16, 0, 0}, {0, 48, 0}, {0, 64, 0}, {0, 0, 4}, {0, 0, -1}, {-5, 70, 9}};
        for (auto const &arg : bad_args)
        {
            check(!tetris::Placement::try_make(arg[0], arg[1], arg[2]).has_value(), "placement rejects invalid arguments");
        }
        auto masked = tetris::Placement::unchecked(0x13, 0x71, 0x7);
        check(masked.x() == 3 && masked.y() == 49 && masked.rotation() == 3, "unchecked placement masks to valid ranges");
        check(tetris::piece_count == 7, "piece count");
        for (char c : std::string_view("TZSJLOI"))
        {
            auto piece = tetris::try_from_char(c);
            check(piece.has_value() && tetris::to_char(*piece) == c, "piece char round trip");
        }
        for (char c : {' ', '?', 't', 'z', '-', '0', '\0', '\xff', 'i', 'o'})
        {
            check(!tetris::try_from_char(c).has_value(), std::string("piece conversion rejects '") + c + "'");
        }
        tetris::Candidate candidate;
        check(candidate.arrival == tetris::ArrivalClass::Normal, "candidate default arrival");
        tetris::Outcome outcome;
        check(outcome.spin == tetris::SpinType::None && outcome.clear_count == 0 && !outcome.lockout, "outcome defaults");
        tetris::DecisionContext context;
        check(!context.hold.has_value() && !context.used_hold && context.depth == 0 && context.next.empty(), "decision context defaults");
    }

    occupancy_t mask_of(std::vector<std::pair<int, int>> const &cells)
    {
        occupancy_t mask;
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
                    check(board.apply(mask), "property apply rejected a valid mask at step " + std::to_string(step));
                }
            }
            else
            {
                int lines = 1 + static_cast<int>(rng() % 4);
                uint16_t hole = static_cast<uint16_t>(Board::row_mask & ~(1u << (rng() % Board::width)));
                oracle.add_garbage(lines, hole);
                board.add_garbage(lines, hole);
            }
            std::array<bool, Board::height> was_full = {};
            for (int y = 0; y < Board::height; ++y)
            {
                was_full[y] = oracle.rows[y] == Board::row_mask;
            }
            auto result = board.cleared();
            int expected = oracle.clear_full();
            check(result.count == expected, "clear count mismatch at step " + std::to_string(step));
            for (int y = 0; y < Board::height; ++y)
            {
                check(result.cleared_row(y) == was_full[y], "cleared row mask mismatch at step " + std::to_string(step));
            }
            board = result.board;
            compare(board, oracle, "step " + std::to_string(step));
        }
    }

    void run_boundary_tests()
    {
        std::mt19937 rng(555u);
        int const boundaries[][2] = {{5, 6}, {11, 12}, {35, 36}, {41, 42}};
        for (auto const &pair : boundaries)
        {
            Oracle oracle;
            for (int y = 0; y < Board::height; ++y)
            {
                oracle.rows[y] = static_cast<uint16_t>(rng() & Board::row_mask);
                if (oracle.rows[y] == Board::row_mask)
                {
                    oracle.rows[y] &= 0x1ff;
                }
            }
            oracle.rows[pair[0]] = Board::row_mask;
            oracle.rows[pair[1]] = Board::row_mask;
            Board board = Board::from_rows(std::to_array(oracle.rows));
            auto result = board.cleared();
            int expected = oracle.clear_full();
            check(result.count == expected, "boundary clear count rows " + std::to_string(pair[0]) + "/" + std::to_string(pair[1]));
            compare(result.board, oracle, "boundary clear rows " + std::to_string(pair[0]) + "/" + std::to_string(pair[1]));
        }

        int const four_line_spans[][2] = {{3, 6}, {4, 7}, {5, 8}, {10, 13}, {41, 44}, {44, 47}};
        for (auto const &span : four_line_spans)
        {
            Oracle oracle;
            for (int y = 0; y < Board::height; ++y)
            {
                oracle.rows[y] = static_cast<uint16_t>(rng() & Board::row_mask);
                if (oracle.rows[y] == Board::row_mask)
                {
                    oracle.rows[y] &= 0x1ff;
                }
            }
            for (int y = span[0]; y < span[0] + 4 && y < Board::height; ++y)
            {
                oracle.rows[y] = Board::row_mask;
            }
            Board board = Board::from_rows(std::to_array(oracle.rows));
            auto result = board.cleared();
            int expected = oracle.clear_full();
            check(result.count == expected, "four-line clear count spanning " + std::to_string(span[0]));
            compare(result.board, oracle, "four-line clear spanning " + std::to_string(span[0]));
        }

        {
            Oracle oracle;
            for (int y = 42; y < 48; ++y)
            {
                oracle.rows[y] = Board::row_mask;
            }
            Board board = Board::from_rows(std::to_array(oracle.rows));
            check(board.roof() == 48, "top-region roof is 48 before clear");
            auto result = board.cleared();
            check(result.count == 6, "clear entirely in rows 42-47 count");
            check(result.board.empty() && result.board.roof() == 0, "clear to empty resets roof");
        }

        {
            Oracle oracle;
            oracle.rows[47] = 0x3ff;
            Board board = Board::from_rows(std::to_array(oracle.rows));
            check(board.roof() == 48, "row 47 roof is 48");
            check(board.full(0, 47) && board.full(9, 47), "row 47 cells queryable");
        }
    }

    uint64_t rows_as_word(std::array<uint16_t, Board::height> const &rows, int word)
    {
        uint64_t w = 0;
        for (int k = 0; k < occupancy_t::lines_per_under; ++k)
        {
            int y = word * occupancy_t::lines_per_under + k;
            w |= static_cast<uint64_t>(y < Board::height ? rows[y] : 0) << (10 * k);
        }
        return w;
    }

    std::array<uint16_t, Board::height> rows_with_single_cell()
    {
        std::array<uint16_t, Board::height> rows = {};
        rows[0] = 1;
        return rows;
    }

    void run_query_tests()
    {
        std::mt19937 rng(31337u);
        for (int trial = 0; trial < 200; ++trial)
        {
            Oracle oracle;
            for (int y = 0; y < Board::height; ++y)
            {
                oracle.rows[y] = static_cast<uint16_t>(rng() & Board::row_mask);
            }
            Board board = Board::from_rows(std::to_array(oracle.rows));
            bool row_full_ok = true;
            bool tops_ok = true;
            for (int y = 0; y < Board::height; ++y)
            {
                check(board.row(y) == oracle.rows[y], "row() agreement trial " + std::to_string(trial));
                for (int x = 0; x < Board::width; ++x)
                {
                    row_full_ok = row_full_ok && board.full(x, y) == oracle.occupied(x, y);
                }
            }
            check(row_full_ok, "full() agreement trial " + std::to_string(trial));
            auto tops = board.column_tops();
            for (int x = 0; x < Board::width; ++x)
            {
                unsigned expected = 0;
                for (int y = Board::height - 1; y >= 0; --y)
                {
                    if (oracle.occupied(x, y))
                    {
                        expected = static_cast<unsigned>(y + 1);
                        break;
                    }
                }
                tops_ok = tops_ok && tops[x] == expected;
            }
            check(tops_ok, "column_tops() agreement trial " + std::to_string(trial));
            auto rows = board.rows();
            bool occupancy_ok = true;
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                occupancy_ok = occupancy_ok && board.occupancy().logical_word(i) == (occupancy_t::word_logical_mask(i) & rows_as_word(rows, i));
            }
            check(occupancy_ok, "occupancy() agreement trial " + std::to_string(trial));
        }
    }

    void run_equality_hash_tests()
    {
        std::mt19937 rng(777777u);
        for (int trial = 0; trial < 200; ++trial)
        {
            Oracle oracle;
            for (int y = 0; y < Board::height; ++y)
            {
                oracle.rows[y] = static_cast<uint16_t>(rng() & Board::row_mask);
            }
            Board via_rows = Board::from_rows(std::to_array(oracle.rows));
            Board via_apply;
            for (int y = 0; y < Board::height; ++y)
            {
                for (int x = 0; x < Board::width; ++x)
                {
                    if (oracle.occupied(x, y))
                    {
                        via_apply.apply(mask_of({{x, y}}));
                    }
                }
            }
            check(via_rows == via_apply, "equality across construction paths trial " + std::to_string(trial));
            check(via_rows.hash() == via_apply.hash(), "hash agreement across construction paths trial " + std::to_string(trial));
            Oracle mutated = oracle;
            mutated.rows[rng() % Board::height] ^= static_cast<uint16_t>(1u << (rng() % Board::width));
            Board other = Board::from_rows(std::to_array(mutated.rows));
            check(via_rows != other, "inequality on differing occupancy trial " + std::to_string(trial));
        }

        occupancy_t clean_mask = mask_of({{0, 0}, {5, 23}, {9, 47}});
        std::array<uint64_t, occupancy_t::num_of_under> dirty = {};
        for (int i = 0; i < occupancy_t::num_of_under; ++i)
        {
            dirty[i] = clean_mask.logical_word(i) | 0xf000000000000000ull;
        }
        occupancy_t padded_mask{dirty};
        Board with_padding;
        check(with_padding.apply(padded_mask) == true, "apply accepts occupancy with padding bits");
        Board via_clean;
        via_clean.apply(clean_mask);
        check(with_padding == via_clean, "padding bits canonicalized on apply");
        check(with_padding.hash() == via_clean.hash(), "hash canonicalized on apply");
        check(with_padding.rows() == via_clean.rows(), "padding bits invisible in rows");

        occupancy_t overlapping = mask_of({{0, 0}});
        Board base;
        base.apply(mask_of({{0, 0}}));
        check(base.apply(overlapping) == false, "checked apply rejects overlap in release");
        check(base == Board::from_rows(rows_with_single_cell()), "rejected apply leaves board unchanged");
    }


    void run_garbage_tests()
    {
        std::mt19937 rng(2468u);
        int const counts[] = {0, 1, 47, 48, 50};
        for (int lines : counts)
        {
            Oracle oracle;
            for (int y = 0; y < Board::height; ++y)
            {
                oracle.rows[y] = static_cast<uint16_t>(rng() & Board::row_mask);
            }
            Board board = Board::from_rows(std::to_array(oracle.rows));
            uint16_t garbage_row = static_cast<uint16_t>(rng() & Board::row_mask);
            oracle.add_garbage(lines, garbage_row);
            board.add_garbage(lines, garbage_row);
            compare(board, oracle, "garbage " + std::to_string(lines) + " lines");
        }
        {
            Board board;
            board.add_garbage(2, 0x2aa);
            check(board.row(0) == 0x2aa && board.row(1) == 0x2aa && board.row(2) == 0 && board.roof() == 2, "garbage fills bottom rows with the garbage row pattern");
            board.add_garbage(48, 0x155);
            for (int y = 0; y < Board::height; ++y)
            {
                check(board.row(y) == 0x155, "garbage 48 replaces every row");
            }
            check(board.roof() == 48, "garbage 48 roof");
            board.add_garbage(50, 0x0ff);
            for (int y = 0; y < Board::height; ++y)
            {
                check(board.row(y) == 0x0ff, "garbage beyond height replaces every row");
            }
        }
        {
            std::array<uint16_t, Board::height> rows = {};
            rows[3] = 0xffff;
            rows[5] = 0xffffffffu & Board::row_mask;
            Board board = Board::from_rows(rows);
            check(board.row(3) == 0x3ff, "malformed row masks are clipped to board width");
            check(board.row(5) == 0x3ff, "malformed row masks are clipped to board width");
        }
    }

    void run_exported_tests()
    {
        std::array<uint32_t, 23> field = {};
        std::array<uint32_t, 8> overfield = {};
        for (size_t i = 0; i < field.size(); ++i)
        {
            field[i] = 1u << (i % 10) | 0x400u;
        }
        for (size_t i = 0; i < overfield.size(); ++i)
        {
            overfield[i] = 1u << ((i + 3) % 10) | 0x400u;
        }
        Board board = Board::from_exported(field, overfield);
        bool ok = true;
        for (int y = 0; y <= 22; ++y)
        {
            ok = ok && board.row(y) == static_cast<uint16_t>(field[static_cast<size_t>(22 - y)] & Board::row_mask);
        }
        for (int y = 23; y <= 30; ++y)
        {
            ok = ok && board.row(y) == static_cast<uint16_t>(overfield[static_cast<size_t>(y - 23)] & Board::row_mask);
        }
        for (int y = 31; y < Board::height; ++y)
        {
            ok = ok && board.row(y) == 0;
        }
        check(ok, "hand-authored exported ordering and masking");
        check(board.roof() == 31 && board.full(6, 26), "exported overfield reaches row 30");
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

    std::array<uint16_t, Board::height> legacy_rows(m_tetris::TetrisMap const &map)
    {
        std::array<uint16_t, Board::height> rows = {};
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
        size_t attachments = 0;
        for (int trial = 0; trial < 300; ++trial)
        {
            m_tetris::TetrisMap map(10, 40);
            unsigned roof = 4 + rng() % 36;
            for (unsigned y = 0; y < roof && y < 40; ++y)
            {
                map.row[y] = static_cast<uint32_t>(rng()) & Board::row_mask;
                if (map.row[y] == Board::row_mask)
                {
                    map.row[y] &= ~(1u << (rng() % 10));
                }
            }
            rebuild_metadata(map);
            std::array<uint16_t, Board::height> rows = legacy_rows(map);
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
                occupancy_t mask;
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
                ++attachments;
                check(board.apply(mask), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " board apply matches legacy legality");
                auto cleared = board.cleared();
                check(cleared.count == static_cast<int>(legacy_clear), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " clear count");
                board = cleared.board;
                auto board_rows = board.rows();
                bool rows_ok = true;
                for (int y = 0; y < 40; ++y)
                {
                    rows_ok = rows_ok && board_rows[y] == static_cast<uint16_t>(map.row[y] & Board::row_mask);
                }
                for (int y = 40; y < Board::height; ++y)
                {
                    rows_ok = rows_ok && board_rows[y] == 0;
                }
                check(rows_ok, "trial " + std::to_string(trial) + " move " + std::to_string(move) + " rows vs legacy");
                check(board.roof() == static_cast<unsigned>(map.roof), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " roof vs legacy");
                check(board.empty() == (map.count == 0), "trial " + std::to_string(trial) + " move " + std::to_string(move) + " emptiness vs legacy count");
            }
        }
        check(attachments >= 2000, "legacy differential exercised enough attachments: " + std::to_string(attachments));
        std::println("legacy differential attachments: {}", attachments);
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
            unsigned roof = rng() % 31;
            for (unsigned y = 0; y < roof && y < 40; ++y)
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
    run_boundary_tests();
    run_query_tests();
    run_equality_hash_tests();
    run_garbage_tests();
    run_exported_tests();
    run_legacy_differential();
    run_exported_import_test();
    std::println("sizeof(Board) = {}, sizeof(occupancy_t) = {}, alignof(occupancy_t) = {}",
        sizeof(Board), sizeof(occupancy_t), alignof(occupancy_t));
    std::println("tetris_board_tests: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
