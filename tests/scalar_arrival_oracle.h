#include "perft.hpp"

#include <print>
#include <queue>
#include <random>
#include <string>
#include <vector>

using reachability::operator""_szc;

namespace scalar_arrival
{
    using namespace reachability;
struct ScalarConfig
{
    bool allow_180;
    bool allow_softdrop;
    bool allow_sonicdrop;
    bool allow_20g;
};

struct KickRule
{
    int from;
    int to;
    std::vector<std::pair<int, int>> offsets;
};

template <auto B>
struct PieceGeometry
{
    static constexpr int orientations = B.orientations;
    std::array<std::array<int, 4>, orientations> cell_x{};
    std::array<std::array<int, 4>, orientations> cell_y{};
    std::array<int, orientations> cell_count{};
    std::array<int, orientations> shape_of{};
    std::array<std::pair<int, int>, orientations> spawn_off{};
    std::vector<KickRule> kicks;
};

template <auto B>
PieceGeometry<B> make_geometry()
{
    PieceGeometry<B> geo;
    static_for<B.orientations>([&](auto i) {
        constexpr auto entry = B.mino_index[i];
        constexpr auto shape = index_c<entry[0_szc]>;
        constexpr auto mino = B.minos[shape];
        constexpr auto off = entry[1_szc];
        geo.shape_of[i] = int(shape);
        geo.spawn_off[i] = {off[0_szc], off[1_szc]};
        geo.cell_count[i] = static_cast<int>(std::tuple_size_v<decltype(mino)>);
        static_for<std::tuple_size_v<decltype(mino)>>([&](auto k) {
            constexpr auto cell = mino[k];
            geo.cell_x[i][k] = cell[0_szc];
            geo.cell_y[i][k] = cell[1_szc];
        });
    });
    static_for<std::tuple_size_v<decltype(B.kicks)>>([&](auto j) {
        constexpr auto entry = B.kicks[j];
        constexpr auto diff = entry[0_szc];
        constexpr auto table = entry[1_szc];
        KickRule rule;
        rule.from = diff[0_szc];
        rule.to = diff[1_szc];
        static_for<std::tuple_size_v<decltype(table)>>([&](auto k) {
            constexpr auto kick = table[k];
            rule.offsets.emplace_back(kick[0_szc], kick[1_szc]);
        });
        geo.kicks.push_back(rule);
    });
    return geo;
}

// Test-only scalar BFS over (orientation, x, y, arrival). Independent of
// binary_bfs traversal; shares only the compile-time geometry tables.
template <auto B>
struct ScalarOracle
{
    static constexpr int orientations = B.orientations;
    static constexpr int height = 48;
    static constexpr int width = 10;

    PieceGeometry<B> const &geo;
    ScalarConfig const &cfg;
    std::array<uint16_t, height> const &rows;
    uint8_t visited[orientations][width][height] = {};

    bool fits(int o, int x, int y) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
        {
            return false;
        }
        for (int k = 0; k < geo.cell_count[o]; ++k)
        {
            int cx = x + geo.cell_x[o][k];
            int cy = y + geo.cell_y[o][k];
            if (cx < 0 || cx >= width || cy < 0)
            {
                return false;
            }
            if (cy >= height)
            {
                continue;
            }
            if ((rows[cy] >> cx) & 1)
            {
                return false;
            }
        }
        return true;
    }

    void visit(int o, int x, int y, int channel)
    {
        visited[o][x][y] |= static_cast<uint8_t>(1 << channel);
    }

    bool visited_any(int o, int x, int y) const
    {
        return visited[o][x][y] != 0;
    }

    void drop_to_rest(int o, int &y, int x)
    {
        while (fits(o, x, y - 1))
        {
            --y;
        }
    }

