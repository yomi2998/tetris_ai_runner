#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <algorithm>
#include <map>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace
{
    constexpr size_t width = 10;
    constexpr size_t height = 40;

    struct Cell
    {
        int x, y;
        bool operator==(Cell const &) const = default;
    };

    using Cells = std::vector<Cell>;

    Cells sorted(Cells c)
    {
        std::sort(c.begin(), c.end(), [](Cell a, Cell b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
        return c;
    }

    Cells translate(Cells const &c, int dx, int dy)
    {
        Cells out;
        for (auto v : c) out.push_back({v.x + dx, v.y + dy});
        return out;
    }

    Cell min_corner(Cells const &c)
    {
        int mx = c[0].x, my = c[0].y;
        for (auto v : c)
        {
            mx = std::min(mx, v.x);
            my = std::min(my, v.y);
        }
        return {mx, my};
    }

    Cells normalize(Cells const &c)
    {
        Cell m = min_corner(c);
        return sorted(translate(c, -m.x, -m.y));
    }

    Cells spawn_shape(char piece)
    {
        switch (piece)
        {
        case 'T': return {{0, 0}, {1, 0}, {2, 0}, {1, 1}};
        case 'J': return {{0, 0}, {1, 0}, {2, 0}, {0, 1}};
        case 'L': return {{0, 0}, {1, 0}, {2, 0}, {2, 1}};
        case 'S': return {{0, 0}, {1, 0}, {1, 1}, {2, 1}};
        case 'Z': return {{1, 0}, {2, 0}, {0, 1}, {1, 1}};
        case 'I': return {{0, 2}, {1, 2}, {2, 2}, {3, 2}};
        case 'O': return {{1, 0}, {2, 0}, {1, 1}, {2, 1}};
        }
        std::exit(1);
    }

    Cells orientation(char piece, int r)
    {
        Cells c = spawn_shape(piece);
        for (int i = 0; i < r; ++i)
        {
            for (Cell &v : c)
            {
                v = {v.y, 2 - v.x};
            }
        }
        return sorted(c);
    }

    Cells shape_at(char piece, int r)
    {
        if (piece == 'I')
        {
            switch (r)
            {
            case 0: return {{0, 2}, {1, 2}, {2, 2}, {3, 2}};
            case 1: return {{2, 0}, {2, 1}, {2, 2}, {2, 3}};
            case 2: return {{0, 1}, {1, 1}, {2, 1}, {3, 1}};
            case 3: return {{1, 0}, {1, 1}, {1, 2}, {1, 3}};
            }
            std::exit(1);
        }
        return orientation(piece, r);
    }

    struct Kick
    {
        int dx, dy;
    };

    std::vector<Kick> const jlSTZ_kicks = {
        {0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2},
        {0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2},
        {0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2},
        {0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2},
        {0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2},
        {0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2},
        {0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2},
        {0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2},
    };

    std::vector<Kick> const i_kicks = {
        {0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2},
        {0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2},
        {0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1},
        {0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1},
        {0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2},
        {0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2},
        {0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1},
        {0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1},
    };

    size_t kick_index(int from, bool clockwise)
    {
        size_t const group_of[4][2] = {{0, 7}, {2, 1}, {4, 3}, {6, 5}};
        return group_of[from % 4][clockwise ? 0 : 1];
    }

    struct Board
    {
        uint32_t row[height] = {};

        int column_top(int x) const
        {
            for (int y = static_cast<int>(height) - 1; y >= 0; --y)
            {
                if ((row[y] >> x) & 1)
                {
                    return y + 1;
                }
            }
            return 0;
        }

        bool free(Cells const &cells) const
        {
            if (cells.empty())
            {
                return false;
            }
            int bottom[10];
            for (int x = 0; x < 10; ++x)
            {
                bottom[x] = -1;
            }
            for (auto c : cells)
            {
                if (c.x < 0 || c.x >= static_cast<int>(width) || c.y < 0 || c.y >= static_cast<int>(height))
                {
                    return false;
                }
                if (bottom[c.x] < 0 || c.y < bottom[c.x])
                {
                    bottom[c.x] = c.y;
                }
            }
            for (int x = 0; x < 10; ++x)
            {
                if (bottom[x] >= 0 && bottom[x] < column_top(x))
                {
                    return false;
                }
            }
            return true;
        }

        size_t clear_full()
        {
            size_t cleared = 0;
            for (int y = 0; y < static_cast<int>(height);)
            {
                if (row[y] == 0x3ff)
                {
                    for (int j = y; j < static_cast<int>(height) - 1; ++j)
                    {
                        row[j] = row[j + 1];
                    }
                    row[height - 1] = 0;
                    ++cleared;
                }
                else
                {
                    ++y;
                }
            }
            return cleared;
        }
    };

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

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

    Engine engine;

    struct Frame
    {
        Cells pattern;
        int ax, ay;
    };

    std::map<std::pair<char, int>, Frame> frames;

    void measure_frames()
    {
        m_tetris::TetrisMap empty(width, height);
        for (char const *p = "TJLSZIO"; *p; ++p)
        {
            for (int r = 0; r < 4; ++r)
            {
                if (engine.context()->get_opertion(*p, static_cast<unsigned char>(r)).create == nullptr)
                {
                    continue;
                }
                m_tetris::TetrisNode node;
                if (!engine.context()->create(m_tetris::TetrisBlockStatus(*p, 0, 39, static_cast<uint8_t>(r)), node))
                {
                    std::println(stderr, "frame measurement failed for {} r{}", *p, r);
                    std::exit(1);
                }
                Cells cells;
                for (int ry = 0; ry < node.height; ++ry)
                {
                    for (int rx = 0; rx < node.width; ++rx)
                    {
                        if ((node.data[ry] >> (node.col + rx)) & 1)
                        {
                            cells.push_back({node.col + rx, node.row + ry});
                        }
                    }
                }
                cells = sorted(cells);
                Cell m = min_corner(cells);
                Frame f;
                f.pattern = normalize(cells);
                f.ax = m.x;
                f.ay = m.y - 39;
                frames[{*p, r}] = f;
            }
        }
    }

    Cells cells_at(char piece, int r, int x, int y)
    {
        Frame const &f = frames[{piece, r}];
        return translate(f.pattern, x + f.ax, y + f.ay);
    }

    bool in_bounds(Cells const &cells)
    {
        for (auto c : cells)
        {
            if (c.x < 0 || c.x >= static_cast<int>(width) || c.y < 0 || c.y >= static_cast<int>(height))
            {
                return false;
            }
        }
        return !cells.empty();
    }

    Cells node_cells(m_tetris::TetrisNode const *node)
    {
        Cells cells;
        for (int ry = 0; ry < node->height; ++ry)
        {
            for (int rx = 0; rx < node->width; ++rx)
            {
                if ((node->data[ry] >> (node->col + rx)) & 1)
                {
                    cells.push_back({node->col + rx, node->row + ry});
                }
            }
        }
        return sorted(cells);
    }

    void run_kick_sequence_cases()
    {
        for (auto const &[key, frame] : frames)
        {
            char const piece = key.first;
            int const r = key.second;
            for (int x = 0; x < static_cast<int>(width); ++x)
            {
                for (int y : {2, 20, 38})
                {
                    if (!in_bounds(cells_at(piece, r, x, y)))
                    {
                        continue;
                    }
                    m_tetris::TetrisNode const *node = engine.context()->get(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)));
                    if (node == nullptr)
                    {
                        continue;
                    }
                    for (bool clockwise : {true, false})
                    {
                        int const to = (r + (clockwise ? 1 : 3)) % 4;
                        size_t const base = kick_index(r, clockwise);
                        std::vector<m_tetris::TetrisBlockStatus> expected;
                        for (size_t i = 0; i < 5; ++i)
                        {
                            Kick const kick = (piece == 'I' ? i_kicks : jlSTZ_kicks)[base * 5 + i];
                            Cells const cand = cells_at(piece, to, x + kick.dx, y + kick.dy);
                            if (in_bounds(cand))
                            {
                                expected.push_back(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x + kick.dx), static_cast<int8_t>(y + kick.dy), static_cast<uint8_t>(to)));
                            }
                        }
                        std::vector<m_tetris::TetrisBlockStatus> actual;
                        m_tetris::TetrisNode const *const *seq = clockwise ? node->wall_kick_clockwise : node->wall_kick_counterclockwise;
                        for (size_t i = 0; i < m_tetris::max_wall_kick && seq[i] != nullptr; ++i)
                        {
                            actual.push_back(seq[i]->status);
                        }
                        bool same = actual.size() == expected.size();
                        for (size_t i = 0; same && i < actual.size(); ++i)
                        {
                            same = actual[i].status == expected[i].status;
                        }
                        check(same, std::string("kick sequence ") + piece + " r" + std::to_string(r)
                            + (clockwise ? " cw" : " ccw") + " x" + std::to_string(x) + " y" + std::to_string(y)
                            + (same ? "" : " unexpected candidate set"));
                    }
                }
            }
        }
    }

    void run_basic_rotation_cases()
    {
        for (auto const &[key, frame] : frames)
        {
            char const piece = key.first;
            if (piece == 'O')
            {
                continue;
            }
            int const r = key.second;
            int const to = (r + 1) % 4;
            for (int x = 0; x < static_cast<int>(width); ++x)
            {
                for (int y : {2, 20, 38})
                {
                    Cells const start = cells_at(piece, r, x, y);
                    if (!in_bounds(start))
                    {
                        continue;
                    }
                    m_tetris::TetrisNode node;
                    if (!engine.context()->create(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)), node))
                    {
                        continue;
                    }
                    bool const ok = node.op.rotate_clockwise(node, engine.context().get());
                    bool const expect_ok = in_bounds(cells_at(piece, to, x, y));
                    std::string what = std::string("basic rotation ") + piece + " r" + std::to_string(r)
                        + " x" + std::to_string(x) + " y" + std::to_string(y);
                    check(ok == expect_ok, what + " success mismatch");
                    if (ok && expect_ok)
                    {
                        check(node_cells(&node) == cells_at(piece, to, x, y), what + " cells mismatch");
                    }
                }
            }
        }
    }

    void run_180_cases()
    {
        Board empty;
        for (auto const &[key, frame] : frames)
        {
            char const piece = key.first;
            if (piece == 'O')
            {
                continue;
            }
            int const r = key.second;
            for (int x = 2; x <= 7; ++x)
            {
                m_tetris::TetrisNode node;
                if (!engine.context()->create(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x), 20, static_cast<uint8_t>(r)), node))
                {
                    continue;
                }
                bool const ok = node.op.rotate_opposite(node, engine.context().get());
                std::string what = std::string("180 ") + piece + " r" + std::to_string(r) + " x" + std::to_string(x);
                check(ok, what + " direct 180 failed on empty board");
                if (ok)
                {
                    Cells const got = normalize(node_cells(&node));
                    Cells const want = normalize(shape_at(piece, (r + 2) % 4));
                    check(got == want, what + " opposite orientation pattern mismatch");
                }
            }
        }
        (void)empty;
    }

    void run_board_cases()
    {
        struct AttachCase
        {
            const char *name;
            std::vector<uint32_t> in;
            char piece;
            int x, y, r;
            std::vector<uint32_t> out;
            size_t clear;
        };
        std::vector<AttachCase> cases = {
            {"single_clear_bottom", {0x0ff, 0x3f0, 0x0f0}, 'I', 0, 2, 0,
             {0x0ff, 0x0f0}, 1},
            {"no_clear", {0x0ff}, 'O', 0, 2, 0,
             {0x0ff, 0x006, 0x006}, 0},
            {"mid_clear_shift", {0x000, 0x0ff, 0x3f0, 0x0f0}, 'I', 0, 3, 0,
             {0x000, 0x0ff, 0x0f0}, 1},
            {"hole_row_kept", {0x2aa, 0x155}, 'T', 3, 2, 0,
             {0x2aa, 0x17d, 0x010}, 0},
        };
        for (auto const &c : cases)
        {
            m_tetris::TetrisMap map(width, height);
            for (size_t i = 0; i < c.in.size(); ++i)
            {
                map.row[i] = c.in[i];
            }
            for (size_t y = 0; y < height; ++y)
            {
                for (size_t x = 0; x < width; ++x)
                {
                    if (map.full(x, y))
                    {
                        map.top[x] = static_cast<int32_t>(y + 1);
                        map.roof = std::max<int32_t>(map.roof, static_cast<int32_t>(y + 1));
                        ++map.count;
                    }
                }
            }
            m_tetris::TetrisNode node;
            std::string what = std::string("board ") + c.name;
            bool const created = engine.context()->create(m_tetris::TetrisBlockStatus(c.piece, static_cast<int8_t>(c.x), static_cast<int8_t>(c.y), static_cast<uint8_t>(c.r)), node);
            check(created, what + " piece create failed");
            if (!created)
            {
                continue;
            }
            size_t const cleared = node.attach(engine.context().get(), map);
            check(cleared == c.clear, what + " clear count mismatch");
            bool rows_ok = cleared == c.clear;
            for (size_t i = 0; i < height; ++i)
            {
                uint32_t want = i < c.out.size() ? c.out[i] : 0;
                rows_ok = rows_ok && map.row[i] == want;
            }
            check(rows_ok, what + " resulting rows mismatch");
        }
    }

    struct ScalarState
    {
        int x, y, r;
    };

    struct ScalarReplayer
    {
        Board board;
        char piece;

        bool valid(ScalarState s) const
        {
            return board.free(cells_at(piece, s.r, s.x, s.y));
        }

        bool rotate(ScalarState &s, bool clockwise, bool opposite)
        {
            int const to = opposite ? (s.r + 2) % 4 : (s.r + (clockwise ? 1 : 3)) % 4;
            auto test = [&](int dx, int dy) {
                ScalarState c{s.x + dx, s.y + dy, to};
                if (valid(c))
                {
                    s = c;
                    return true;
                }
                return false;
            };
            if (opposite)
            {
                if (test(0, 0))
                {
                    return true;
                }
                m_tetris::TetrisNode const *node = engine.context()->get(m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(s.x), static_cast<int8_t>(s.y), static_cast<uint8_t>(s.r)));
                if (node == nullptr)
                {
                    return false;
                }
                for (size_t i = 0; i < m_tetris::max_wall_kick; ++i)
                {
                    m_tetris::TetrisNode const *n = node->wall_kick_opposite[i];
                    if (n == nullptr)
                    {
                        break;
                    }
                    if (test(n->status.x - node->status.x, n->status.y - node->status.y))
                    {
                        return true;
                    }
                }
                return false;
            }
            auto const &table = piece == 'I' ? i_kicks : jlSTZ_kicks;
            size_t const idx = kick_index(s.r, clockwise) * 5;
            for (size_t i = 0; i < 5; ++i)
            {
                if (test(table[idx + i].dx, table[idx + i].dy))
                {
                    return true;
                }
            }
            return false;
        }

        bool shift(ScalarState &s, int dx)
        {
            if (valid({s.x + dx, s.y, s.r}))
            {
                s.x += dx;
                return true;
            }
            return false;
        }

        bool drop_one(ScalarState &s)
        {
            if (valid({s.x, s.y - 1, s.r}))
            {
                --s.y;
                return true;
            }
            return false;
        }

        void drop_to_rest(ScalarState &s)
        {
            while (drop_one(s))
            {
            }
        }
    };

    void run_replay_cases()
    {
        search_tspin::Search search;
        search_tspin::Search::Config config;
        config.allow_rotate_move = false;
        config.allow_180 = true;
        config.allow_d = true;
        config.allow_nont_d = false;
        config.is_20g = false;
        config.last_rotate = false;
        search.init(engine.context().get(), &config);
        std::mt19937 rng(20260903u);
        size_t paths_checked = 0;
        for (size_t b = 0; b < 12; ++b)
        {
            m_tetris::TetrisMap map(width, height);
            size_t const roof = 4 + rng() % 14;
            for (size_t y = 0; y < roof; ++y)
            {
                map.row[y] = static_cast<uint32_t>(rng()) & 0x3ff;
            }
            for (size_t y = 0; y < height; ++y)
            {
                for (size_t x = 0; x < width; ++x)
                {
                    if (map.full(x, y))
                    {
                        map.top[x] = static_cast<int32_t>(y + 1);
                        map.roof = std::max<int32_t>(map.roof, static_cast<int32_t>(y + 1));
                        ++map.count;
                    }
                }
            }
            Board board;
            for (size_t y = 0; y < height; ++y)
            {
                board.row[y] = map.row[y];
            }
            for (char const *p = "TJLSZI"; *p; ++p)
            {
                m_tetris::TetrisNode const *spawn = engine.context()->generate(*p);
                auto const *results = search.search(map, spawn, 1);
                size_t taken = 0;
                for (auto const &land : *results)
                {
                    if (taken >= 6) break;
                    auto path = search.make_path(spawn, land, map);
                    ScalarReplayer replayer{board, *p};
                    ScalarState s{spawn->status.x, spawn->status.y, spawn->status.r};
                    bool locked = false;
                    bool ok = true;
                    for (char cmd : std::string_view(path.data(), path.size()))
                    {
                        switch (cmd)
                        {
                        case 'l': replayer.shift(s, -1); break;
                        case 'r': replayer.shift(s, 1); break;
                        case 'L': while (replayer.shift(s, -1)) {} break;
                        case 'R': while (replayer.shift(s, 1)) {} break;
                        case 'd': replayer.drop_one(s); break;
                        case 'D': replayer.drop_to_rest(s); break;
                        case 'z': replayer.rotate(s, false, false); break;
                        case 'c': replayer.rotate(s, true, false); break;
                        case 'x': replayer.rotate(s, false, true); break;
                        default: ok = false; break;
                        }
                    }
                    replayer.drop_to_rest(s);
                    locked = ok;
                    Cells const final_cells = cells_at(*p, s.r, s.x, s.y);
                    Cells const want = node_cells(land.node);
                    auto dump = [](Cells const &c) {
                        std::string s;
                        for (auto v : c)
                        {
                            s += std::to_string(v.x) + "," + std::to_string(v.y) + " ";
                        }
                        return s;
                    };
                    ++paths_checked;
                    check(locked, "replay path contains an unknown command");
                    check(ok && final_cells == want, std::string("replay mismatch board ") + std::to_string(b)
                        + " for " + *p + " path " + std::string(path.data(), path.size())
                        + " got[" + dump(final_cells) + "] want[" + dump(want) + "]");
                    ++taken;
                }
            }
        }
        std::println("replay paths checked: {}", paths_checked);
    }
}

int main()
{
    if (!engine.prepare(width, height))
    {
        std::println(stderr, "engine.prepare failed");
        return 1;
    }
    engine.search_config()->allow_rotate_move = false;
    engine.search_config()->allow_180 = true;
    engine.search_config()->allow_d = true;
    engine.search_config()->allow_nont_d = false;
    engine.search_config()->is_20g = false;
    engine.search_config()->last_rotate = false;

    measure_frames();
    run_kick_sequence_cases();
    run_basic_rotation_cases();
    run_180_cases();
    run_board_cases();
    run_replay_cases();

    std::println("spec oracle: {} checks, {} failures", checks, failures);
    return failures == 0 ? 0 : 1;
}
