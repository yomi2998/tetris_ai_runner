#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <random>
#include <string>

namespace tuner_toj
{
    class Tuner
    {
    public:
        static constexpr size_t NUM_PARAMS = 29;
        static constexpr int NEXT_LENGTH = 6;

        static constexpr double param_scale[NUM_PARAMS] = {
            0.17, 2.8, 0.31, 0.97, 6.3, 6.8, 0.43, 0.18, 7.3, 8.15,
            0.037, 2.64, 1.8, 0.00085, 0.0012, 1.4, 0.31, 0.24, 0.99, 0.48,
            0.70, 0.0092, 0.058, 1.3, 0.22, 0.26, 0.59, 0.94, 0.68,
        };

        static inline char const *const param_names[NUM_PARAMS] = {
            "base", "roof", "col_trans", "row_trans", "hole_count", "hole_line",
            "clear_width", "wide_2", "wide_3", "wide_4", "safe", "b2b", "attack",
            "hold_t", "hold_i", "waste_t", "waste_i", "clear_1", "clear_2",
            "clear_3", "clear_4", "t2_slot", "t3_slot", "tspin_mini", "tspin_1",
            "tspin_2", "tspin_3", "combo", "ratio",
        };

        static constexpr int combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
        static constexpr int combo_table_max = 10;

        static uint64_t splitmix64(uint64_t x)
        {
            x += 0x9E3779B97F4A7C15ULL;
            x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
            x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
            return x ^ (x >> 31);
        }

        static int rademacher(uint64_t seed, int k, int j, int i)
        {
            uint64_t h = splitmix64(seed ^ splitmix64(static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ULL
                                                      + static_cast<uint64_t>(j) * 0xBF58476D1CE4E5B9ULL
                                                      + static_cast<uint64_t>(i)));
            return static_cast<int>(h & 1) ? 1 : -1;
        }

        struct Scenario
        {
            std::deque<char> pieces;
            uint64_t pair_seed = 0;
            int round = 0;
            int packet_index = 0;
        };

        static void begin_round(Scenario &s1, Scenario &s2, int round)
        {
            s1.round = round;
            s2.round = round;
            s1.packet_index = 0;
            s2.packet_index = 0;
        }

        static int scenario_hole(Scenario const &s)
        {
            uint64_t h = splitmix64(s.pair_seed
                ^ splitmix64(static_cast<uint64_t>(s.round) * 0x9E3779B97F4A7C15ULL
                             + static_cast<uint64_t>(s.packet_index) * 0x94D049BB133111EBULL));
            return static_cast<int>(h % 10);
        }

        static Scenario make_scenario(uint64_t seed, size_t max_rounds, size_t next_len)
        {
            std::mt19937 rng(static_cast<unsigned>(splitmix64(seed)));
            std::string bag = "IJLOSTZ";
            Scenario s;

            size_t pieces_needed = max_rounds * 2 + next_len * 2 + 4;
            while (s.pieces.size() < pieces_needed)
            {
                std::shuffle(bag.begin(), bag.end(), rng);
                for (char c : bag)
                {
                    s.pieces.push_back(c);
                }
            }

            s.pair_seed = seed;
            return s;
        }
    };
}
