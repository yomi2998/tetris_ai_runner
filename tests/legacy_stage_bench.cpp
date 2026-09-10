#ifdef TETRIS_LEGACY_HOST_DIAG
#endif

#include "legacy_host_diag_common.h"

#include <algorithm>
#include <chrono>
#include <print>
#include <string>
#include <vector>

namespace
{
    using namespace legacy_diag;

    struct BatchStat
    {
        double median_ns = 0;
        double min_ns = 0;
        double max_ns = 0;
        std::uint64_t drives = 0;
        std::uint64_t batch_ns = 0;
    };

    struct CellResult
    {
        std::string corpus;
        char piece = '?';
        std::string domain;
        int decile = 0;
        std::size_t cases = 0;
        std::array<BatchStat, 7> stages{};
        std::array<std::uint64_t, 7> sinks{};
        std::uint64_t unmappable = 0;
        std::uint64_t spawn_blocked = 0;
        std::uint64_t ood = 0;
        std::uint64_t not_in_graph = 0;
        std::uint64_t t_terminal_cases = 0;
    };

    struct CaseData
    {
        value_Board board{};
        std::array<std::uint16_t, 48> rows{};
        m_tetris::TetrisMap map{};
        std::uint64_t multiplicity = 1;
        BoardClass cls{};
    };

    std::uint64_t steady_stamp()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    struct CaseTimings
    {
        double median = 0;
        double min = 0;
        double max = 0;
    };

    CaseTimings summarize(std::vector<double> &samples)
    {
        std::sort(samples.begin(), samples.end());
        CaseTimings t;
        std::size_t n = samples.size();
        t.median = n % 2 == 1 ? samples[n / 2] : (samples[n / 2 - 1] + samples[n / 2]) / 2.0;
        t.min = samples.front();
        t.max = samples.back();
        return t;
    }
}

