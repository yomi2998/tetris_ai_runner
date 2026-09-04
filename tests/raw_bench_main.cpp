#include "board.hpp"
#include "block.hpp"
#include "kick_srs.hpp"
#include "piece_tetromino.hpp"
#include "producer_info.h"
#include "reach_corpus.h"
#include "search.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <span>
#include <string_view>
#include <vector>

using namespace reachability;
using namespace reachability::rules;
using BOARD = board_t<10, 48>;
using SRS = rule_set<Tetromino, SRS_Kicks>;

namespace {

struct Options
{
    int reps = 5;
    int warmup = 2;
    std::string_view producer = "unknown";
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
        else if (arg == "--producer" && i + 1 < argc)
        {
            o.producer = argv[++i];
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

uint64_t fnv_mix(uint64_t hash, uint64_t value)
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & 0xffu;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

uint64_t landings_sum(BOARD const *boards, std::size_t number)
{
    uint64_t sum = 0;
    for (std::size_t index = 0; index < number; ++index)
    {
        std::size_t row = 0;
        for (auto value : boards[index].to_row_bitboard())
        {
            sum += static_cast<uint64_t>(value) * (row + 1);
            ++row;
        }
    }
    return sum;
}

uint64_t landings_hash(BOARD const *boards, std::size_t number, std::size_t &count, uint64_t salt)
{
    uint64_t hash = fnv_mix(0xcbf29ce484222325ull, salt);
    for (std::size_t index = 0; index < number; ++index)
    {
        auto const &board = boards[index];
        for (auto row : board.to_row_bitboard())
        {
            hash = fnv_mix(hash, static_cast<uint64_t>(row));
            count += static_cast<std::size_t>(__builtin_popcountll(static_cast<uint64_t>(row)));
        }
    }
    return hash;
}

std::vector<BOARD> prepared(std::vector<std::array<uint16_t, 48>> const &boards)
{
    std::vector<BOARD> result;
    result.reserve(boards.size());
    for (auto const &rows : boards)
    {
        result.push_back(board_from_rows(rows));
    }
    return result;
}

template <auto B>
std::uint64_t run_case(BOARD const &board, search::search_config const &cfg, std::size_t &count, uint64_t salt)
{
    auto const landings = search::template binary_bfs<B>(board, cfg, coord{reach_corpus::spawn_x, reach_corpus::spawn_y}, 0);
    return landings_hash(landings.data(), landings.size(), count, salt);
}

template <auto B>
std::array<BOARD, 4> store_landings(BOARD const &board, search::search_config const &cfg)
{
    auto const landings = search::template binary_bfs<B>(board, cfg, coord{reach_corpus::spawn_x, reach_corpus::spawn_y}, 0);
    std::array<BOARD, 4> stored{};
    for (std::size_t i = 0; i < landings.size(); ++i)
    {
        stored[i] = landings[i];
    }
    return stored;
}

template <auto B>
double run_rep(std::vector<BOARD> const &boards, search::search_config const &cfg, std::size_t &sink, int rep)
{
    auto t0 = std::chrono::steady_clock::now();
    for (auto const &board : boards)
    {
        auto const landings = search::template binary_bfs<B>(board, cfg, coord{reach_corpus::spawn_x, reach_corpus::spawn_y}, 0);
        sink += static_cast<std::size_t>(landings_sum(landings.data(), landings.size()) >> rep);
    }
    auto t1 = std::chrono::steady_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / static_cast<double>(boards.size());
}

template <auto B>
double run_extract_rep(std::vector<std::array<BOARD, 4>> const &stored, std::size_t &sink, int rep)
{
    auto t0 = std::chrono::steady_clock::now();
    for (auto const &entry : stored)
    {
        sink += static_cast<std::size_t>(landings_sum(entry.data(), B.shapes) >> rep);
    }
    auto t1 = std::chrono::steady_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
        / static_cast<double>(stored.size());
}

} // namespace

int main(int argc, char **argv)
{
    Options const opts = parse_args(argc, argv);
    producer_info::print(opts.producer.data());
    auto const boards = prepared(reach_corpus::make());
    search::search_config cfg{};
    cfg.allow_180 = opts.allow_180;
    cfg.allow_softdrop = true;
    cfg.allow_sonicdrop = true;
    cfg.allow_20g = false;

    std::size_t const cases = std::string_view(reach_corpus::pieces).size() * boards.size();
    std::size_t corpus_count = 0;
    uint64_t corpus_hash = 0xcbf29ce484222325ull;
    for (char piece : std::string_view(reach_corpus::pieces))
    {
        call_with_block<SRS>(Tetromino::from_name(piece), [&]<block B>() {
            for (std::size_t b = 0; b < boards.size(); ++b)
            {
                std::size_t count = 0;
                auto const hash = run_case<B>(boards[b], cfg, count, 0);
                std::println("RAWCASE {} {:>3} {:>5} {:016x} {}", piece, b, count, hash, opts.producer);
                corpus_hash = fnv_mix(fnv_mix(corpus_hash, hash), static_cast<uint64_t>(count));
                corpus_count += count;
            }
            return 0;
        });
    }
    std::println("RAWCORPUS {} cases {} bits {} {:016x}", opts.producer, cases, corpus_count, corpus_hash);

    for (int rep = -opts.warmup; rep < opts.reps; ++rep)
    {
        for (char piece : std::string_view(reach_corpus::pieces))
        {
            call_with_block<SRS>(Tetromino::from_name(piece), [&]<block B>() {
                std::vector<std::array<BOARD, 4>> stored;
                stored.reserve(boards.size());
                for (auto const &board : boards)
                {
                    stored.push_back(store_landings<B>(board, cfg));
                }
                std::size_t sink = 0;
                double ns = run_rep<B>(boards, cfg, sink, rep);
                std::size_t extract_sink = 0;
                double extract_ns = run_extract_rep<B>(stored, extract_sink, rep);
                if (rep >= 0)
                {
                    std::println("RAWREP {} {} {:016x} {:9.1f} {:9.1f} {:016x} {}", rep, piece, sink, ns, extract_ns, extract_sink, opts.producer);
                }
                return 0;
            });
        }
    }
    return 0;
}
