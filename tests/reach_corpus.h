#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

namespace reach_corpus
{

inline constexpr int width = 10;
inline constexpr int height = 48;
inline constexpr int spawn_x = 4;
inline constexpr int spawn_y = 20;
inline constexpr char const *pieces = "TZSJLOI";

struct MovementConfig
{
    bool allow_180 = true;
    bool allow_softdrop = true;
    bool allow_sonicdrop = true;
    bool allow_20g = false;
};

inline constexpr std::array<MovementConfig, 8> all_configs = {{
    {true, true, true, false},
    {false, true, true, false},
    {true, false, true, false},
    {true, true, false, false},
    {true, false, false, false},
    {true, false, true, true},
    {true, true, true, true},
    {false, false, false, false},
}};

inline std::vector<std::array<uint16_t, height>> make()
{
    std::vector<std::array<uint16_t, height>> boards;
    boards.push_back({});
    std::mt19937 rng(20260905u);
    for (unsigned stack_height : {2u, 5u, 10u, 18u, 25u, 32u, 40u, 44u})
    {
        for (int variant = 0; variant < 3; ++variant)
        {
            std::array<uint16_t, height> rows = {};
            for (unsigned y = 0; y < stack_height; ++y)
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
        std::array<uint16_t, height> flat = {};
        for (int y = 0; y < 10; ++y)
        {
            flat[y] = 0x2aa;
        }
        boards.push_back(flat);
    }
    {
        std::array<uint16_t, height> near_top = {};
        for (int y = 38; y < 46; ++y)
        {
            near_top[y] = 0x2aa;
        }
        boards.push_back(near_top);
    }
    {
        std::array<uint16_t, height> fringe_a = {};
        fringe_a[18] = 0x010;
        fringe_a[19] = 0x028;
        boards.push_back(fringe_a);
    }
    {
        std::array<uint16_t, height> fringe_b = {};
        fringe_b[19] = 0x020;
        fringe_b[21] = 0x008;
        boards.push_back(fringe_b);
    }
    for (int variant = 0; variant < 8; ++variant)
    {
        std::array<uint16_t, height> rows = {};
        for (int y = 0; y < 15; ++y)
        {
            rows[y] = static_cast<uint16_t>(rng()) & 0x3ff;
            if (rows[y] == 0x3ff)
            {
                rows[y] &= 0x1ff;
            }
        }
        unsigned density = 12u + static_cast<unsigned>(variant) * 4u;
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

} // namespace reach_corpus
