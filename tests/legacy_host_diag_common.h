#pragma once

#include "partition_format.h"
#include "reach_corpus.h"

#include "tetris_board.h"
#include "tetris_types.h"
#include "toj_rule.h"
#include "toj_pathfinder.h"
#include "toj_policy.h"

#include "tetris_engine.h"
#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include "fast-reachability/block.hpp"
#include "fast-reachability/kick_srs.hpp"
#include "fast-reachability/piece_tetromino.hpp"
#include "fast-reachability/search.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <optional>
#include <cstdio>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace legacy_diag
{
    using reachability::operator""_szc;

    inline constexpr std::uint64_t memory_cap = 268435456ull;
    inline constexpr std::uint64_t memory_margin = 65536ull;

    using value_Board = tetris::Board;
    using value_Piece = tetris::Piece;
    using value_Placement = tetris::Placement;
    using value_Candidate = tetris::Candidate;

    struct Counts
    {
        std::uint64_t searches = 0;
        std::uint64_t evals = 0;
        std::uint64_t gets = 0;
        std::uint64_t board_conversions = 0;
        std::uint64_t board_conversion_reuses = 0;
        std::uint64_t unmappable_landings = 0;
        std::uint64_t unmappable_starts = 0;
        std::uint64_t not_in_graph = 0;
        std::uint64_t path_failures = 0;
        std::uint64_t path_relabels = 0;
        std::uint64_t spawn_blocked = 0;
        std::uint64_t parents = 0;
        std::uint64_t candidates = 0;
        std::uint64_t materialized = 0;
        std::uint64_t selected = 0;
    };

    inline std::uint64_t fnv_mix(std::uint64_t h, std::uint64_t v)
    {
        h ^= v;
        h *= 1099511628211ull;
        return h;
    }

    inline bool read_vmhwm_vmrss(std::uint64_t &hwm, std::uint64_t &rss)
    {
        std::ifstream status("/proc/self/status");
        std::string line;
        bool got_hwm = false;
        bool got_rss = false;
        while (std::getline(status, line))
        {
            std::size_t const colon = line.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }
            std::string const key = line.substr(0, colon);
            std::string const rest = line.substr(colon + 1);
            std::uint64_t value = 0;
            if (std::sscanf(rest.c_str(), "%llu", static_cast<unsigned long long *>((void *)&value)) != 1)
            {
                continue;
            }
            if (key == "VmHWM")
            {
                hwm = value * 1024ull;
                got_hwm = true;
            }
            else if (key == "VmRSS")
            {
                rss = value * 1024ull;
                got_rss = true;
            }
        }
        return got_hwm && got_rss;
    }

    inline bool map_rows_fit(std::array<std::uint16_t, 48> const &rows)
    {
        for (int y = 40; y < 48; ++y)
        {
            if (rows[static_cast<std::size_t>(y)] != 0)
            {
                return false;
            }
        }
        return true;
    }

    inline void rebuild_metadata(m_tetris::TetrisMap &map)
    {
        map.roof = 0;
        map.count = 0;
        std::memset(map.top, 0, sizeof map.top);
        for (int y = 0; y < 40; ++y)
        {
            for (int x = 0; x < 10; ++x)
            {
                if (map.full(static_cast<std::size_t>(x), static_cast<std::size_t>(y)))
                {
                    map.top[static_cast<std::size_t>(x)] = y + 1;
                    map.roof = std::max(map.roof, y + 1);
                    ++map.count;
                }
            }
        }
    }

    inline void map_from_rows(std::array<std::uint16_t, 48> const &rows, m_tetris::TetrisMap &map)
    {
        std::memset(static_cast<void *>(&map), 0, sizeof map);
        map.width = 10;
        map.height = 40;
        for (int y = 0; y < 40; ++y)
        {
            map.row[static_cast<std::size_t>(y)] = rows[static_cast<std::size_t>(y)];
        }
        rebuild_metadata(map);
    }

    inline std::array<std::uint16_t, 48> rows_from_words(std::vector<std::uint64_t> const &words)
    {
        value_Board::occupancy_t occ{};
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            occ.set_logical_word(static_cast<int>(i), words[i]);
        }
        auto rb = occ.template to_row_bitboard<true>();
        std::array<std::uint16_t, 48> rows{};
        for (int y = 0; y < 48; ++y)
        {
            rows[static_cast<std::size_t>(y)] = static_cast<std::uint16_t>(rb[static_cast<std::size_t>(y)] & 0x3ff);
        }
        return rows;
    }
    inline value_Board board_from_words(std::vector<std::uint64_t> const &words)
    {
        return value_Board::from_rows(rows_from_words(words));
    }


    inline int occupied_cells(std::array<std::uint16_t, 48> const &rows)
    {
        int count = 0;
        for (auto row : rows)
        {
            count += std::popcount(row);
        }
        return count;
    }

    struct ReplayInput
    {
        std::vector<std::uint64_t> words;
        char piece = '?';
        std::uint8_t source = 0;
        std::uint64_t multiplicity = 0;
    };

    struct ReplayStream
    {
        explicit ReplayStream(std::string const &path)
            : file_(path)
        {
        }

        bool ok() const
        {
            return file_.ok();
        }

        bool header(std::uint32_t &word_count, std::uint64_t &n_inputs)
        {
            char magic[8];
            std::uint32_t version = 0;
            if (!file_.get(magic, 8) || !file_.get_value(version)
                || !file_.get_value(word_count) || !file_.get_value(n_inputs))
            {
                return false;
            }
            if (std::memcmp(magic, partition_fmt::inputs_magic, 8) != 0
                || version != partition_fmt::format_version)
            {
                return false;
            }
            word_count_ = word_count;
            return true;
        }

        bool next(ReplayInput &input, std::vector<partition_fmt::ValueCandidate> &candidates)
        {
            std::uint64_t words8 = 0;
            std::uint8_t overflow = 0;
            std::uint8_t pad = 0;
            std::uint32_t n_cand = 0;
            input.words.assign(word_count_, 0);
            for (std::uint32_t i = 0; i < word_count_; ++i)
            {
                if (!file_.get_value(words8))
                {
                    return false;
                }
                input.words[static_cast<std::size_t>(i)] = words8;
            }
            char raw_piece = '?';
            if (!file_.get(&raw_piece, 1))
            {
                return false;
            }
            input.piece = raw_piece;
            std::uint8_t piece_code = static_cast<std::uint8_t>(raw_piece);
            (void)piece_code;
            if (!file_.get(&input.source, 1)
                || !file_.get(&overflow, 1)
                || !file_.get(&pad, 1) || !file_.get_value(input.multiplicity)
                || !file_.get_value(n_cand))
            {
                return false;
            }
            candidates.clear();
            candidates.resize(n_cand);
            for (std::uint32_t i = 0; i < n_cand; ++i)
            {
                partition_fmt::ValueCandidate &c = candidates[static_cast<std::size_t>(i)];
                std::uint8_t apply_ok = 0;
                std::uint8_t spin = 0;
                std::uint8_t clear_count = 0;
                std::uint8_t lockout = 0;
                std::uint8_t survivor = 0;
                std::uint8_t pad_a = 0;
                std::uint8_t pad_b = 0;
                if (!file_.get_value(c.packed) || !file_.get(&c.arrival, 1)
                    || !file_.get(&apply_ok, 1) || !file_.get(&spin, 1)
                    || !file_.get(&clear_count, 1) || !file_.get(&lockout, 1)
                    || !file_.get(&survivor, 1) || !file_.get(&pad_a, 1)
                    || !file_.get(&pad_b, 1) || !file_.get_value(c.result_hash40))
                {
                    return false;
                }
                c.apply_ok = apply_ok;
                c.spin = spin;
                c.clear_count = clear_count;
                c.lockout = lockout;
                c.survivor = survivor;
            }
            return true;
        }

    private:
        partition_fmt::Reader file_;
        std::uint32_t word_count_ = 0;
    };

    struct BoardClass
    {
        bool mappable = false;
        bool upper_row = false;
        bool t_terminal = false;
        bool blocked_spawn = false;
        int density_decile = 0;
    };

    struct SuppliedStartResult
    {
        std::vector<value_Candidate> candidates;
        std::uint64_t raw = 0;
        bool overflow = false;
    };

    namespace adapter_detail
    {
        using detail_Entry = tetris::toj::detail::CandidateSortEntry;

        inline std::uint64_t candidate_key(tetris::Piece piece, value_Placement placement,
            value_Candidate candidate)
        {
            return reachability::call_with_block<tetris::toj::SRS>(piece,
                [&]<reachability::block B>() -> std::uint64_t {
                    std::uint64_t key = 0;
                    for (auto const &cell : tetris::toj::detail::block_cells<B>(placement))
                    {
                        key = key << 4 | static_cast<std::uint64_t>(cell.first);
                        key = key << 6 | static_cast<std::uint64_t>(cell.second);
                    }
                    key = key << 1 | static_cast<std::uint64_t>(candidate.arrival);
                    return key << 2
                        | static_cast<std::uint64_t>(candidate.placement.rotation());
                });
        }

        inline void canonicalize(std::vector<detail_Entry> &scratch,
            std::vector<value_Candidate> &out)
        {
            auto key_less = [](detail_Entry const &a, detail_Entry const &b) {
                return a.key < b.key;
            };
            auto key_equal = [](detail_Entry const &a, detail_Entry const &b) {
                return (a.key >> 2) == (b.key >> 2);
            };
            constexpr std::size_t bucket_count = 16;
            std::array<std::size_t, bucket_count> counts{};
            auto bucket_of = [](detail_Entry const &entry) {
                return static_cast<std::size_t>(entry.key >> 39);
            };
            for (auto const &entry : scratch)
            {
                ++counts[bucket_of(entry)];
            }
            std::array<std::size_t, bucket_count + 1> offsets{};
            for (std::size_t bucket = 0; bucket < bucket_count; ++bucket)
            {
                offsets[bucket + 1] = offsets[bucket] + counts[bucket];
            }
            std::array<std::size_t, bucket_count> next{};
            std::copy_n(offsets.begin(), bucket_count, next.begin());
            for (std::size_t bucket = 0; bucket < bucket_count; ++bucket)
            {
                while (next[bucket] < offsets[bucket + 1])
                {
                    std::size_t const target = bucket_of(scratch[next[bucket]]);
                    if (target == bucket)
                    {
                        ++next[bucket];
                    }
                    else
                    {
                        std::swap(scratch[next[bucket]], scratch[next[target]++]);
                    }
                }
                std::sort(scratch.begin() + static_cast<std::ptrdiff_t>(offsets[bucket]),
                    scratch.begin() + static_cast<std::ptrdiff_t>(offsets[bucket + 1]),
                    key_less);
            }
            out.clear();
            std::size_t kept = static_cast<std::size_t>(-1);
            for (std::size_t i = 0; i < scratch.size(); ++i)
            {
                if (kept == static_cast<std::size_t>(-1) || !key_equal(scratch[kept], scratch[i]))
                {
                    out.push_back(scratch[i].candidate);
                    kept = i;
                }
            }
        }
    }

    inline SuppliedStartResult enumerate_supplied_start(value_Board const &board,
        value_Piece piece, tetris::toj::MovementConfig config,
        reachability::coord start, unsigned init_rot)
    {
        SuppliedStartResult result;
        result.candidates.reserve(tetris::toj::max_candidates_per_source());
        reachability::call_with_block<tetris::toj::SRS>(piece,
            [&]<reachability::block B>() {
                reachability::search::search_config cfg{};
                cfg.allow_180 = config.allow_180;
                cfg.allow_softdrop = true;
                cfg.allow_sonicdrop = true;
                cfg.allow_20g = false;
                bool const is_t = B.piece_identity == reachability::piece_id("T");
                thread_local std::vector<adapter_detail::detail_Entry> scratch;
                scratch.clear();
                scratch.reserve(tetris::toj::max_candidates_per_source());
                auto add = [&](value_Placement placement, tetris::ArrivalClass arrival) {
                    ++result.raw;
                    adapter_detail::detail_Entry entry;
                    entry.candidate = value_Candidate{placement,
                        is_t ? arrival : tetris::ArrivalClass::Normal};
                    entry.key = adapter_detail::candidate_key(piece, placement, entry.candidate);
                    scratch.push_back(entry);
                };
                reachability::search::search_workspace<B, value_Board::occupancy_t> ws(board.occupancy());
                auto landed = reachability::search::arrival_search<B>(ws, cfg, start, init_rot);
                reachability::static_for<B.orientations>([&](auto i) {
                    landed.normal_landings[i].for_each_bit([&](int x, int y) {
                        add(value_Placement::unchecked(x, y, static_cast<int>(i)),
                            tetris::ArrivalClass::Normal);
                    });
                    landed.rotation_landings[i].for_each_bit([&](int x, int y) {
                        add(value_Placement::unchecked(x, y, static_cast<int>(i)),
                            tetris::ArrivalClass::TerminalRotation);
                    });
                });
                adapter_detail::canonicalize(scratch, result.candidates);
                return 0;
            });
        return result;
    }

    inline std::uint64_t digest_one_candidate(value_Candidate const &candidate)
    {
        std::uint64_t h = fnv_mix(1469598103934665603ull, candidate.placement.packed());
        return fnv_mix(h, static_cast<std::uint64_t>(candidate.arrival));
    }

    inline value_Placement unpack_recorded(std::uint16_t packed)
    {
        return value_Placement::unchecked(packed & 0xf, (packed >> 4) & 0x3f,
            (packed >> 10) & 0x3);
    }

    struct TSpinAnalysis
    {
        bool corners_ready = false;
        bool mini_ready = false;
    };

    inline TSpinAnalysis analyze_t_spawn(value_Board const &board, value_Candidate candidate)
    {
        TSpinAnalysis analysis;
        if (candidate.arrival != tetris::ArrivalClass::TerminalRotation)
        {
            return analysis;
        }
        using tetris::toj::detail::block_cells;
        int const x = candidate.placement.x();
        int const y = candidate.placement.y();
        int corners = 0;
        for (int cx = x - 1; cx <= x + 1; cx += 2)
        {
            for (int cy = y - 1; cy <= y + 1; cy += 2)
            {
                if (cx < 0 || cx >= value_Board::width || cy < 0 || cy >= value_Board::height
                    || board.full(cx, cy))
                {
                    ++corners;
                }
            }
        }
        analysis.corners_ready = corners >= 3;
        analysis.mini_ready = true;
        reachability::call_with_block<tetris::toj::SRS>(value_Piece::T,
            [&]<reachability::block B>() {
                int const rotation = candidate.placement.rotation();
                for (int other = 0; other < B.orientations && analysis.mini_ready; ++other)
                {
                    if (other == rotation)
                    {
                        continue;
                    }
                    bool decided = false;
                    bool rotation_open = false;
                    reachability::static_for<std::tuple_size_v<decltype(B.kicks)>>([&](auto i) {
                        constexpr auto entry = B.kicks[i];
                        constexpr auto diff = entry[0_szc];
                        if (diff[0_szc] != rotation || diff[1_szc] != other)
                        {
                            return;
                        }
                        constexpr auto table = entry[1_szc];
                        reachability::static_for<std::tuple_size_v<std::remove_const_t<decltype(table)>>>([&](auto j) {
                            if (decided)
                            {
                                return;
                            }
                            constexpr auto kick = table[j];
                            auto target = value_Placement::try_make(
                                x + kick[0_szc], y + kick[1_szc], other);
                            if (!target)
                            {
                                return;
                            }
                            auto target_cells = block_cells<B>(*target);
                            if (!tetris::toj::detail::cell_set_in_bounds(target_cells))
                            {
                                return;
                            }
                            decided = true;
                            rotation_open = tetris::toj::detail::cell_set_empty(board, target_cells);
                        });
                    });
                    if (decided && rotation_open)
                    {
                        analysis.mini_ready = false;
                    }
                }
                return 0;
            });
        return analysis;
    }

    class AdapterSearch
    {
    public:
        using TSpinType = search_tspin::Search::TSpinType;
        using Config = search_tspin::Search::Config;
        using TetrisNodeWithTSpinType = search_tspin::Search::TetrisNodeWithTSpinType;

        Counts *counts = nullptr;
        bool allow_180 = true;
        std::vector<value_Candidate> last_candidates;

        void init(m_tetris::TetrisContext const *context, Config const *config)
        {
            context_ = context;
            config_ = config;
            allow_180 = config != nullptr ? config->allow_180 : true;
        }

        std::vector<TetrisNodeWithTSpinType> const *search(
            m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, std::size_t depth)
        {
            (void)depth;
            if (counts != nullptr)
            {
                ++counts->searches;
            }
            if (node == nullptr)
            {
                cache_.clear();
                return &cache_;
            }
            value_Board const &board = board_for(map);
            auto maybe_piece = tetris::try_from_char(node->status.t);
            if (!maybe_piece.has_value())
            {
                if (counts != nullptr)
                {
                    ++counts->unmappable_starts;
                }
                cache_.clear();
                return &cache_;
            }
            value_Piece piece = *maybe_piece;
            auto start = tetris::toj::ExternalPoseTransform::to_placement(piece,
                node->status.x, node->status.y, node->status.r);
            if (!start.has_value())
            {
                if (counts != nullptr)
                {
                    ++counts->unmappable_starts;
                }
                cache_.clear();
                return &cache_;
            }
            if (!tetris::toj::fits(piece, *start, board))
            {
                if (counts != nullptr)
                {
                    ++counts->spawn_blocked;
                }
                cache_.clear();
                return &cache_;
            }
            tetris::toj::MovementConfig movement;
            movement.allow_180 = allow_180;
            SuppliedStartResult enumerated = enumerate_supplied_start(board, piece, movement,
                reachability::coord{start->x(), start->y()},
                static_cast<unsigned>(start->rotation()));
            last_candidates = enumerated.candidates;
            cache_.clear();
            for (auto const &candidate : enumerated.candidates)
            {
                auto legacy = tetris::toj::ExternalPoseTransform::to_legacy(piece, candidate.placement);
                if (!legacy.has_value())
                {
                    if (counts != nullptr)
                    {
                        ++counts->unmappable_landings;
                    }
                    continue;
                }
                m_tetris::TetrisBlockStatus status{node->status.t,
                    static_cast<std::int8_t>((*legacy)[0]), static_cast<std::int8_t>((*legacy)[1]),
                    static_cast<std::uint8_t>((*legacy)[2])};
                m_tetris::TetrisNode const *pose = context_ != nullptr ? context_->get(status) : nullptr;
                if (pose == nullptr)
                {
                    if (counts != nullptr)
                    {
                        ++counts->not_in_graph;
                    }
                    continue;
                }
                TetrisNodeWithTSpinType land_point(pose);
                land_point.last = pose;
                if (piece == value_Piece::T
                    && candidate.arrival == tetris::ArrivalClass::TerminalRotation)
                {
                    TSpinAnalysis analysis = analyze_t_spawn(board, candidate);
                    if (analysis.corners_ready)
                    {
                        land_point.type = TSpinType::TSpin;
                        land_point.is_ready = true;
                        land_point.is_mini_ready = analysis.mini_ready;
                    }
                }
                cache_.push_back(land_point);
                if (counts != nullptr)
                {
                    ++counts->candidates;
                }
            }
            return &cache_;
        }

        std::vector<char> make_path(m_tetris::TetrisNode const *node,
            TetrisNodeWithTSpinType const &land_point, m_tetris::TetrisMap const &map)
        {
            std::vector<char> out;
            if (node == nullptr || land_point.node == nullptr)
            {
                return out;
            }
            value_Board const &board = board_for(map);
            auto piece_opt = tetris::try_from_char(node->status.t);
            if (!piece_opt.has_value())
            {
                if (counts != nullptr)
                {
                    ++counts->path_failures;
                }
                return out;
            }
            value_Piece piece = *piece_opt;
            auto start = tetris::toj::ExternalPoseTransform::to_placement(piece,
                node->status.x, node->status.y, node->status.r);
            auto target = tetris::toj::ExternalPoseTransform::to_placement(piece,
                land_point.node->status.x, land_point.node->status.y, land_point.node->status.r);
            if (!start.has_value() || !target.has_value())
            {
                if (counts != nullptr)
                {
                    ++counts->path_failures;
                }
                return out;
            }
            tetris::path::PathConfig path_config;
            path_config.allow_180 = allow_180;
            tetris::path::Pathfinder finder(board, piece, *start, path_config);
            tetris::ArrivalClass arrival = land_point.type == TSpinType::None
                ? tetris::ArrivalClass::Normal
                : tetris::ArrivalClass::TerminalRotation;
            tetris::path::Path path = finder.find(value_Candidate{*target, arrival});
            if (!path.valid)
            {
                path = finder.find(value_Candidate{*target, tetris::ArrivalClass::Normal});
                if (path.valid)
                {
                    if (counts != nullptr)
                    {
                        ++counts->path_relabels;
                    }
                }
            }
            if (!path.valid)
            {
                if (counts != nullptr)
                {
                    ++counts->path_failures;
                }
                return out;
            }
            out.assign(path.view().begin(), path.view().end());
            return out;
        }

        TSpinType classify(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node,
            bool last_rotate, std::size_t clear)
        {
            (void)map;
            (void)node;
            (void)last_rotate;
            (void)clear;
            return TSpinType::None;
        }

        std::uint64_t board_conversion_reserved_bytes() const
        {
            return sizeof(value_Board) + sizeof(m_tetris::TetrisMap);
        }

    private:
        value_Board const &board_for(m_tetris::TetrisMap const &map)
        {
            if (have_map_ && std::memcmp(&map, &last_map_, sizeof(m_tetris::TetrisMap)) == 0)
            {
                if (counts != nullptr)
                {
                    ++counts->board_conversion_reuses;
                }
                return board_;
            }
            if (counts != nullptr)
            {
                ++counts->board_conversions;
            }
            std::array<std::uint16_t, 48> rows{};
            for (int y = 0; y < 40; ++y)
            {
                rows[static_cast<std::size_t>(y)] = static_cast<std::uint16_t>(map.row[static_cast<std::size_t>(y)]);
            }
            board_ = value_Board::from_rows(rows);
            last_map_ = map;
            have_map_ = true;
            return board_;
        }

        m_tetris::TetrisContext const *context_ = nullptr;
        Config const *config_ = nullptr;
        std::vector<TetrisNodeWithTSpinType> cache_;
        m_tetris::TetrisMap last_map_{};
        value_Board board_{};
        bool have_map_ = false;
    };
}

namespace legacy_host_diag = m_tetris::legacy_host_diag;
