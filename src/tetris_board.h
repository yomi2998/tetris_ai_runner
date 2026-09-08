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
            board.canonicalize();
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
            assert(y >= 0 && y < height);
            int const yi = y / occupancy_t::lines_per_under;
            int const off = (y % occupancy_t::lines_per_under) * width;
            return static_cast<uint16_t>((occupancy_.logical_word(yi) >> off) & row_mask);
        }

        constexpr bool full(int x, int y) const
        {
            assert(x >= 0 && x < width && y >= 0 && y < height);
            return occupancy_.get(x, y) == 1;
        }

        constexpr occupancy_t const &occupancy() const
        {
            return occupancy_;
        }

        constexpr unsigned roof() const
        {
            return static_cast<unsigned>(occupancy_.highest_y());
        }

        constexpr bool empty() const
        {
            std::uint64_t bits = 0;
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                bits |= occupancy_.logical_word(i);
            }
            return bits == 0;
        }

        std::array<size_t, width> column_tops() const
        {
            return occupancy_.column_tops();
        }

        constexpr bool operator==(Board const &other) const
        {
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                if (occupancy_.logical_word(i) != other.occupancy_.logical_word(i))
                {
                    return false;
                }
            }
            return true;
        }

        constexpr bool operator!=(Board const &other) const
        {
            return !(*this == other);
        }

        bool apply(occupancy_t const &mask)
        {
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                if (mask.logical_word(i) & occupancy_.logical_word(i))
                {
                    return false;
                }
            }
            apply_unchecked(mask);
            return true;
        }

        void apply_unchecked(occupancy_t const &mask)
        {
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                occupancy_.set_logical_word(i, occupancy_.logical_word(i) | mask.logical_word(i));
            }
            validate();
        }

        struct ClearResult;

        ClearResult cleared() const;

        void add_garbage(int lines, uint16_t garbage_row)
        {
            if (lines <= 0)
            {
                return;
            }
            auto rows = occupancy_.template to_row_bitboard<true>();
            auto const fill = static_cast<occupancy_t::row_t>(garbage_row & row_mask);
            if (lines >= height)
            {
                for (int y = 0; y < height; ++y)
                {
                    rows[y] = fill;
                }
            }
            else
            {
                for (int y = height - 1; y >= lines; --y)
                {
                    rows[y] = rows[y - lines];
                }
                for (int y = 0; y < lines; ++y)
                {
                    rows[y] = fill;
                }
            }
            occupancy_.from_row_bitboard<true>(rows);
            canonicalize();
            validate();
        }

        void validate() const
        {
#ifdef NDEBUG
            (void)0;
#else
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                assert(occupancy_.logical_word(i) == occupancy_.raw()[i]);
            }
#endif
        }

        constexpr size_t hash() const
        {
            uint64_t h = 1469598103934665603ull;
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                h ^= occupancy_.logical_word(i);
                h *= 1099511628211ull;
            }
            return static_cast<size_t>(h);
        }

    private:
        constexpr void canonicalize()
        {
            for (int i = 0; i < occupancy_t::word_count(); ++i)
            {
                occupancy_.set_logical_word(i, occupancy_.logical_word(i));
            }
        }

        occupancy_t occupancy_{};
    };

    static_assert(sizeof(Board) == sizeof(Board::occupancy_t));

    struct Board::ClearResult
    {
        Board board;
        int count;
        Board::occupancy_t full_rows;

        bool cleared_row(int y) const
        {
            return full_rows.get(Board::width - 1, y) == 1;
        }
    };

    inline Board::ClearResult Board::cleared() const
    {
        auto result = occupancy_.clear_full_lines();
        Board board;
        board.occupancy_ = result.board;
        board.canonicalize();
        board.validate();
        return {board, result.count, result.full_rows};
    }
}
