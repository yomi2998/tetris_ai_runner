#pragma once

#include "tetris_board.h"
#include "tetris_types.h"

#include "fast-reachability/block.hpp"
#include "fast-reachability/kick_srs.hpp"
#include "fast-reachability/piece_tetromino.hpp"
#include "fast-reachability/search.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace tetris::toj
{
    using Board = tetris::Board;
    using Piece = tetris::Piece;
    using Placement = tetris::Placement;
    using Candidate = tetris::Candidate;
    using ArrivalClass = tetris::ArrivalClass;
    using SpinType = tetris::SpinType;

    using SRS = reachability::rules::rule_set<reachability::rules::Tetromino, reachability::rules::SRS_Kicks>;

    inline constexpr int death_row = 20;
    inline constexpr int spawn_x = 4;
    inline constexpr int spawn_y = 20;

    constexpr reachability::coord spawn(Piece)
    {
        return reachability::coord{spawn_x, spawn_y};
    }

    struct ExternalPoseTransform
    {
        struct Entry
        {
            int rotation;
            int dx;
            int dy;
            bool valid;
        };

        static constexpr Entry invalid{-1, 0, 0, false};

        static constexpr std::array<std::array<Entry, 4>, 7> table = [] {
            std::array<std::array<std::pair<int, int>, 4>, 7> const deltas{{
                {{{1, -1}, {1, -1}, {1, -1}, {1, -1}}},
                {{{1, -1}, {2, -1}, {1, -2}, {1, -1}}},
                {{{1, -1}, {2, -1}, {1, -2}, {1, -1}}},
                {{{1, -1}, {1, -1}, {1, -1}, {1, -1}}},
                {{{1, -1}, {1, -1}, {1, -1}, {1, -1}}},
                {{{1, -1}, {0, 0}, {0, 0}, {0, 0}}},
                {{{1, -1}, {2, -3}, {1, -2}, {1, -3}}},
            }};
            std::array<std::array<Entry, 4>, 7> out{};
            for (std::size_t piece = 0; piece < 7; ++piece)
            {
                for (int rotation = 0; rotation < 4; ++rotation)
                {
                    bool const valid = !(piece == static_cast<std::size_t>(Piece::O) && rotation > 0);
                    out[piece][rotation] = valid
                        ? Entry{rotation, deltas[piece][static_cast<std::size_t>(rotation)].first,
                            deltas[piece][static_cast<std::size_t>(rotation)].second, true}
                        : invalid;
                }
            }
            return out;
        }();

        static constexpr std::optional<Placement> to_placement(Piece piece, int legacy_x, int legacy_y, int legacy_rotation)
        {
            if (legacy_rotation < 0 || legacy_rotation > 3)
            {
                return std::nullopt;
            }
            Entry const &entry = table[static_cast<std::size_t>(piece)][legacy_rotation];
            if (!entry.valid)
            {
                return std::nullopt;
            }
            return Placement::try_make(legacy_x + entry.dx, legacy_y + entry.dy, entry.rotation);
        }

        static constexpr std::optional<std::array<int, 3>> to_legacy(Piece piece, Placement placement)
        {
            int const rotation = placement.rotation();
            if (rotation < 0 || rotation > 3)
            {
                return std::nullopt;
            }
            Entry const &entry = table[static_cast<std::size_t>(piece)][rotation];
            if (!entry.valid)
            {
                return std::nullopt;
            }
            return std::array<int, 3>{placement.x() - entry.dx, placement.y() - entry.dy, rotation};
        }
    };

    namespace detail
    {
        using reachability::operator""_szc;
        using reachability::operator+;

        template <auto B>
            requires reachability::block_spec<decltype(B)>
        struct PieceCells
        {
            static constexpr int orientations = B.orientations;
            static constexpr std::array<std::array<std::array<int, 2>, 4>, orientations> offsets = [] {
                std::array<std::array<std::array<int, 2>, 4>, orientations> out{};
                reachability::static_for<B.orientations>([&](auto i) {
                    constexpr auto shape = reachability::index_c<B.mino_index[i][0_szc]>;
                    reachability::static_for<4>([&](auto k) {
                        constexpr auto cell = B.minos[shape][k];
                        out[i][k] = std::array<int, 2>{cell[0_szc], cell[1_szc]};
                    });
                });
                return out;
            }();
        };
    }

    inline std::array<std::pair<int, int>, 4> cells(Piece piece, Placement placement)
    {
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::array<std::pair<int, int>, 4> {
            auto const &offsets = detail::PieceCells<B>::offsets[placement.rotation()];
            std::array<std::pair<int, int>, 4> out{};
            for (int k = 0; k < 4; ++k)
            {
                out[k] = {placement.x() + offsets[k][0], placement.y() + offsets[k][1]};
            }
            return out;
        });
    }

    inline bool fits(Piece piece, Placement placement, Board const &board)
    {
        for (auto const &cell : cells(piece, placement))
        {
            if (cell.first < 0 || cell.first >= Board::width || cell.second < 0 || cell.second >= Board::height)
            {
                return false;
            }
            if (board.full(cell.first, cell.second))
            {
                return false;
            }
        }
        return true;
    }

    inline bool can_spawn(Board const &board, Piece piece)
    {
        return fits(piece, Placement::unchecked(spawn_x, spawn_y, 0), board);
    }

    inline int lowest_occupied_row(Piece piece, Placement placement)
    {
        int low = Board::height;
        for (auto const &cell : cells(piece, placement))
        {
            low = std::min(low, cell.second);
        }
        return low;
    }

    inline Board::occupancy_t occupancy_mask(Piece piece, Placement placement)
    {
        Board::occupancy_t mask{};
        for (auto const &cell : cells(piece, placement))
        {
            mask.set(cell.first, cell.second);
        }
        return mask;
    }

    struct MovementConfig
    {
        bool allow_180 = true;
    };

    inline std::vector<Candidate> enumerate_candidates(Board const &board, Piece piece, MovementConfig config)
    {
        reachability::search::search_config cfg{};
        cfg.allow_180 = config.allow_180;
        cfg.allow_softdrop = true;
        cfg.allow_sonicdrop = true;
        cfg.allow_20g = false;
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::vector<Candidate> {
            struct Key
            {
                std::array<std::pair<int, int>, 4> cells;
                int arrival;

                bool operator<(Key const &other) const
                {
                    if (cells != other.cells)
                    {
                        return cells < other.cells;
                    }
                    return arrival < other.arrival;
                }
            };
            bool const is_t = B.piece_identity == reachability::piece_id("T");
            std::map<Key, Candidate> found;
            auto add = [&](Placement placement, ArrivalClass arrival) {
                Key key{cells(piece, placement), is_t ? static_cast<int>(arrival) : 0};
                found.emplace(key, Candidate{placement, is_t ? arrival : ArrivalClass::Normal});
            };
            reachability::search::dispatch_with_height<B, 20>(board.occupancy(), board.roof(),
                [&]<class Cut, bool Check>(Cut nb, std::integral_constant<bool, Check>) {
                    reachability::search::search_workspace<B, Cut> ws(nb);
                    auto result = reachability::search::arrival_search<B, Check>(ws, cfg, spawn(piece), 0);
                    for (int o = 0; o < B.orientations; ++o)
                    {
                        result.normal_landings[o].for_each_bit([&](int x, int y) {
                            add(Placement::unchecked(x, y, o), ArrivalClass::Normal);
                        });
                        result.rotation_landings[o].for_each_bit([&](int x, int y) {
                            add(Placement::unchecked(x, y, o), ArrivalClass::TerminalRotation);
                        });
                    }
                });
            std::vector<Candidate> out;
            out.reserve(found.size());
            for (auto const &entry : found)
            {
                out.push_back(entry.second);
            }
            return out;
        });
    }

    inline std::optional<SpinType> classify_spin(Board const &board, Piece piece, Candidate candidate, int clear_count)
    {
        if (piece != Piece::T)
        {
            return std::nullopt;
        }
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::optional<SpinType> {
            reachability::search::search_workspace<B, Board::occupancy_t> ws(board.occupancy());
            auto checker = ws.checker();
            Placement const placement = candidate.placement;
            int const rotation = placement.rotation();
            int const x = placement.x();
            int const y = placement.y();
            if (!checker.is_valid(rotation, x, y) || checker.is_valid(rotation, x, y - 1))
            {
                return std::nullopt;
            }
            if (candidate.arrival != ArrivalClass::TerminalRotation)
            {
                return SpinType::None;
            }
            int corners = 0;
            for (int cx = x - 1; cx <= x + 1; cx += 2)
            {
                for (int cy = y - 1; cy <= y + 1; cy += 2)
                {
                    if (cx < 0 || cx >= Board::width || cy < 0 || cy >= Board::height || board.full(cx, cy))
                    {
                        ++corners;
                    }
                }
            }
            if (corners < 3 || clear_count <= 0)
            {
                return SpinType::None;
            }
            bool mini_ready = true;
            for (int other = 0; other < B.orientations; ++other)
            {
                if (other != rotation && checker.is_valid(other, x, y))
                {
                    mini_ready = false;
                    break;
                }
            }
            if (mini_ready)
            {
                return clear_count == 1 ? SpinType::Mini : SpinType::Full;
            }
            return SpinType::Full;
        });
    }

    struct RuleResult
    {
        Board board;
        int clear_count = 0;
        SpinType spin = SpinType::None;
        bool lockout = false;
        bool perfect_clear = false;
    };

    inline std::optional<RuleResult> apply(Board const &board, Piece piece, Candidate candidate)
    {
        if (!fits(piece, candidate.placement, board))
        {
            return std::nullopt;
        }
        Board next = board;
        next.apply_unchecked(occupancy_mask(piece, candidate.placement));
        Board::ClearResult cleared = next.cleared();
        SpinType spin = SpinType::None;
        if (piece == Piece::T)
        {
            auto classified = classify_spin(board, piece, candidate, cleared.count);
            if (!classified)
            {
                return std::nullopt;
            }
            spin = *classified;
        }
        RuleResult result;
        result.board = cleared.board;
        result.clear_count = cleared.count;
        result.perfect_clear = cleared.board.empty();
        result.lockout = lowest_occupied_row(piece, candidate.placement) >= death_row;
        result.spin = spin;
        return result;
    }
}
