#pragma once

#include "kick_srs.hpp"
#include "piece_tetromino.hpp"
#include "search.hpp"
#include "utils.hpp"
#include "board.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

using reachability::operator""_szc;
using namespace reachability::rules;

using BOARD = reachability::board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

inline uint64_t perft(BOARD b, const char* block, unsigned depth, unsigned height = 0, reachability::search::search_config cfg = {}) {
    return reachability::call_with_block<SRS>(Tetromino::from_name(*block), [&]<reachability::block B> [[gnu::always_inline]] () {
        uint64_t n = 0;
        constexpr int downmost = reachability::search::downmost_position<B>;
        b.call_with_height<reachability::tuple{6, 12, 24, 48}>(height + 3, [&] [[gnu::always_inline]] (auto nb) {
            constexpr reachability::coord spawn_pos = reachability::coord{4, 20};
            constexpr int necessary_height = spawn_pos[1_szc] + downmost;
            std::array<decltype(nb), B.shapes> reachable;
            if constexpr (nb.height < necessary_height) {
                reachable = reachability::search::binary_bfs<B, false>(nb, cfg, spawn_pos, 0);
            } else {
                bool check_consecutive = height > necessary_height;
                if (check_consecutive) [[unlikely]] {
                    reachable = reachability::search::binary_bfs<B, true>(nb, cfg, spawn_pos, 0);
                } else {
                    reachable = reachability::search::binary_bfs<B, false>(nb, cfg, spawn_pos, 0);
                }
            }
            if (depth == 1) {
                for (std::size_t rot = 0; rot < reachable.size(); ++rot)
                    n += reachable[rot].popcount();
                return;
            }
            reachability::static_for<B.shapes>([&] [[gnu::always_inline]] (auto rot) {
                constexpr auto mino = B.minos[rot];
                constexpr auto range = reachability::mino_range<mino>();
                constexpr auto max_y = range[3];
                reachable[rot].for_each_bit([&] [[gnu::always_inline]] (int x, int y) {
                    BOARD new_board = b | BOARD::put<mino>(x, y);
                    auto result = new_board.clear_full_lines();
                    unsigned new_height = std::max(height, unsigned(y + max_y + 1)) - result.count;
                    n += perft(result.board, block + 1, depth - 1, new_height, cfg);
                });
            });
        });
        return n;
    });
}

inline std::pair<uint64_t, uint64_t> perft_with_time(BOARD b, const char* block, unsigned depth) {
    constexpr auto cfg = reachability::search::search_config{false, true, true};
    const auto start = std::chrono::high_resolution_clock::now();
    uint64_t nodes = perft(b, block, depth, 0, cfg);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return {nodes, dt};
}

inline std::pair<uint64_t, uint64_t> perft_with_time(BOARD b, const char* block, unsigned depth, reachability::search::search_config cfg) {
    const auto start = std::chrono::high_resolution_clock::now();
    uint64_t nodes = perft(b, block, depth, 0, cfg);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return {nodes, dt};
}
