#pragma once

#include "fast-reachability/piece_tetromino.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace tetris
{
    using Piece = reachability::block_type;

    inline constexpr size_t piece_count = 7;

    constexpr char to_char(Piece piece)
    {
        return reachability::rules::Tetromino::name_of(piece);
    }

    constexpr std::optional<Piece> try_from_char(char c)
    {
        switch (c)
        {
        case 'T': return Piece::T;
        case 'Z': return Piece::Z;
        case 'S': return Piece::S;
        case 'J': return Piece::J;
        case 'L': return Piece::L;
        case 'O': return Piece::O;
        case 'I': return Piece::I;
        default: return std::nullopt;
        }
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
        constexpr Placement() = default;

        static constexpr std::optional<Placement> try_make(int x, int y, int rotation)
        {
            if (x < 0 || x >= 10 || y < 0 || y >= 48 || rotation < 0 || rotation > 3)
            {
                return std::nullopt;
            }
            return unchecked(x, y, rotation);
        }

        static constexpr Placement unchecked(int x, int y, int rotation)
        {
            assert(x >= 0 && x < 10 && y >= 0 && y < 48 && rotation >= 0 && rotation <= 3);
            return Placement{pack(x, y, rotation)};
        }

        constexpr uint16_t packed() const
        {
            return data;
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

    private:
        uint16_t data = 0;

        constexpr explicit Placement(uint16_t raw)
            : data(raw)
        {
        }

        static constexpr uint16_t pack(int x, int y, int rotation)
        {
            return static_cast<uint16_t>(x & 0xf | (y & 0x3f) << 4 | (rotation & 0x3) << 10);
        }
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

    static_assert(sizeof(Placement) == 2);
    static_assert(std::is_trivially_copyable_v<Placement>);
    static_assert(std::is_standard_layout_v<Placement>);
    static_assert(std::is_trivially_copyable_v<Candidate>);
    static_assert(std::is_trivially_copyable_v<Outcome>);
}