int main(int argc, char **argv)
{
    using namespace std::chrono;
    std::string out_dir;
    std::size_t drives_s1 = 200000;
    std::size_t drives_rest = 50000;
    std::size_t warmup = 3;
    std::size_t timed = 7;
    std::string inputs_path;
    std::string corpus = "both";
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&](char const *name) -> std::string {
            if (i + 1 >= argc)
            {
                std::println(stderr, "missing value for {}", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--out") out_dir = next("out");
        else if (a == "--inputs") inputs_path = next("inputs");
        else if (a == "--corpus") corpus = next("corpus");
        else if (a == "--drives-s1") drives_s1 = std::strtoull(next("drives").c_str(), nullptr, 10);
        else if (a == "--drives-rest") drives_rest = std::strtoull(next("drives").c_str(), nullptr, 10);
        else if (a == "--warmup") warmup = std::strtoull(next("warmup").c_str(), nullptr, 10);
        else if (a == "--timed") timed = std::strtoull(next("timed").c_str(), nullptr, 10);
        else
        {
            std::println(stderr, "unknown option {}", a);
            return 2;
        }
    }

    std::vector<CaseData> c1_cases;
    {
        auto boards = reach_corpus::make();
        m_tetris::TetrisMap map(10, 40);
        for (auto idx : reach_corpus::legacy_subcorpus_indices())
        {
            CaseData data;
            std::array<std::uint16_t, 48> rows{};
            for (int y = 0; y < 40; ++y)
            {
                rows[static_cast<std::size_t>(y)] = boards[idx][static_cast<std::size_t>(y)];
            }
            data.rows = rows;
            data.board = value_Board::from_rows(rows);
            data.map = map;
            for (int y = 0; y < 40; ++y)
            {
                data.map.row[static_cast<std::size_t>(y)] = rows[static_cast<std::size_t>(y)];
            }
            rebuild_metadata(data.map);
            data.cls.mappable = true;
            data.cls.upper_row = false;
            int cells = occupied_cells(rows);
            data.cls.density_decile = std::min(9, cells / 44);
            c1_cases.push_back(data);
        }
    }

    std::vector<CaseData> c2_cases;
    std::uint64_t c2_total_inputs = 0;
    std::uint64_t c2_ood = 0;
    if (corpus == "c2" || corpus == "both")
    {
        if (inputs_path.empty())
        {
            std::println(stderr, "--inputs required for corpus c2");
            return 2;
        }
        ReplayStream stream(inputs_path);
        if (!stream.ok())
        {
            std::println(stderr, "cannot open inputs");
            return 2;
        }
        std::uint32_t word_count = 0;
        std::uint64_t n_inputs = 0;
        if (!stream.header(word_count, n_inputs))
        {
            std::println(stderr, "bad inputs header");
            return 2;
        }
        std::unordered_map<std::string, std::size_t> index;
        ReplayInput in;
        std::vector<partition_fmt::ValueCandidate> cands;
        while (stream.next(in, cands))
        {
            ++c2_total_inputs;
            auto rows = rows_from_words(in.words);
            if (!map_rows_fit(rows))
            {
                ++c2_ood;
                continue;
            }
            std::string key(reinterpret_cast<char const *>(in.words.data()),
                static_cast<std::size_t>(word_count) * 8);
            key.push_back(in.piece);
            auto [it, inserted] = index.try_emplace(key, c2_cases.size());
            if (inserted)
            {
                CaseData data;
                data.rows = rows;
                data.board = board_from_words(in.words);
                data.map = m_tetris::TetrisMap(10, 40);
                for (int y = 0; y < 40; ++y)
                {
                    data.map.row[static_cast<std::size_t>(y)] = rows[static_cast<std::size_t>(y)];
                }
                rebuild_metadata(data.map);
                data.multiplicity = in.multiplicity;
                data.cls.mappable = true;
                data.cls.upper_row = false;
                int cells = occupied_cells(rows);
                data.cls.density_decile = std::min(9, cells / 44);
                c2_cases.push_back(data);
            }
            else
            {
                c2_cases[it->second].multiplicity += in.multiplicity;
            }
        }
    }

    std::vector<CellResult> results;
    std::string const pieces = reach_corpus::pieces;

    m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search> legacy_engine;
    if (!legacy_engine.prepare(10, 40))
    {
        std::println(stderr, "legacy prepare failed");
        return 2;
    }
    search_tspin::Search legacy_search;
    legacy_search.init(legacy_engine.context().get(), legacy_engine.search_config());

    auto run_stage = [&](int stage, std::vector<CaseData> const &cases,
                         std::vector<std::size_t> const &case_ids, char piece_char,
                         std::size_t drives, std::uint64_t &sink) -> BatchStat {
        value_Piece piece = *tetris::try_from_char(piece_char);
        tetris::toj::MovementConfig movement;
        movement.allow_180 = true;
        auto start = value_Placement::unchecked(4, 20, 0);
        unsigned init_rot = 0;
        m_tetris::TetrisNode const *gen = legacy_engine.context()->generate(piece_char);
        std::vector<double> samples;
        std::uint64_t total_ns = 0;
        sink = 0;
        for (std::size_t b = 0; b < warmup + timed; ++b)
        {
            std::size_t cursor = 0;
            std::uint64_t t0 = steady_stamp();
            for (std::size_t d = 0; d < drives; ++d)
            {
                CaseData const &data = cases[case_ids[cursor]];
                cursor = cursor + 1 == case_ids.size() ? 0 : cursor + 1;
                switch (stage)
                {
                case 0:
                {
                    sink += reachability::call_with_block<tetris::toj::SRS>(piece,
                        [&]<reachability::block B>() {
                            auto shapes = reachability::search::binary_bfs<B>(
                                data.board.occupancy(), reachability::search::search_config{},
                                reachability::coord{start.x(), start.y()}, init_rot);
                            std::uint64_t bits = 0;
                            for (auto const &bb : shapes)
                            {
                                bits += static_cast<std::uint64_t>(bb.popcount());
                            }
                            return bits;
                        });
                    break;
                }
                case 1:
                {
                    auto occ = data.board.occupancy();
                    sink += occ.logical_word(0) & 1ull;
                    break;
                }
                case 2:
                {
                    reachability::call_with_block<tetris::toj::SRS>(piece,
                        [&]<reachability::block B>() {
                            reachability::search::search_config cfg{};
                            cfg.allow_180 = true;
                            cfg.allow_softdrop = true;
                            cfg.allow_sonicdrop = true;
                            cfg.allow_20g = false;
                            reachability::search::search_workspace<B, value_Board::occupancy_t> ws(
                                data.board.occupancy());
                                        auto landed = reachability::search::arrival_search<B>(ws, cfg,
                                reachability::coord{start.x(), start.y()}, init_rot);
                            std::uint64_t bits = 0;
                            reachability::static_for<B.orientations>([&](auto i) {
                                bits += static_cast<std::uint64_t>(landed.normal_landings[i].popcount());
                                bits += static_cast<std::uint64_t>(landed.rotation_landings[i].popcount());
                            });
                            sink += bits;
                            return 0;
                        });
                    break;
                }
                case 3:
                {
                    std::array<value_Candidate, 4 * value_Board::width * value_Board::height * 2> out;
                    auto batch = tetris::toj::enumerate_candidates_into(
                        data.board, piece, movement, std::span<value_Candidate>(out.data(), out.size()));
                    sink += batch->count;
                    break;
                }
                case 4:
                {
                    auto const *land = legacy_search.search(data.map, gen, 1);
                    sink += land->size();
                    break;
                }
                case 5:
                {
                    m_tetris::TetrisMap map(10, 40);
                    for (int y = 0; y < 40; ++y)
                    {
                        map.row[static_cast<std::size_t>(y)] = data.rows[static_cast<std::size_t>(y)];
                    }
                    rebuild_metadata(map);
                    auto const *land = legacy_search.search(map, gen, 1);
                    sink += land->size();
                    break;
                }
                case 6:
                {
                    std::array<value_Candidate, 4 * value_Board::width * value_Board::height * 2> out;
                    auto batch = tetris::toj::enumerate_candidates_into(
                        data.board, piece, movement, std::span<value_Candidate>(out.data(), out.size()));
                    std::uint64_t mapped = 0;
                    for (std::size_t i = 0; i < batch->count; ++i)
                    {
                        auto legacy = tetris::toj::ExternalPoseTransform::to_legacy(piece,
                            out[static_cast<std::size_t>(i)].placement);
                        if (!legacy.has_value())
                        {
                            continue;
                        }
                        m_tetris::TetrisBlockStatus status{piece_char,
                            static_cast<std::int8_t>((*legacy)[0]),
                            static_cast<std::int8_t>((*legacy)[1]),
                            static_cast<std::uint8_t>((*legacy)[2])};
                        if (legacy_engine.context()->get(status) != nullptr)
                        {
                            ++mapped;
                        }
                    }
                    sink += mapped;
                    break;
                }
                default:
                    break;
                }
            }
            std::uint64_t t1 = steady_stamp();
            if (b >= warmup)
            {
                samples.push_back(static_cast<double>(t1 - t0));
                total_ns += t1 - t0;
            }
        }
        BatchStat stat;
        auto t = summarize(samples);
        stat.median_ns = t.median;
        stat.min_ns = t.min;
        stat.max_ns = t.max;
        stat.drives = drives * timed;
        stat.batch_ns = static_cast<std::uint64_t>(t.median);
        return stat;
    };

    auto digest_cases = [&](std::vector<CaseData> const &cases,
                            std::vector<std::size_t> const &case_ids, char piece_char,
                            std::array<std::uint64_t, 7> &sinks,
                            std::uint64_t &unmappable, std::uint64_t &spawn_blocked,
                            std::uint64_t &t_terminal) {
        value_Piece piece = *tetris::try_from_char(piece_char);
        tetris::toj::MovementConfig movement;
        movement.allow_180 = true;
        auto start = value_Placement::unchecked(4, 20, 0);
        for (auto id : case_ids)
        {
            CaseData const &data = cases[id];
            std::array<value_Candidate, 4 * value_Board::width * value_Board::height * 2> out;
            auto batch = tetris::toj::enumerate_candidates_into(
                data.board, piece, movement, std::span<value_Candidate>(out.data(), out.size()));
            std::uint64_t h = 1469598103934665603ull;
            bool t_term = false;
            for (std::size_t i = 0; i < batch->count; ++i)
            {
                h = fnv_mix(h, digest_one_candidate(out[static_cast<std::size_t>(i)]));
                if (out[static_cast<std::size_t>(i)].arrival == tetris::ArrivalClass::TerminalRotation)
                {
                    t_term = true;
                }
            }
            sinks[0] ^= h;
            sinks[1] += batch->count;
            if (t_term)
            {
                ++t_terminal;
            }
        }
        (void)unmappable;
        (void)spawn_blocked;
    };

    auto poisoned_check = [&]() {
        CaseData bad;
        bad.rows.fill(0x3ff);
        bad.board = value_Board::from_rows(bad.rows);
        value_Piece piece = value_Piece::T;
        auto start = value_Placement::unchecked(4, 20, 0);
        std::uint64_t sink = 0;
        reachability::call_with_block<tetris::toj::SRS>(piece,
            [&]<reachability::block B>() {
                reachability::search::search_config cfg{};
                reachability::search::search_workspace<B, value_Board::occupancy_t> ws(
                    bad.board.occupancy());
                auto landed = reachability::search::arrival_search<B>(ws, cfg,
                    reachability::coord{start.x(), start.y()}, 0);
                reachability::static_for<B.orientations>([&](auto i) {
                    sink += static_cast<std::uint64_t>(landed.normal_landings[i].popcount());
                });
                return 0;
            });
        return sink == 0;
    };

    std::println(stdout, "DIAGNOSTIC NON-BINDING legacy_stage_bench start");
    if (poisoned_check())
    {
        std::println(stdout, "POISON_CHECK pass (fully occupied board yields zero landings)");
    }
    else
    {
        std::println(stderr, "POISON_CHECK FAIL");
        return 1;
    }

    auto run_corpus = [&](char const *name, std::vector<CaseData> &cases) {
        for (std::size_t p = 0; p < 7; ++p)
        {
            char piece_char = pieces[p];
            std::vector<std::size_t> ids(cases.size());
            for (std::size_t i = 0; i < cases.size(); ++i)
            {
                ids[i] = i;
            }
            CellResult cell;
            cell.corpus = name;
            cell.piece = piece_char;
            cell.domain = "mappable";
            cell.decile = -1;
            cell.cases = ids.size();
            std::uint64_t sink_ref = 0;
            cell.stages[0] = run_stage(0, cases, ids, piece_char, drives_s1, cell.sinks[0]);
            std::array<std::uint64_t, 7> digests{};
            digest_cases(cases, ids, piece_char, digests, cell.unmappable,
                cell.spawn_blocked, cell.t_terminal_cases);
            sink_ref = cell.sinks[0];
            if (sink_ref == 0)
            {
                std::println(stderr, "SINK_ZERO FAIL {}", piece_char);
                return;
            }
            cell.stages[1] = run_stage(1, cases, ids, piece_char, drives_s1, cell.sinks[1]);
            cell.stages[2] = run_stage(2, cases, ids, piece_char, drives_rest, cell.sinks[2]);
            cell.stages[3] = run_stage(3, cases, ids, piece_char, drives_rest, cell.sinks[3]);
            cell.stages[4] = run_stage(4, cases, ids, piece_char, drives_rest, cell.sinks[4]);
            cell.stages[5] = run_stage(5, cases, ids, piece_char, drives_rest, cell.sinks[5]);
            cell.stages[6] = run_stage(6, cases, ids, piece_char, drives_rest, cell.sinks[6]);
            results.push_back(cell);
            std::println(stdout,
                "DIAGNOSTIC NON-BINDING cell corpus={} piece={} cases={} "
                "s1_pc={:.1f} s1x_pc={:.1f} s2_pc={:.1f} s3_pc={:.1f} s4_pc={:.1f} s4i_pc={:.1f} s5_pc={:.1f} "
                "digest={:016x}",
                name, piece_char, cell.cases, cell.stages[0].median_ns / static_cast<double>(drives_s1),
                cell.stages[1].median_ns / static_cast<double>(drives_s1),
                cell.stages[2].median_ns / static_cast<double>(drives_rest),
                cell.stages[3].median_ns / static_cast<double>(drives_rest),
                cell.stages[4].median_ns / static_cast<double>(drives_rest),
                cell.stages[5].median_ns / static_cast<double>(drives_rest),
                cell.stages[6].median_ns / static_cast<double>(drives_rest), digests[0]);
        }
    };

    if (corpus == "c1" || corpus == "both")
    {
        run_corpus("c1", c1_cases);
    }
    if (corpus == "c2" || corpus == "both")
    {
        run_corpus("c2", c2_cases);
        std::println(stdout, "DIAGNOSTIC NON-BINDING c2_inputs total={} distinct={} ood={}",
            c2_total_inputs, c2_cases.size(), c2_ood);
    }
    std::println(stdout, "DIAGNOSTIC NON-BINDING legacy_stage_bench done cells={}", results.size());
    return 0;
}
