#include "candidate_format.h"
#include "producer_info.h"
#include "reach_corpus.h"
#include "perft.hpp"

#include <chrono>
#include <cstdlib>
#include <print>
#include <string_view>
#include <vector>

using namespace reachability;
using BOARD = board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

namespace {

struct Options
{
    int reps = 5;
    int warmup = 2;
    bool allow_180 = true;
};

Options parse_args(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--reps" && i + 1 < argc)
        {
            o.reps = std::atoi(argv[++i]);
        }
        else if (arg == "--warmup" && i + 1 < argc)
        {
            o.warmup = std::atoi(argv[++i]);
        }
        else if (arg == "--no-180")
        {
            o.allow_180 = false;
        }
    }
    return o;
}

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
std::size_t enumerate_normalized(BOARD const &board, search::search_config const &cfg, candfmt::Report *report, std::size_t board_index, char piece)
{
    search::search_workspace<B, BOARD> ws(board);
    auto result = search::template arrival_search<B>(ws, cfg, coord{reach_corpus::spawn_x, reach_corpus::spawn_y}, 0);
    std::size_t raw = 0;
    if (report != nullptr)
    {
        report->begin_case(piece, board_index, piece == 'T');
    }
    static_for<B.orientations>([&](auto i) {
        constexpr auto shape = index_c<B.mino_index[i][0_szc]>;
        constexpr auto mino = B.minos[shape];
        auto emit = [&](auto const &mask, int channel) {
            mask.for_each_bit([&](int x, int y) {
                ++raw;
                if (report == nullptr)
                {
                    return;
                }
                candfmt::Cells cells;
                static_for<4>([&](auto j) {
                    cells[j] = {x + mino[j][0_szc], y + mino[j][1_szc]};
                });
                report->add_candidate(candfmt::occupancy_hash(cells), channel);
            });
        };
        emit(result.normal_landings[i], 0);
        emit(result.rotation_landings[i], 1);
    });
    if (report != nullptr)
    {
        report->end_case();
    }
    return raw;
}

template <auto B>
void run_report(candfmt::Report &report, std::vector<std::array<uint16_t, 48>> const &boards, search::search_config const &cfg, char piece)
{
    for (std::size_t b = 0; b < boards.size(); ++b)
    {
        BOARD board = board_from_rows(boards[b]);
        enumerate_normalized<B>(board, cfg, &report, b, piece);
    }
}

template <auto B>
double run_timed(std::vector<std::array<uint16_t, 48>> const &boards, search::search_config const &cfg, std::size_t &sink)
{
    auto t0 = std::chrono::steady_clock::now();
    for (auto const &rows : boards)
    {
        BOARD board = board_from_rows(rows);
        sink += enumerate_normalized<B>(board, cfg, nullptr, 0, ' ');
    }
    auto t1 = std::chrono::steady_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / static_cast<double>(boards.size());
}

} // namespace

int main(int argc, char **argv)
{
    Options const opts = parse_args(argc, argv);
    producer_info::print("current_arrival");
    auto const boards = reach_corpus::make();
    search::search_config cfg{};
    cfg.allow_180 = opts.allow_180;
    cfg.allow_softdrop = true;
    cfg.allow_sonicdrop = true;
    cfg.allow_20g = false;

    candfmt::Report report;
    for (char piece : std::string_view(reach_corpus::pieces))
    {
        call_with_block<SRS>(Tetromino::from_name(piece), [&]<block B>() {
            run_report<B>(report, boards, cfg, piece);
            return 0;
        });
    }
    report.print_corpus("current_arrival");

    for (int rep = -opts.warmup; rep < opts.reps; ++rep)
    {
        for (char piece : std::string_view(reach_corpus::pieces))
        {
            call_with_block<SRS>(Tetromino::from_name(piece), [&]<block B>() {
                std::size_t sink = 0;
                double ns = run_timed<B>(boards, cfg, sink);
                if (rep >= 0)
                {
                    std::println("REP {} {} {:016x} {:9.1f} current_arrival", rep, piece, sink, ns);
                }
                return 0;
            });
        }
    }
    return 0;
}
