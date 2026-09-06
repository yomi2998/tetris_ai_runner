#pragma once

#include "tetris_board.h"
#include "tetris_types.h"

#include "fast-reachability/block.hpp"
#include "fast-reachability/kick_srs.hpp"
#include "fast-reachability/piece_tetromino.hpp"
#include "fast-reachability/search.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace tetris::toj
{
    using reachability::operator""_szc;

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

    inline constexpr int orientation_count(Piece piece)
    {
        return piece == Piece::O ? 1 : 4;
    }

    inline std::optional<std::array<std::pair<int, int>, 4>> cells(Piece piece, Placement placement)
    {
        int const rotation = placement.rotation();
        if (rotation < 0 || rotation >= orientation_count(piece))
        {
            return std::nullopt;
        }
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::array<std::pair<int, int>, 4> {
            auto const &offsets = detail::PieceCells<B>::offsets[rotation];
            std::array<std::pair<int, int>, 4> out{};
            for (int k = 0; k < 4; ++k)
            {
                out[k] = {placement.x() + offsets[k][0], placement.y() + offsets[k][1]};
            }
            return out;
        });
    }

    inline bool in_bounds(Piece piece, Placement placement)
    {
        auto maybe = cells(piece, placement);
        if (!maybe)
        {
            return false;
        }
        for (auto const &cell : *maybe)
        {
            if (cell.first < 0 || cell.first >= Board::width || cell.second < 0 || cell.second >= Board::height)
            {
                return false;
            }
        }
        return true;
    }

    inline bool cells_empty(Piece piece, Placement placement, Board const &board)
    {
        auto maybe = cells(piece, placement);
        if (!maybe)
        {
            return false;
        }
        for (auto const &cell : *maybe)
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

    inline bool fits(Piece piece, Placement placement, Board const &board)
    {
        return cells_empty(piece, placement, board);
    }

    inline bool can_spawn(Board const &board, Piece piece)
    {
        return fits(piece, Placement::unchecked(spawn_x, spawn_y, 0), board);
    }

    inline std::optional<int> lowest_occupied_row(Piece piece, Placement placement)
    {
        auto maybe = cells(piece, placement);
        if (!maybe)
        {
            return std::nullopt;
        }
        int low = Board::height;
        for (auto const &cell : *maybe)
        {
            low = std::min(low, cell.second);
        }
        return low;
    }

    inline std::optional<Board::occupancy_t> occupancy_mask(Piece piece, Placement placement)
    {
        auto maybe = cells(piece, placement);
        if (!maybe)
        {
            return std::nullopt;
        }
        Board::occupancy_t mask{};
        for (auto const &cell : *maybe)
        {
            if (cell.first < 0 || cell.first >= Board::width || cell.second < 0 || cell.second >= Board::height)
            {
                return std::nullopt;
            }
            mask.set(cell.first, cell.second);
        }
        return mask;
    }

    struct MovementConfig
    {
        bool allow_180 = true;
    };

    struct CandidateBatch
    {
        std::size_t count = 0;
        std::size_t raw_landings = 0;
    };

    inline std::size_t max_candidates_per_source()
    {
        return 4 * Board::width * Board::height * 2;
    }

    inline std::optional<CandidateBatch> enumerate_candidates_into(
        Board const &board, Piece piece, MovementConfig config, std::span<Candidate> out)
    {
        reachability::search::search_config cfg{};
        cfg.allow_180 = config.allow_180;
        cfg.allow_softdrop = true;
        cfg.allow_sonicdrop = true;
        cfg.allow_20g = false;
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::optional<CandidateBatch> {
            bool const is_t = B.piece_identity == reachability::piece_id("T");
            auto key_less = [piece, is_t](Candidate const &a, Candidate const &b) {
                auto const ca = *cells(piece, a.placement);
                auto const cb = *cells(piece, b.placement);
                int const aa = is_t ? static_cast<int>(a.arrival) : 0;
                int const ab = is_t ? static_cast<int>(b.arrival) : 0;
                if (ca != cb)
                {
                    return ca < cb;
                }
                return aa < ab;
            };
            auto key_equal = [piece, is_t](Candidate const &a, Candidate const &b) {
                return *cells(piece, a.placement) == *cells(piece, b.placement)
                    && (is_t ? static_cast<int>(a.arrival) : 0)
                        == (is_t ? static_cast<int>(b.arrival) : 0);
            };
            std::size_t n = 0;
            std::size_t raw = 0;
            bool overflow = false;
            auto add = [&](Placement placement, ArrivalClass arrival) {
                ++raw;
                auto maybe_cells = cells(piece, placement);
                if (!maybe_cells)
                {
                    return;
                }
                if (n >= out.size())
                {
                    overflow = true;
                    return;
                }
                out[n++] = Candidate{placement, is_t ? arrival : ArrivalClass::Normal};
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
            if (overflow)
            {
                return std::nullopt;
            }
            std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n), key_less);
            std::size_t m = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (m == 0 || !key_equal(out[m - 1], out[i]))
                {
                    out[m++] = out[i];
                }
            }
            return CandidateBatch{m, raw};
        });
    }

    inline std::vector<Candidate> enumerate_candidates(Board const &board, Piece piece, MovementConfig config)
    {
        std::vector<Candidate> out(max_candidates_per_source());
        auto batch = enumerate_candidates_into(board, piece, config, std::span<Candidate>(out));
        if (!batch.has_value())
        {
            return {};
        }
        out.resize(batch->count);
        return out;
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
            for (int other = 0; other < B.orientations && mini_ready; ++other)
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
                        auto target = Placement::try_make(x + kick[0_szc], y + kick[1_szc], other);
                        if (!target || !in_bounds(piece, *target))
                        {
                            return;
                        }
                        decided = true;
                        rotation_open = cells_empty(piece, *target, board);
                    });
                });
                if (decided && rotation_open)
                {
                    mini_ready = false;
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
        int const rotation = candidate.placement.rotation();
        int const x = candidate.placement.x();
        int const y = candidate.placement.y();
        return reachability::call_with_block<SRS>(piece, [&]<reachability::block B>() -> std::optional<RuleResult> {
            reachability::search::search_workspace<B, Board::occupancy_t> ws(board.occupancy());
            auto checker = ws.checker();
            if (rotation < 0 || rotation >= B.orientations
                || !checker.is_valid(rotation, x, y) || checker.is_valid(rotation, x, y - 1))
            {
                return std::nullopt;
            }
            auto mask = occupancy_mask(piece, candidate.placement);
            auto lowest = lowest_occupied_row(piece, candidate.placement);
            if (!mask || !lowest)
            {
                return std::nullopt;
            }
            Board next = board;
            next.apply_unchecked(*mask);
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
            result.lockout = *lowest >= death_row;
            result.spin = spin;
            return result;
        });
    }
}