    void run(coord start, unsigned init_rot)
    {
        bool const allow_float = cfg.allow_softdrop && !cfg.allow_20g;
        bool const sonicmode = cfg.allow_sonicdrop || cfg.allow_20g;
        bool const grounded = !allow_float && sonicmode;
        bool const phase_a = !cfg.allow_20g || allow_float;
        auto spawn_pose = [&](int o) {
            return std::pair{start[0_szc] + geo.spawn_off[o].first,
                std::min(start[1_szc] + geo.spawn_off[o].second, height - 1)};
        };
        auto seed = [&](int o) {
            auto [px, py] = spawn_pose(o);
            if (!fits(o, px, py))
            {
                return;
            }
            if (cfg.allow_20g)
            {
                drop_to_rest(o, py, px);
            }
            visit(o, px, py, 0);
        };
        auto [init_sx, init_sy] = spawn_pose(static_cast<int>(init_rot));
        if (!fits(static_cast<int>(init_rot), init_sx, init_sy))
        {
            return;
        }
        seed(static_cast<int>(init_rot));

        auto run_phase = [&](bool settle_steps) {
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (int o = 0; o < orientations; ++o)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        for (int y = 0; y < height; ++y)
                        {
                            if (!visited_any(o, x, y))
                            {
                                continue;
                            }
                            auto try_visit = [&](int oo, int xx, int yy, int channel) {
                                if (!fits(oo, xx, yy))
                                {
                                    return;
                                }
                                uint8_t bit = static_cast<uint8_t>(1 << channel);
                                if (settle_steps)
                                {
                                    int ny = yy;
                                    drop_to_rest(oo, ny, xx);
                                    if (channel == 1 && ny != yy)
                                    {
                                        channel = 0;
                                        bit = 1;
                                    }
                                    yy = ny;
                                }
                                if ((visited[oo][xx][yy] & bit) == 0)
                                {
                                    visited[oo][xx][yy] |= bit;
                                    changed = true;
                                }
                            };
                            if (allow_float)
                            {
                                try_visit(o, x - 1, y, 0);
                                try_visit(o, x + 1, y, 0);
                                try_visit(o, x, y - 1, 0);
                            }
                            else
                            {
                                try_visit(o, x - 1, y, 0);
                                try_visit(o, x + 1, y, 0);
                            }
                            for (auto const &rule : geo.kicks)
                            {
                                if (rule.from != o)
                                {
                                    continue;
                                }
                                if ((rule.from + 2) % 4 == rule.to && !cfg.allow_180)
                                {
                                    continue;
                                }
                                for (auto const &[dx, dy] : rule.offsets)
                                {
                                    if (fits(rule.to, x + dx, y + dy))
                                    {
                                        try_visit(rule.to, x + dx, y + dy, 1);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };
        auto finalize = [&]() {
            uint8_t settled[orientations][width][height] = {};
            for (int o = 0; o < orientations; ++o)
            {
                for (int x = 0; x < width; ++x)
                {
                    for (int y = 0; y < height; ++y)
                    {
                        uint8_t v = visited[o][x][y];
                        if (v == 0)
                        {
                            continue;
                        }
                        int ny = y;
                        drop_to_rest(o, ny, x);
                        if ((v & 1) != 0)
                        {
                            settled[o][x][ny] |= 1;
                        }
                        if ((v & 2) != 0)
                        {
                            settled[o][x][ny] |= (ny == y) ? 2 : 1;
                        }
                    }
                }
            }
            for (int o = 0; o < orientations; ++o)
            {
                for (int x = 0; x < width; ++x)
                {
                    for (int y = 0; y < height; ++y)
                    {
                        visited[o][x][y] = settled[o][x][y];
                    }
                }
            }
        };
        if (phase_a)
        {
            run_phase(false);
        }
        if (!allow_float)
        {
            finalize();
        }
        if (grounded)
        {
            run_phase(true);
        }
    }

    // landable visited states as per-orientation 48-row bitboard words
    std::array<std::array<uint64_t, 8>, orientations> landable_words(int channel, bool allow_float) const
    {
        std::array<std::array<uint64_t, 8>, orientations> words = {};
        for (int o = 0; o < orientations; ++o)
        {
            for (int x = 0; x < width; ++x)
            {
                for (int y = 0; y < height; ++y)
                {
                    uint8_t v = visited[o][x][y];
                    if ((v & (1 << channel)) == 0)
                    {
                        continue;
                    }
                    if (allow_float && fits(o, x, y - 1))
                    {
                        continue;
                    }
                    words[o][y / 6] |= uint64_t(1) << ((y % 6) * 10 + x);
                }
            }
        }
        return words;
    }
};

bool compare_with_bitpar(auto const &result, auto const &oracle_normal, auto const &oracle_rotation, std::string const &what)
{
    constexpr int orientations = std::remove_reference_t<decltype(result)>::orientations;
    bool ok = true;
    for (int o = 0; o < orientations; ++o)
    {
        for (int w = 0; w < 8; ++w)
        {
            ok = ok && uint64_t(result.normal_landings[o].logical_word(w)) == uint64_t(oracle_normal[o][w]);
            ok = ok && uint64_t(result.rotation_landings[o].logical_word(w)) == uint64_t(oracle_rotation[o][w]);
        }
    }
    if (!ok)
    {
        std::println(stderr, "comparison failed: {}", what);
    }
    return ok;
}


std::vector<std::array<uint16_t, 48>> make_corpus()
{
    std::vector<std::array<uint16_t, 48>> boards;
    boards.push_back({});
    std::mt19937 rng(20260905u);
    for (unsigned height : {2u, 5u, 10u, 18u, 25u, 32u, 40u, 44u})
    {
        for (int variant = 0; variant < 3; ++variant)
        {
            std::array<uint16_t, 48> rows = {};
            for (unsigned y = 0; y < height; ++y)
            {
                rows[y] = static_cast<uint16_t>(rng()) & 0x3ff;
                if (rows[y] == 0x3ff)
                {
                    rows[y] &= 0x1ff;
                }
            }
            boards.push_back(rows);
        }
    }
    {
        std::array<uint16_t, 48> flat = {};
        for (int y = 0; y < 10; ++y)
        {
            flat[y] = 0x2aa;
        }
        boards.push_back(flat);
    }
    {
        std::array<uint16_t, 48> near_top = {};
        for (int y = 38; y < 46; ++y)
        {
            near_top[y] = 0x2aa;
        }
        boards.push_back(near_top);
    }
    {
        std::array<uint16_t, 48> fringe_a = {};
        fringe_a[18] = 0x010;
        fringe_a[19] = 0x028;
        boards.push_back(fringe_a);
    }
    {
        std::array<uint16_t, 48> fringe_b = {};
        fringe_b[19] = 0x020;
        fringe_b[21] = 0x008;
        boards.push_back(fringe_b);
    }
    for (int variant = 0; variant < 8; ++variant)
    {
        std::array<uint16_t, 48> rows = {};
        for (int y = 0; y < 15; ++y)
        {
            rows[y] = static_cast<uint16_t>(rng()) & 0x3ff;
            if (rows[y] == 0x3ff)
            {
                rows[y] &= 0x1ff;
            }
        }
        unsigned density = 12u + unsigned(variant) * 4u;
        for (int y = 15; y <= 23; ++y)
        {
            uint16_t mask = 0;
            for (int x = 1; x <= 8; ++x)
            {
                if (rng() % 100u < density)
                {
                    mask |= static_cast<uint16_t>(1u << x);
                }
            }
            rows[y] = mask;
        }
        boards.push_back(rows);
    }
    return boards;
}

} // namespace scalar_arrival
