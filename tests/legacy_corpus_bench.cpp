// Frozen legacy corpus comparator for phase-7 gates 8/9 (test-only).
//
// One legacy engine per process: prepare(10, 40), 256 MiB limit, the frozen
// search_config (allow_rotate_move=false, allow_180=true, allow_d=true,
// allow_D=true, allow_LR=true, allow_nont_d=false, is_20g=false,
// last_rotate=false), one search_tspin::Search init(context, config).
// TETRIS_LEGACY_CMP is never defined here, so zero hook code compiles; the
// binary links the same flag-off legacy sources as the frozen baseline.
//
// Untimed normalization pass (CASE/CORPUS/UNMATCHED lines): for every case
// in fixed order (piece outer, board inner) drive a fresh TetrisMap through
// search(map, node, 1) and feed every land point through
// legacy_cmp::normalize_land_point into a candfmt::Report with the
// arrival-class channel used by the corpus harness (T: keep_arrival=true,
// arrival = is_last_rotate ? 1 : 0; non-T: keep_arrival=false, channel 0).
// Matched keys contribute their sorted-cell occupancy hash, exactly like
// arrival_candidates. Unconvertible land points are never silently dropped:
// they enter the report under a salted FNV identity over their opaque
// status bits (distinct statuses stay distinct, matched/unmatched never
// collide by construction of the salt) and are counted into UNMATCHED.
//
// Timed pass (REP lines): warmup reps, then timed reps; per piece, one span
// over the 33 subcorpus boards, reported as nanoseconds per parent, exactly
// like arrival_candidates::run_timed. Normalization never enters the span.
//
// Subcorpus: reach_corpus::make() restricted to legacy_subcorpus_indices()
// (33 boards, 231 cases). Startup asserts every included board's max
// occupied row is <= 39 and aborts loudly otherwise. --asan-identity
// additionally emits BOARDCASE <piece> <board> <hash-of-40-row-masks> lines
// (231) for an independent identity check of the imported occupancy.

#include "candidate_format.h"
#include "legacy_cmp_normalize.h"
#include "producer_info.h"
#include "reach_corpus.h"

#include "ai_zzz.h"
#include "rule_toj.h"
#include "search_tspin.h"
#include "tetris_core.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string_view>
#include <vector>

namespace
{
    constexpr int legacy_width = 10;
    constexpr int legacy_height = 40;
    constexpr char const *default_producer = "legacy_corpus";
    constexpr std::uint64_t unmatched_salt = 0x9e3779b97f4a7c15ull;

    using Engine =
        m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    struct Options
    {
        int reps = 5;
        int warmup = 2;
        std::string_view producer = default_producer;
        bool asan_identity = false;
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
            else if (arg == "--asan-identity")
            {
                o.asan_identity = true;
            }
            else if (arg == "--legacy-subcorpus")
            {
                // Accepted for harness uniformity with arrival_candidates:
                // this binary always runs the legacy subcorpus.
            }
        }
        return o;
    }

    void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (int y = 0; y < legacy_height; ++y)
        {
            for (int x = 0; x < legacy_width; ++x)
            {
                if (map.full(static_cast<std::size_t>(x), static_cast<std::size_t>(y)))
                {
                    map.top[x] = y + 1;
                    map.roof = std::max(map.roof, y + 1);
                    ++map.count;
                }
            }
        }
    }

    std::uint64_t board_hash40(std::array<std::uint16_t, 48> const &rows)
    {
        std::uint64_t hash = candfmt::fnv_offset;
        for (int y = 0; y < legacy_height; ++y)
        {
            hash = candfmt::fnv_mix(hash, static_cast<std::uint64_t>(rows[static_cast<std::size_t>(y)]));
        }
        return hash;
    }

    int max_occupied_row(std::array<std::uint16_t, 48> const &rows)
    {
        for (int y = 47; y >= 0; --y)
        {
            if (rows[static_cast<std::size_t>(y)] != 0)
            {
                return y;
            }
        }
        return -1;
    }

    // One parent drive: fresh TetrisMap(10, 40) -> copy rows 0..39 ->
    // rebuild_metadata -> context->generate(piece) -> search(map, node, 1).
    // With report != nullptr the land points are normalized into the report
    // (untimed pass); with report == nullptr only the raw count is returned
    // (timed pass; normalization must never enter the span).
    std::size_t drive_case(search_tspin::Search &search, Engine &engine,
        std::array<std::uint16_t, 48> const &rows, char piece,
        candfmt::Report *report, std::size_t board_index, std::uint64_t &unmatched)
    {
        m_tetris::TetrisMap map(legacy_width, legacy_height);
        for (int y = 0; y < legacy_height; ++y)
        {
            map.row[y] = rows[static_cast<std::size_t>(y)];
        }
        rebuild_metadata(map);
        m_tetris::TetrisNode const *node = engine.context()->generate(piece);
        auto const *results = search.search(map, node, 1);
        if (report != nullptr)
        {
            report->begin_case(piece, board_index, piece == 'T');
            for (auto const &land : *results)
            {
                int const spin_class =
                    land.type == search_tspin::Search::TSpinType::TSpin
                    ? 1
                    : (land.type == search_tspin::Search::TSpinType::TSpinMini ? 2 : 0);
                legacy_cmp::NormalizedKey const key = legacy_cmp::normalize_land_point(
                    land->status.t, land->status.x, land->status.y,
                    land->status.r, spin_class, land.is_last_rotate,
                    land->status.status);
                int const arrival =
                    (piece == 'T' && land.is_last_rotate) ? 1 : 0;
                if (key.matched)
                {
                    report->add_candidate(key.cells_hash, arrival);
                }
                else
                {
                    ++unmatched;
                    report->add_candidate(
                        candfmt::fnv_mix(unmatched_salt,
                            static_cast<std::uint64_t>(key.opaque)),
                        arrival);
                }
            }
            report->end_case();
        }
        return results->size();
    }

    double run_timed_piece(search_tspin::Search &search, Engine &engine,
        std::vector<std::array<std::uint16_t, 48>> const &boards,
        std::vector<std::size_t> const &order, char piece, std::size_t &sink)
    {
        auto t0 = std::chrono::steady_clock::now();
        for (std::size_t b : order)
        {
            std::uint64_t unmatched = 0;
            sink += drive_case(search, engine, boards[b], piece, nullptr, b, unmatched);
        }
        auto t1 = std::chrono::steady_clock::now();
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count())
            / static_cast<double>(order.size());
    }

} // namespace

