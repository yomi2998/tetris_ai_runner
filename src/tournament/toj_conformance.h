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
#include "tuning/domain.h"
#include "tuning/engine_match.h"
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

        constexpr std::size_t kCanonicalGames = 2;
        constexpr std::size_t kCanonicalIterationsPerMove = 24;
        constexpr int kCanonicalMaxRounds = 80;
        constexpr std::uint64_t kCanonicalSeed = 0xCA1E5EEDULL;

        template<class Adapter>
        void hash_bot_round(std::uint64_t &hash, int round,
                            tuning::EngineBotState<Adapter> const &bot)
        {
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(round));
            hash = tuning::fnv1a_byte(hash, bot.dead ? 1 : 0);
            hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(bot.hold));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.combo));
            hash = tuning::fnv1a_u64(hash, bot.b2b ? 1 : 0);
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.last_clear));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.total_clear));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.total_attack));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.total_block));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(bot.send_attack));
        }

        template<class Adapter>
        std::uint64_t canonical_game_moves_fingerprint(std::shared_ptr<m_tetris::TetrisContext> const &context,
                                                       std::span<double const> theta_a,
                                                       std::span<double const> theta_b)
        {
            if (!context || theta_a.size() != Adapter::param_count()
                || theta_b.size() != Adapter::param_count())
            {
                return 0;
            }
            std::size_t const next_len = Adapter::next_length();
            std::span<int const> const combo_table = Adapter::combo_table();
            m_tetris::SearchBudget const budget
                = m_tetris::SearchBudget::by_iterations(kCanonicalIterationsPerMove);
            std::size_t const max_rounds = static_cast<std::size_t>(kCanonicalMaxRounds);
            std::uint64_t hash = tuning::kFnvOffsetBasis ^ 0x6A1E5ULL;
            for (std::size_t game = 1; game <= kCanonicalGames; ++game)
            {
                tuning::EngineBotState<Adapter> a(
                    tuning::derive_game_seed(kCanonicalSeed, game, 0), max_rounds, next_len, context);
                tuning::EngineBotState<Adapter> b(
                    tuning::derive_game_seed(kCanonicalSeed, game, 1), max_rounds, next_len, context);
                if (!a.instance.apply_theta(theta_a.data(), theta_a.size())
                    || !b.instance.apply_theta(theta_b.data(), theta_b.size()))
                {
                    return 0;
                }
                hash = tuning::fnv1a_u64(hash, game);
                for (int round = 1; round <= kCanonicalMaxRounds; ++round)
                {
                    tuning::begin_game_round(a.scenario, b.scenario, round);
                    tuning::engine_prepare_side(a, next_len);
                    tuning::engine_prepare_side(b, next_len);
                    tuning::engine_run_side(a, *context, next_len, budget, combo_table);
                    tuning::engine_run_side(b, *context, next_len, budget, combo_table);
                    hash_bot_round(hash, round, a);
                    hash_bot_round(hash, round, b);
                    if (a.dead || b.dead)
                    {
                        break;
                    }
                    int const cancelled = std::min(a.send_attack, b.send_attack);
                    a.send_attack -= cancelled;
                    b.send_attack -= cancelled;
                    if (b.send_attack > 0)
                    {
                        a.recv_attack.push_back(b.send_attack);
                    }
                    if (a.send_attack > 0)
                    {
                        b.recv_attack.push_back(a.send_attack);
                    }
                }
                hash = tuning::fnv1a_u64(hash, a.dead ? 1 : 0);
                hash = tuning::fnv1a_u64(hash, b.dead ? 1 : 0);
            }
            return hash;
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

    inline std::uint64_t toj_conformance_fingerprint(
        std::shared_ptr<m_tetris::TetrisContext> const &context)
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
        std::uint64_t const moves
            = detail::canonical_game_moves_fingerprint<tuning_toj::TojAdapter>(context, theta_a,
                                                                               theta_b);
        std::uint64_t hash = tuning::fnv1a_u64(tuning::kFnvOffsetBasis ^ 0xC0AF0F0E1ULL, eval_a);
        hash = tuning::fnv1a_u64(hash, eval_b);
        hash = tuning::fnv1a_u64(hash, moves);
        return hash;
    }
}
