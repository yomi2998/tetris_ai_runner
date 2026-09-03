#pragma once

#include "tetris_types.h"
#include "fast-reachability/board.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace tetris
{
    class Board
    {
    public:
        static constexpr int width = 10;
        static constexpr int height = 48;
        static constexpr uint16_t row_mask = 0x3ff;
        using occupancy_t = reachability::board_t<width, height, std::uint64_t>;

        constexpr Board() = default;

        static Board from_rows(std::array<uint16_t, height> const &rows)
        {
            Board board;
            std::array<occupancy_t::row_t, height> clipped = {};
            for (int y = 0; y < height; ++y)
            {
                clipped[y] = static_cast<occupancy_t::row_t>(rows[y] & row_mask);
            }
            board.occupancy_.from_row_bitboard<true>(clipped);
            board.refresh_roof();
            return board;
        }

        static Board from_exported(std::array<uint32_t, 23> const &field, std::array<uint32_t, 8> const &overfield)
        {
            std::array<uint16_t, height> rows = {};
            for (int y = 0; y <= 22; ++y)
            {
                rows[y] = static_cast<uint16_t>(field[static_cast<size_t>(22 - y)] & row_mask);
            }
            for (int y = 23; y <= 30; ++y)
            {
                rows[y] = static_cast<uint16_t>(overfield[static_cast<size_t>(y - 23)] & row_mask);
            }
            return from_rows(rows);
        }

        std::array<uint16_t, height> rows() const
        {
            std::array<uint16_t, height> out = {};
            for (int y = 0; y < height; ++y)
            {
                out[y] = row(y);
            }
            return out;
        }

        uint16_t row(int y) const
        {
            int const yi = y / occupancy_t::lines_per_under;
            int const off = (y % occupancy_t::lines_per_under) * width;
            return static_cast<uint16_t>((occupancy_.raw()[yi] >> off) & row_mask);
        }

        constexpr bool full(int x, int y) const
        {
            return occupancy_.get(x, y) == 1;
        }

        constexpr occupancy_t const &occupancy() const
        {
            return occupancy_;
        }

        constexpr unsigned roof() const
        {
            return roof_;
        }

        constexpr bool empty() const
        {
            return roof_ == 0;
        }

        std::array<size_t, width> column_tops() const
        {
            return occupancy_.column_tops();
        }

        constexpr bool operator==(Board const &other) const
        {
            return occupancy_ == other.occupancy_;
        }

        void apply(occupancy_t const &mask)
        {
            assert_valid_mask(mask);
            occupancy_ |= mask;
            refresh_roof();
            validate();
        }

        struct ClearResult;

        ClearResult cleared() const;

        void add_garbage(int lines, uint16_t hole_mask)
        {
            if (lines <= 0)
            {
                return;
            }
            auto rows = occupancy_.template to_row_bitboard<true>();
            for (int y = height - 1; y >= lines; --y)
            {
                rows[y] = rows[y - lines];
            }
            for (int y = 0; y < lines && y < height; ++y)
            {
                rows[y] = static_cast<occupancy_t::row_t>(hole_mask & row_mask);
            }
            occupancy_.from_row_bitboard<true>(rows);
            refresh_roof();
            validate();
        }

        void validate() const
        {
#ifdef NDEBUG
            (void)0;
#else
            unsigned const exact = static_cast<unsigned>(occupancy_.highest_y());
            assert(roof_ == exact);
            for (int i = 0; i < occupancy_t::num_of_under; ++i)
            {
                assert((occupancy_.raw()[i] & ~uint64_t(0x0fffffffffffffffull)) == 0);
            }
#endif
        }

        size_t hash() const
        {
            uint64_t h = 1469598103934665603ull;
            for (int i = 0; i < occupancy_t::num_of_under; ++i)
            {
                uint64_t word = occupancy_.raw()[i] & uint64_t(0x0fffffffffffffffull);
                h ^= word;
                h *= 1099511628211ull;
            }
            return static_cast<size_t>(h);
        }

    private:
        constexpr void refresh_roof()
        {
            roof_ = static_cast<unsigned>(occupancy_.highest_y());
        }

        void assert_valid_mask(occupancy_t const &mask)
        {
#ifndef NDEBUG
            mask.for_each_bit([&](int x, int y) {
                assert(!full(x, y));
            });
#else
            (void)mask;
#endif
        }

        occupancy_t occupancy_{};
        unsigned roof_ = 0;
    };

    struct Board::ClearResult
    {
        Board board;
        int count;
    };

    inline Board::ClearResult Board::cleared() const
    {
        auto result = occupancy_.clear_full_lines();
        Board board;
        board.occupancy_ = result.board;
        board.refresh_roof();
        board.validate();
        return {board, result.count};
    }
}
