#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace legacy_cmp
{
    inline std::int64_t steady_nanos()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    struct Counts
    {
        std::uint64_t eval_requests = 0;
        std::uint64_t eval_hits = 0;
        std::uint64_t eval_calls = 0;
        std::uint64_t widening_iters = 0;
        std::uint64_t parent_expansions = 0;
        std::uint64_t fresh_nodes = 0;
        std::uint64_t recycled_nodes = 0;
        std::uint64_t root_nodes = 0;
        std::uint64_t reused_nodes = 0;
        std::int64_t eval_hit_ns = 0;
        std::int64_t eval_miss_ns = 0;
        std::int64_t parent_ns = 0;
        std::uint64_t path_valid_states = 0;
    };

    struct Observer
    {
        bool timers_enabled = true;
        std::function<std::int64_t()> clock = steady_nanos;
        Counts counts;
        const void *path_mark_target = nullptr;
        std::vector<const void *> path_marks;

        void reset()
        {
            counts = Counts{};
            path_mark_target = nullptr;
            path_marks.clear();
        }

        std::int64_t now() const
        {
            return timers_enabled ? clock() : 0;
        }
    };

    inline Observer *&slot()
    {
        static Observer *current = nullptr;
        return current;
    }

    inline Observer *observer()
    {
        return slot();
    }

    inline void attach(Observer *observer)
    {
        slot() = observer;
    }

    struct AttachGuard
    {
        explicit AttachGuard(Observer *observer)
        {
            attach(observer);
        }

        ~AttachGuard()
        {
            attach(nullptr);
        }

        AttachGuard(AttachGuard const &) = delete;
        AttachGuard &operator=(AttachGuard const &) = delete;
    };
}
