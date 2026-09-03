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
std::vector<refa::Candidate> reference_a_candidates(
    scalar_arrival::PieceGeometry<B> const &geo,
    search::search_config const &cfg,
    std::array<uint16_t, 48> const &rows,
    coord spawn)
{
    BOARD board = board_from_rows(rows);
    auto reachable = search::template binary_bfs<B>(board, cfg, spawn, 0);
    scalar_arrival::ScalarOracle<B> oracle{geo, scalar_arrival::ScalarConfig{cfg.allow_180, cfg.allow_softdrop, cfg.allow_sonicdrop, cfg.allow_20g}, rows};
    oracle.run(spawn, 0);
    bool const allow_float = cfg.allow_softdrop && !cfg.allow_20g;
    auto ow = oracle.landable_words(0, allow_float);
    auto orw = oracle.landable_words(1, allow_float);
    for (int s = 0; s < B.shapes; ++s)
    {
        BOARD union_board{};
        for (int o = 0; o < B.orientations; ++o)
        {
            if (geo.shape_of[o] != s)
            {
                continue;
            }
            for (int y = 0; y < 48; ++y)
            {
                int yi = y / 6;
                int off = (y % 6) * 10;
                uint16_t both = static_cast<uint16_t>(((ow[o][yi] | orw[o][yi]) >> off) & 0x3ff);
                for (int x = 0; x < 10; ++x)
                {
                    if ((both >> x) & 1)
                    {
                        union_board.set(x, y);
                    }
                }
            }
        }
        if (!(union_board == reachable[s]))
        {
            std::println(stderr, "reference-A union mismatch shape{}", s);
        }
    }
    std::vector<refa::Candidate> out;
    for (int o = 0; o < B.orientations; ++o)
    {
        for (int y = 0; y < 48; ++y)
        {
            int yi = y / 6;
            int off = (y % 6) * 10;
            uint16_t n = static_cast<uint16_t>((ow[o][yi] >> off) & 0x3ff);
            uint16_t r = static_cast<uint16_t>((orw[o][yi] >> off) & 0x3ff);
            for (int x = 0; x < 10; ++x)
            {
                if ((n >> x) & 1)
                {
                    out.push_back(refa::Candidate{static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(o), 0});
                }
                if ((r >> x) & 1)
                {
                    out.push_back(refa::Candidate{static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(o), 1});
                }
            }
        }
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
                auto geo = scalar_arrival::make_geometry<B>();
                for (auto const &rows : boards)
                {
                    volatile auto c = reference_a_candidates<B>(geo, cfg, rows, spawn);
                    (void)c;
                }
                return 0;
            });
        }
    }
    for (char name : std::string_view("TZSJLOI"))
    {
        call_with_block<SRS>(Tetromino::from_name(name), [&]<block B>() {
            auto geo = scalar_arrival::make_geometry<B>();
            std::vector<refa::Candidate> first;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < iters; ++i)
            {
                for (auto const &rows : boards)
                {
                    auto c = reference_a_candidates<B>(geo, cfg, rows, spawn);
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
