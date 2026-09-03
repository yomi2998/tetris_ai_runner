#include "scalar_arrival_oracle.h"
#include "candidate_format.h"

#include <algorithm>
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

template <auto B>
std::vector<refa::Candidate> arrival_candidates(
    search::search_workspace<B, BOARD> const &ws,
    search::search_config const &cfg,
    coord spawn)
{
    auto result = search::template arrival_search<B>(ws, cfg, spawn, 0);
    std::vector<refa::Candidate> out;
    for (int o = 0; o < B.orientations; ++o)
    {
        auto dump = [&](auto const &bb, uint8_t arrival) {
            bb.for_each_bit([&](int x, int y) {
                out.push_back(refa::Candidate{static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(o), arrival});
            });
        };
        dump(result.normal_landings[o], 0);
        dump(result.rotation_landings[o], 1);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? std::atoi(argv[1]) : 20;
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
                    search::search_workspace<B, BOARD> ws(board);
                    volatile auto c = arrival_candidates<B>(ws, cfg, spawn);
                    (void)c;
                }
                return 0;
            });
        }
    }
    for (char name : std::string_view("TZSJLOI"))
    {
        call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
            std::vector<refa::Candidate> first;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < iters; ++i)
            {
                for (auto const &rows : boards)
                {
                    BOARD board = board_from_rows(rows);
                    search::search_workspace<B, BOARD> ws(board);
                    auto c = arrival_candidates<B>(ws, cfg, spawn);
                    if (i == 0 && first.empty())
                    {
                        first = c;
                    }
                }
            }
            auto t1 = std::chrono::steady_clock::now();
            double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / (iters * boards.size());
            std::sort(first.begin(), first.end());
            std::println("{} count={} hash={} ns={:.0f}", name, first.size(), refa::hex64(refa::fnv1a(first)), ns);
            return 0;
        });
    }
    return 0;
}
