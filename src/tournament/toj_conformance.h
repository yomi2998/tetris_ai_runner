#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "ai_zzz.h"
#include "search_tspin.h"
#include "tetris_core.h"
#include "tournament/engine_identity.h"
#include "tuning/domain.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace tournament_identity
{
    namespace detail
    {
        inline std::vector<m_tetris::TetrisMap> conformance_maps()
        {
            std::mt19937 rng(0x51CE5D11u);
            std::vector<m_tetris::TetrisMap> maps;
            maps.emplace_back(10, 40);
            for (int k = 0; k < 7; ++k)
            {
                m_tetris::TetrisMap map(10, 40);
                int const fill = 2 + static_cast<int>(rng() % 17);
                for (int y = 0; y < fill; ++y)
                {
                    std::uint32_t row = rng() & 0x3ffu;
                    if (y + 1 == fill)
                    {
                        row |= 0x3ffu;
                    }
                    map.row[y] = row;
                }
                for (int x = 0; x < 10; ++x)
                {
                    int top = 0;
                    for (int y = 39; y >= 0; --y)
                    {
                        if ((map.row[y] >> x) & 1)
                        {
                            top = y + 1;
                            break;
                        }
                    }
                    map.top[x] = top;
                }
                for (int y = 39; y >= 0; --y)
                {
                    if (map.row[y] != 0)
                    {
                        map.roof = y + 1;
                        break;
                    }
                }
                for (int y = 0; y < map.roof; ++y)
                {
                    map.count += static_cast<std::int32_t>(std::popcount(map.row[y]));
                }
                maps.push_back(map);
            }
            return maps;
        }

        struct ConformancePlacement
        {
            char t = ' ';
            std::uint8_t r = 0;
            std::int8_t x = 0;
            std::int8_t y = 0;
            int spin = 0;
            std::uint32_t flags = 0;
            int clear = 0;
            double value = 0.0;
            std::int16_t t2 = 0;
            std::int16_t t3 = 0;
            std::int16_t well = 0;
        };

        inline bool conformance_placement_before(ConformancePlacement const &a,
                                                 ConformancePlacement const &b)
        {
            if (a.t != b.t)
            {
                return a.t < b.t;
            }
            if (a.r != b.r)
            {
                return a.r < b.r;
            }
            if (a.x != b.x)
            {
                return a.x < b.x;
            }
            return a.y < b.y;
        }

        inline std::uint64_t toj_eval_fingerprint(std::shared_ptr<m_tetris::TetrisContext> const &context,
                                                  std::span<double const> theta)
        {
            if (!context || theta.size() != tuning_toj::TojAdapter::param_count())
            {
                return 0;
            }
            std::span<int const> const combo = tuning_toj::TojAdapter::combo_table();
            ai_zzz::TOJ::Config ai_config;
            ai_config.table = combo.data();
            ai_config.table_max = static_cast<int>(combo.size());
            ai_config.safe = 0;
            ai_zzz::TOJ::theta_to_param(theta.data(), ai_config.param);
            ai_zzz::TOJ ai;
            ai.init(context.get(), &ai_config);
            search_tspin::Search::Config search_config;
            search_tspin::Search search;
            search.init(context.get(), &search_config);
            std::vector<m_tetris::TetrisMap> const maps = conformance_maps();
            std::uint64_t hash = tuning::kFnvOffsetBasis;
            for (m_tetris::TetrisMap const &map : maps)
            {
                for (std::size_t type = 0; type < context->type_max(); ++type)
                {
                    m_tetris::TetrisNode const *node = context->generate(context->convert(type));
                    if (node == nullptr)
                    {
                        continue;
                    }
                    std::vector<search_tspin::Search::TetrisNodeWithTSpinType> const *placements
                        = search.search(map, node, 2, 0);
                    if (placements == nullptr)
                    {
                        continue;
                    }
                    std::vector<ConformancePlacement> entries;
                    entries.reserve(placements->size());
                    for (search_tspin::Search::TetrisNodeWithTSpinType const &entry : *placements)
                    {
                        if (entry.node == nullptr)
                        {
                            continue;
                        }
                        m_tetris::TetrisMap placed = map;
                        int const clear = entry.node->attach(context.get(), placed);
                        ai_zzz::TOJ::Result const result = ai.eval(entry, placed, map);
                        ConformancePlacement placement;
                        placement.t = entry.node->status.t;
                        placement.r = entry.node->status.r;
                        placement.x = entry.node->status.x;
                        placement.y = entry.node->status.y;
                        placement.spin = static_cast<int>(entry.type);
                        placement.flags = entry.flags;
                        placement.clear = clear;
                        placement.value = result.value;
                        placement.t2 = result.t2_value;
                        placement.t3 = result.t3_value;
                        placement.well = result.well_depth;
                        entries.push_back(placement);
                    }
                    std::sort(entries.begin(), entries.end(), conformance_placement_before);
                    hash = tuning::fnv1a_u64(hash, entries.size());
                    for (ConformancePlacement const &entry : entries)
                    {
                        hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(entry.t));
                        hash = tuning::fnv1a_byte(hash, entry.r);
                        hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(entry.x));
                        hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(entry.y));
                        hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(entry.spin));
                        hash = tuning::fnv1a_u64(hash, entry.flags);
                        hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(entry.clear));
                        hash = tuning::fnv1a_double(hash, entry.value);
                        hash = tuning::fnv1a_u64(hash,
                            static_cast<std::uint64_t>(static_cast<std::uint16_t>(entry.t2)) << 48
                            | static_cast<std::uint64_t>(static_cast<std::uint16_t>(entry.t3)) << 32
                            | static_cast<std::uint64_t>(static_cast<std::uint16_t>(entry.well)));
                    }
                }
            }
            return hash;
        }
    }

    template<class RunGames>
    std::uint64_t toj_conformance_fingerprint(std::shared_ptr<m_tetris::TetrisContext> const &context,
                                              RunGames const &run_games)
    {
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        std::vector<double> const theta_a(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> theta_b(theta_a.size());
        for (std::size_t i = 0; i < theta_a.size(); ++i)
        {
            theta_b[i] = theta_a[i] * (1.0 + 0.01 * static_cast<double>(i % 5));
        }
        std::uint64_t const eval_a = detail::toj_eval_fingerprint(context, theta_a);
        std::uint64_t const eval_b = detail::toj_eval_fingerprint(context, theta_b);
        std::uint64_t const game_hash = adapter_engine_fingerprint<tuning_toj::TojAdapter>(run_games);
        std::uint64_t hash = tuning::fnv1a_u64(tuning::kFnvOffsetBasis ^ 0xC0AF0F0E1ULL, eval_a);
        hash = tuning::fnv1a_u64(hash, eval_b);
        hash = tuning::fnv1a_u64(hash, game_hash);
        return hash;
    }
}
