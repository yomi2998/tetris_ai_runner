#include "scalar_arrival_oracle.h"

#include <chrono>
#include <print>
#include <vector>

using namespace reachability;
using BOARD = board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

namespace {

BOARD board_from_rows(std::array<uint16_t, 48> const &rows)
{
    BOARD board;
    std::array<BOARD::row_t, 48> clipped = {};
    for (int y = 0; y < 48; ++y)
    {
        clipped[y] = rows[y];
    }
    board.from_row_bitboard<true>(clipped);
    return board;
}

}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? std::atoi(argv[1]) : 30;
    int warmup = argc > 2 ? std::atoi(argv[2]) : 5;
    auto boards = scalar_arrival::make_corpus();
    coord const spawn{4, 20};
    search::search_config cfg{};
    cfg.allow_180 = true;
    cfg.allow_softdrop = true;
    cfg.allow_sonicdrop = true;
    cfg.allow_20g = false;
    for (int w = 0; w < warmup; ++w)
    {
        for (char name : std::string_view("TZSJLOI"))
        {
            call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
                for (auto const &rows : boards)
                {
                    BOARD board = board_from_rows(rows);
                    volatile auto r = search::template binary_bfs<B>(board, cfg, spawn, 0);
                    (void)r;
                }
                return 0;
            });
        }
    }
    for (char name : std::string_view("TZSJLOI"))
    {
        call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
            size_t sink = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < iters; ++i)
            {
                for (auto const &rows : boards)
                {
                    BOARD board = board_from_rows(rows);
                    auto r = search::template binary_bfs<B>(board, cfg, spawn, 0);
                    for (auto const &bb : r)
                    {
                        sink += bb.popcount();
                    }
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / (iters * boards.size());
            std::println("{} ns={:.0f} sink={}", name, ns, sink);
            return 0;
        });
    }
    return 0;
}