int main(int argc, char **argv)
{
    Options const opts = parse_args(argc, argv);
    producer_info::print(opts.producer.data());

    Engine engine;
    if (!engine.prepare(legacy_width, legacy_height))
    {
        std::println(stderr, "legacy_corpus_bench: engine.prepare(10, 40) failed");
        return 1;
    }
    engine.memory_limit(256ull << 20);

    engine.search_config()->allow_rotate_move = false;
    engine.search_config()->allow_180 = true;
    engine.search_config()->allow_d = true;
    engine.search_config()->allow_D = true;
    engine.search_config()->allow_LR = true;
    engine.search_config()->allow_nont_d = false;
    engine.search_config()->is_20g = false;
    engine.search_config()->last_rotate = false;

    search_tspin::Search search;
    search.init(engine.context().get(), engine.search_config());

    auto const boards = reach_corpus::make();
    std::vector<std::size_t> const order = reach_corpus::legacy_subcorpus_indices();
    if (order.size() != reach_corpus::legacy_subcorpus_boards)
    {
        std::println(stderr, "legacy_corpus_bench: subcorpus size {} != {}",
            order.size(), reach_corpus::legacy_subcorpus_boards);
        return 1;
    }
    for (std::size_t b : order)
    {
        int const top = max_occupied_row(boards[b]);
        if (top > 39)
        {
            std::println(stderr,
                "legacy_corpus_bench: board {} exceeds the legacy height: max occupied row {}",
                b, top);
            return 1;
        }
    }

    if (opts.asan_identity)
    {
        for (char piece : std::string_view(reach_corpus::pieces))
        {
            for (std::size_t b : order)
            {
                std::println("BOARDCASE {} {} {:016x}", piece, b, board_hash40(boards[b]));
            }
        }
    }

    candfmt::Report report;
    std::uint64_t unmatched = 0;
    for (char piece : std::string_view(reach_corpus::pieces))
    {
        for (std::size_t b : order)
        {
            drive_case(search, engine, boards[b], piece, &report, b, unmatched);
        }
    }
    report.print_corpus(opts.producer.data());
    std::println("UNMATCHED {} {}", opts.producer, unmatched);

    for (int rep = -opts.warmup; rep < opts.reps; ++rep)
    {
        for (char piece : std::string_view(reach_corpus::pieces))
        {
            std::size_t sink = 0;
            double const ns = run_timed_piece(search, engine, boards, order, piece, sink);
            if (rep >= 0)
            {
                std::println("REP {} {} {:016x} {:9.1f} {}", rep, piece, sink, ns, opts.producer);
            }
        }
    }
    return 0;
}
