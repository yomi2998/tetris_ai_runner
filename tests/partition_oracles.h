// Oracle helpers, logic verbatim from tests/rule_differential.cpp (kept in
// sync by inspection; copied because that file is a standalone test main).
// Used by the candidate_partition drive/classify modes only.

#pragma once

#include "toj_rule.h"

#include "tetris_board.h"
#include "tetris_core.h"
#include "tetris_types.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"
#include "scalar_arrival_oracle.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

namespace partition_oracle
{
    using Board = tetris::Board;
    using Piece = tetris::Piece;
    using Placement = tetris::Placement;
    using Candidate = tetris::Candidate;
    using ArrivalClass = tetris::ArrivalClass;

    namespace toj = tetris::toj;

    using Engine =
        m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    inline Piece piece_of(char name)
    {
        return *tetris::try_from_char(name);
    }

    inline bool create_legacy(Engine &engine, char piece, int x, int y, int r,
        m_tetris::TetrisNode &node)
    {
        if (engine.context()->get_opertion(piece, static_cast<unsigned char>(r)).create
            == nullptr)
        {
            return false;
        }
        return engine.context()->create(m_tetris::TetrisBlockStatus(piece,
            static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<uint8_t>(r)), node);
    }

    using CellKey = std::array<std::pair<int, int>, 4>;

    inline CellKey sorted_cells(std::array<std::pair<int, int>, 4> cells)
    {
        std::sort(cells.begin(), cells.end());
        return cells;
    }

    inline CellKey cells_key(Piece piece, Placement placement)
    {
        return sorted_cells(toj::cells(piece, placement).value());
    }

    inline CellKey legacy_cells_key(m_tetris::TetrisNode const *node)
    {
        std::array<std::pair<int, int>, 4> cells{};
        int index = 0;
        for (int ry = 0; ry < node->height; ++ry)
        {
            for (int rx = 0; rx < node->width; ++rx)
            {
                if ((node->data[ry] >> (node->col + rx)) & 1)
                {
                    cells[index++] = {node->col + rx, node->row + ry};
                }
            }
        }
        return sorted_cells(cells);
    }

    // Verbatim from tests/rule_differential.cpp.
    struct LegacyReplay
    {
        static constexpr int min_x = -4;
        static constexpr int max_x = 15;
        static constexpr int min_y = -4;
        static constexpr int max_y = 48;

        Engine &engine;
        m_tetris::TetrisMap const &map;
        Board const &board;
        std::uint8_t visited[4][max_x - min_x][max_y - min_y] = {};

