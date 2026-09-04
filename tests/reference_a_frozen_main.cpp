#include "candidate_format.h"
#include "producer_info.h"
#include "reach_corpus.h"

#include "games/toj/geometry.hpp"
#include "games/toj/types.hpp"
#include "search/reachability_search.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <string_view>
#include <vector>

namespace {

using tet::core::BlockType;
using tet::core::PackedPlacement;
using tet::search::ReachabilitySearch;
using Board = tet::toj::Board;

struct Options
{
    int reps = 5;
    int warmup = 2;
    bool allow_180 = true;
    bool dump_keys = false;
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
        else if (arg == "--dump-keys")
        {
            o.dump_keys = true;
        }
    }
    return o;
}

BlockType block_for(char piece)
{
    switch (piece) {
    case 'I':
        return BlockType::I;
    case 'O':
        return BlockType::O;
    case 'T':
        return BlockType::T;
    case 'J':
        return BlockType::J;
    case 'L':
        return BlockType::L;
    case 'S':
        return BlockType::S;
    default:
        return BlockType::Z;
    }
}

Board board_from_rows(std::array<uint16_t, reach_corpus::height> const &rows)
{
    std::array<std::uint64_t, reach_corpus::height> words{};
    for (std::size_t y = 0; y < rows.size(); ++y) {
        words[y] = rows[y];
    }
    return Board::from_rows(words);
}

ReachabilitySearch::Config config_for(bool allow_180)
{
    ReachabilitySearch::Config config{};
    config.allow_180 = allow_180;
    config.allow_softdrop = true;
    config.allow_sonicdrop = true;
    config.allow_20g = false;
    return config;
}

std::size_t normalize(ReachabilitySearch &search, Board const &board, BlockType block,
    PackedPlacement start, candfmt::Report *report, std::size_t board_index, char piece)
{
    auto const candidates = search.enumerate(board, block, start);
    std::size_t raw = 0;
    if (report != nullptr) {
        report->begin_case(piece, board_index, piece == 'T');
    }
    for (auto const &candidate : candidates) {
        ++raw;
        if (report == nullptr) {
            continue;
        }
        auto const &cells = tet::toj::geometry::cells_for(block, candidate.placement.rotation());
        candfmt::Cells pose;
        for (int i = 0; i < 4; ++i) {
            pose[i] = {static_cast<int>(candidate.placement.x()) + cells[i].x,
                static_cast<int>(candidate.placement.y()) + cells[i].y};
        }
        int const arrival = candidate.arrival.has_terminal_rotation() ? 1 : 0;
        report->add_candidate(candfmt::occupancy_hash(pose), arrival);
    }
    if (report != nullptr) {
        report->end_case();
    }
    return raw;
}

} // namespace

int main(int argc, char **argv)
{
    Options const opts = parse_args(argc, argv);
    producer_info::print("reference_a_frozen");
    auto const boards = reach_corpus::make();
    PackedPlacement const start{reach_corpus::spawn_x, reach_corpus::spawn_y, 0};

    ReachabilitySearch search;
    search.configure(config_for(opts.allow_180));
    tet::toj::RuleInfo info{};
    info.width = reach_corpus::width;
    info.height = reach_corpus::height;
    for (std::size_t i = 0; i < info.spawns.size(); ++i) {
        info.spawns[i] = start;
    }
    search.init(info);

    candfmt::Report report;
    report.set_dump(opts.dump_keys);
    for (char piece : std::string_view(reach_corpus::pieces)) {
        for (std::size_t b = 0; b < boards.size(); ++b) {
            Board board = board_from_rows(boards[b]);
            normalize(search, board, block_for(piece), start, &report, b, piece);
        }
    }
    report.print_corpus("reference_a_frozen");

    for (int rep = -opts.warmup; rep < opts.reps; ++rep) {
        for (char piece : std::string_view(reach_corpus::pieces)) {
            std::size_t sink = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (std::size_t b = 0; b < boards.size(); ++b) {
                Board board = board_from_rows(boards[b]);
                sink += normalize(search, board, block_for(piece), start, nullptr, b, piece);
            }
            auto t1 = std::chrono::steady_clock::now();
            double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
                / static_cast<double>(boards.size());
            if (rep >= 0) {
                std::println("REP {} {} {:016x} {:9.1f} reference_a_frozen", rep, piece, sink, ns);
            }
        }
    }
    return 0;
}
