#pragma once

#include "tetris_core.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef TETRIS_LEGACY_HOST_DIAG
namespace legacy_host_diag
{
    template <class Result>
    class EvalAudit
    {
    public:
        struct Entry
        {
            std::uint64_t hash = 0;
            std::uint32_t depth = 0;
            std::uint64_t epoch = 0;
            m_tetris::TetrisMap map{};
            Result value{};
        };

        static constexpr std::size_t capacity = 1u << 17;

        static EvalAudit &instance()
        {
            static EvalAudit audit;
            return audit;
        }

        void reset()
        {
            ++epoch_;
        }

        std::uint64_t epoch() const
        {
            return epoch_;
        }

        bool active(Entry const &entry) const
        {
            return entry.epoch == epoch_;
        }

        Entry &slot(std::uint64_t hash, std::uint32_t depth)
        {
            return entries_[(hash ^ (static_cast<std::uint64_t>(depth) * 0x9E3779B97F4A7C15ull))
                & (capacity - 1)];
        }

        std::uint64_t reserved_bytes() const
        {
            return sizeof(Entry) * capacity;
        }

        std::uint64_t requests = 0;
        std::uint64_t legacy_hits = 0;
        std::uint64_t collisions = 0;
        std::uint64_t exact_hits = 0;
        std::uint64_t fresh_evals = 0;
        std::uint64_t evictions = 0;
        std::uint64_t retargets = 0;

    private:
        std::vector<Entry> entries_ = std::vector<Entry>(capacity);
        std::uint64_t epoch_ = 1;
    };

    inline bool &audit_enabled()
    {
        static bool enabled = false;
        return enabled;
    }

    inline bool &exact_only()
    {
        static bool enabled = false;
        return enabled;
    }
}
#endif
