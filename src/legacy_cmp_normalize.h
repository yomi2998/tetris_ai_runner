#pragma once

#include "candidate_format.h"
#include "tetris_types.h"
#include "toj_rule.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace legacy_cmp
{
    struct NormalizedKey
    {
        std::uint64_t cells_hash = 0;
        std::uint64_t channel = 0;
        bool matched = false;
        std::uint32_t opaque = 0;

        bool operator==(NormalizedKey const &other) const
        {
            if (matched && other.matched)
            {
                return cells_hash == other.cells_hash && channel == other.channel;
            }
            if (!matched && !other.matched)
            {
                return opaque == other.opaque;
            }
            return false;
        }

        bool operator<(NormalizedKey const &other) const
        {
            if (matched != other.matched)
            {
                return matched < other.matched;
            }
            if (matched)
            {
                if (cells_hash != other.cells_hash)
                {
                    return cells_hash < other.cells_hash;
                }
                return channel < other.channel;
            }
            return opaque < other.opaque;
        }
    };

    inline int spin_channel(int spin_class, bool last_rotate)
    {
        return spin_class * 2 + (last_rotate ? 1 : 0);
    }

    inline NormalizedKey normalize_land_point(char piece_char, int x, int y, int rotation,
        int spin_class, bool last_rotate, std::uint32_t status_bits)
    {
        NormalizedKey key;
        key.opaque = status_bits;
        auto piece = tetris::try_from_char(piece_char);
        if (!piece.has_value())
        {
            return key;
        }
        int normalized_rotation = rotation;
        if (*piece == tetris::Piece::O)
        {
            normalized_rotation = 0;
        }
        auto placement = tetris::toj::ExternalPoseTransform::to_placement(
            *piece, x, y, normalized_rotation);
        if (!placement.has_value())
        {
            return key;
        }
        auto cells = tetris::toj::cells(*piece, *placement);
        if (!cells.has_value())
        {
            return key;
        }
        candfmt::Cells sorted = *cells;
        std::sort(sorted.begin(), sorted.end());
        key.cells_hash = candfmt::occupancy_hash(sorted);
        key.channel = *piece == tetris::Piece::T
            ? static_cast<std::uint64_t>(spin_channel(spin_class, last_rotate))
            : 0;
        key.matched = true;
        return key;
    }
}
