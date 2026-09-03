#pragma once

#include "fast-reachability/piece_tetromino.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace tetris
{
    using Piece = reachability::block_type;

    inline constexpr size_t piece_count = 7;

    constexpr char to_char(Piece piece)
    {
        return reachability::rules::Tetromino::name_of(piece);
    }

    constexpr Piece from_char(char c)
    {
        return reachability::rules::Tetromino::from_name(c);
    }

    enum class ArrivalClass : uint8_t
    {
        Normal,
        TerminalRotation,
    };

    enum class SpinType : uint8_t
    {
        None,
        Mini,
        Full,
    };

    enum class Move : uint8_t
    {
        Left,
        Right,
        SoftDrop,
        SonicDrop,
        RotateCW,
        RotateCCW,
        Rotate180,
        Hold,
    };

    struct Placement
    {
        uint16_t data = 0;

        constexpr Placement() = default;

        constexpr Placement(int x, int y, int rotation)
            : data(static_cast<uint16_t>((x & 0xf) | (y & 0x3f) << 4 | (rotation & 0x3) << 10))
        {
        }

        constexpr int x() const
        {
            return data & 0xf;
        }

        constexpr int y() const
        {
            return data >> 4 & 0x3f;
        }

        constexpr int rotation() const
        {
            return data >> 10 & 0x3;
        }

        constexpr bool operator==(Placement const &) const = default;
    };

    struct Candidate
    {
        Placement placement;
        ArrivalClass arrival = ArrivalClass::Normal;

        constexpr bool operator==(Candidate const &) const = default;
    };

    struct Outcome
    {
        SpinType spin = SpinType::None;
        int clear_count = 0;
        bool lockout = false;

        constexpr bool operator==(Outcome const &) const = default;
    };

    struct DecisionContext
    {
        std::span<Piece const> next;
        std::optional<Piece> hold;
        bool used_hold = false;
        size_t depth = 0;
    };
}
