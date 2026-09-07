#pragma once

#include "candidate_format.h"
#include "tetris_types.h"
#include "toj_rule.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace legacy_cmp
{
    struct NormalizedKey
    {
        candfmt::Cells occupied_cells{};
        std::uint64_t cells_hash = 0;
        std::uint64_t channel = 0;
        bool matched = false;
        std::uint32_t opaque = 0;

        bool operator==(NormalizedKey const &other) const
        {
            if (matched && other.matched)
            {
                return occupied_cells == other.occupied_cells && channel == other.channel;
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
                if (occupied_cells != other.occupied_cells)
                {
                    return occupied_cells < other.occupied_cells;
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
        key.occupied_cells = sorted;
        key.cells_hash = candfmt::occupancy_hash(sorted);
        key.channel = *piece == tetris::Piece::T
            ? static_cast<std::uint64_t>(spin_channel(spin_class, last_rotate))
            : 0;
        key.matched = true;
        return key;
    }

    class ProbeDedup
    {
    public:
        ProbeDedup()
        {
            slots_.resize(initial_capacity);
        }

        void begin_call()
        {
            for (std::size_t index : touched_)
            {
                slots_[index].used = false;
                slots_[index].count = 0;
            }
            touched_.clear();
            distinct_ = 0;
        }

        void add(NormalizedKey const &key)
        {
            if ((touched_.size() + 1) * 4 >= slots_.size() * 3)
            {
                grow();
            }
            std::size_t mask = slots_.size() - 1;
            std::size_t index = hash_key(key) & mask;
            while (true)
            {
                Slot &slot = slots_[index];
                if (!slot.used)
                {
                    slot.used = true;
                    slot.key = key;
                    slot.count = 1;
                    touched_.push_back(index);
                    ++distinct_;
                    return;
                }
                if (slot.key == key)
                {
                    ++slot.count;
                    return;
                }
                index = (index + 1) & mask;
            }
        }

        std::size_t distinct() const
        {
            return distinct_;
        }

        std::size_t unmatched() const
        {
            std::size_t count = 0;
            for (std::size_t index : touched_)
            {
                if (!slots_[index].key.matched)
                {
                    ++count;
                }
            }
            return count;
        }

        std::size_t capacity() const
        {
            return slots_.size();
        }

    private:
        struct Slot
        {
            NormalizedKey key;
            std::uint64_t count = 0;
            bool used = false;
        };

        static constexpr std::size_t initial_capacity = 512;

        static std::uint64_t hash_key(NormalizedKey const &key)
        {
            std::uint64_t hash = candfmt::fnv_offset;
            hash = candfmt::fnv_mix(hash, key.cells_hash);
            hash = candfmt::fnv_mix(hash, key.channel);
            hash = candfmt::fnv_mix(hash, key.matched ? 1u : 0u);
            if (!key.matched)
            {
                hash = candfmt::fnv_mix(hash, static_cast<std::uint64_t>(key.opaque));
            }
            return hash;
        }

        void grow()
        {
            std::vector<std::size_t> live;
            live.swap(touched_);
            std::size_t const grown = slots_.size() * 2;
            std::vector<Slot> fresh(grown);
            slots_.swap(fresh);
            std::size_t mask = slots_.size() - 1;
            for (std::size_t index : live)
            {
                std::size_t probe = hash_key(fresh[index].key) & mask;
                while (slots_[probe].used)
                {
                    probe = (probe + 1) & mask;
                }
                slots_[probe] = fresh[index];
                slots_[probe].used = true;
                touched_.push_back(probe);
            }
        }

        std::vector<Slot> slots_;
        std::vector<std::size_t> touched_;
        std::size_t distinct_ = 0;
    };
}