        bool fits_status(char piece, int x, int y, int r) const
        {
            m_tetris::TetrisNode node;
            if (!create_legacy(engine, piece, x, y, r, node))
            {
                return false;
            }
            for (int ry = 0; ry < node.height; ++ry)
            {
                for (int rx = 0; rx < node.width; ++rx)
                {
                    if ((node.data[ry] >> (node.col + rx)) & 1)
                    {
                        int const cx = node.col + rx;
                        int const cy = node.row + ry;
                        if (board.full(cx, cy))
                        {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        void visit(int r, int x, int y, int channel)
        {
            visited[r][x - min_x][y - min_y] |= static_cast<std::uint8_t>(1 << channel);
        }

        void explore(int r, int x, int y, int channel,
            std::vector<std::tuple<int, int, int, int>> &queue)
        {
            std::uint8_t bit = static_cast<std::uint8_t>(1 << channel);
            std::uint8_t &slot = visited[r][x - min_x][y - min_y];
            if ((slot & bit) != 0)
            {
                return;
            }
            slot |= bit;
            queue.push_back({r, x, y, channel});
        }

        void run(char piece)
        {
            std::vector<std::tuple<int, int, int, int>> queue;
            if (fits_status(piece, 3, 21, 0))
            {
                visit(0, 3, 21, 0);
                queue.push_back({0, 3, 21, 0});
            }
            std::size_t head = 0;
            while (head < queue.size())
            {
                auto [r, x, y, channel] = queue[head++];
                (void)channel;
                for (auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}})
                {
                    int const nx = x + dx;
                    int const ny = y + dy;
                    if (nx < min_x || nx >= max_x || ny < min_y || ny >= max_y)
                    {
                        continue;
                    }
                    if (fits_status(piece, nx, ny, r))
                    {
                        explore(r, nx, ny, 0, queue);
                    }
                }
                m_tetris::TetrisNode const *node = engine.context()->get(
                    m_tetris::TetrisBlockStatus(piece, static_cast<int8_t>(x),
                        static_cast<int8_t>(y), static_cast<uint8_t>(r)));
                if (node == nullptr)
                {
                    continue;
                }
                m_tetris::TetrisMapSnap snap;
                node->build_snap(map, engine.context().get(), snap);
                for (int to : {(r + 1) % 4, (r + 3) % 4, (r + 2) % 4})
                {
                    m_tetris::TetrisNode const *const *table = to == (r + 1) % 4
                        ? node->wall_kick_clockwise
                        : (to == (r + 3) % 4 ? node->wall_kick_counterclockwise
                            : node->wall_kick_opposite);
                    for (std::size_t i = 0; i < m_tetris::max_wall_kick; ++i)
                    {
                        if (table[i] == nullptr)
                        {
                            break;
                        }
                        if (!table[i]->check(snap))
                        {
                            continue;
                        }
                        int const nx = table[i]->status.x;
                        int const ny = table[i]->status.y;
                        if (nx < min_x || nx >= max_x || ny < min_y || ny >= max_y)
                        {
                            break;
                        }
                        explore(to, nx, ny, 1, queue);
                        break;
                    }
                }
            }
        }

        bool landable_reachable(char piece, CellKey const &cells, int channel) const
        {
            for (int r = 0; r < 4; ++r)
            {
                for (int x = min_x; x < max_x; ++x)
                {
                    for (int y = min_y; y < max_y; ++y)
                    {
                        if ((visited[r][x - min_x][y - min_y] & (1 << channel)) == 0)
                        {
                            continue;
                        }
                        m_tetris::TetrisNode node;
                        if (!create_legacy(engine, piece, x, y, r, node))
                        {
                            continue;
                        }
                        if (legacy_cells_key(&node) != cells)
                        {
                            continue;
                        }
                        m_tetris::TetrisNode down;
                        if (create_legacy(engine, piece, x, y - 1, r, down))
                        {
                            bool blocked = false;
                            for (int ry = 0; ry < down.height && !blocked; ++ry)
                            {
                                for (int rx = 0; rx < down.width; ++rx)
                                {
                                    if ((down.data[ry] >> (down.col + rx)) & 1)
                                    {
                                        if (board.full(down.col + rx, down.row + ry))
                                        {
                                            blocked = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!blocked)
                            {
                                continue;
                            }
                        }
                        return true;
                    }
                }
            }
            return false;
        }
    };

    inline bool scalar_reachable(std::array<std::uint16_t, 48> const &rows,
        Piece piece, Candidate candidate)
    {
        return reachability::call_with_block<toj::SRS>(piece, [&]<reachability::block B>() {
            using namespace reachability;
            scalar_arrival::ScalarConfig oracle_config{};
            oracle_config.allow_180 = true;
            oracle_config.allow_softdrop = true;
            oracle_config.allow_sonicdrop = true;
            oracle_config.allow_20g = false;
            auto geometry = scalar_arrival::make_geometry<B>();
            scalar_arrival::ScalarOracle<B> oracle{geometry, oracle_config, rows};
            oracle.run(toj::spawn(piece), 0);
            auto normal_words = oracle.landable_words(0, true);
            auto rotation_words = oracle.landable_words(1, true);
            auto bit_at = [&](auto const &words) {
                return (words[candidate.placement.rotation()][candidate.placement.y() / 6]
                    & (std::uint64_t(1) << ((candidate.placement.y() % 6) * 10
                        + candidate.placement.x()))) != 0;
            };
            if (piece == Piece::T)
            {
                return candidate.arrival == ArrivalClass::TerminalRotation
                    ? bit_at(rotation_words)
                    : bit_at(normal_words);
            }
            return bit_at(normal_words) || bit_at(rotation_words);
        });
    }

    inline bool command_reachable(LegacyReplay const &replay, char piece_char,
        Piece piece, CellKey const &key, ArrivalClass arrival)
    {
        int const channel =
            piece == Piece::T && arrival == ArrivalClass::TerminalRotation ? 1 : 0;
        if (piece == Piece::T)
        {
            return replay.landable_reachable(piece_char, key, channel);
        }
        return replay.landable_reachable(piece_char, key, 0)
            || replay.landable_reachable(piece_char, key, 1);
    }
} // namespace partition_oracle
