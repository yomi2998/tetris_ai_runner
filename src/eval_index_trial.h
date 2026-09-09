#pragma once
#ifdef TETRIS_EVAL_INDEX_TRIAL
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#ifndef TETRIS_EVAL_INDEX_SLOTS
#error TETRIS_EVAL_INDEX_SLOTS is required for the trial index
#endif
#ifndef TETRIS_EVAL_INDEX_TAGGED
#error TETRIS_EVAL_INDEX_TAGGED is required for the trial index
#endif
static_assert(TETRIS_EVAL_INDEX_SLOTS > 0
        && (TETRIS_EVAL_INDEX_SLOTS & (TETRIS_EVAL_INDEX_SLOTS - 1)) == 0,
    "trial index slots are a nonzero power of two");
static_assert(TETRIS_EVAL_INDEX_TAGGED == 0 || TETRIS_EVAL_INDEX_TAGGED == 1,
    "trial index tagged flag is zero or one");
namespace tetris_engine
{
inline std::uint64_t eval_index_finalize(std::uint64_t fingerprint)
{
    std::uint64_t mixed = fingerprint ^ (fingerprint >> 33);
    mixed *= 0xFF51AFD7ED558CCDu;
    mixed ^= mixed >> 33;
    mixed *= 0xC4CEB9FE1A85EC53u;
    mixed ^= mixed >> 33;
    return mixed;
}
struct EvalIndexTrial
{
    static constexpr std::uint32_t kEmpty = 0xFFFFFFFFu;
    static constexpr std::size_t kSlots = TETRIS_EVAL_INDEX_SLOTS;
    static constexpr bool kTagged = TETRIS_EVAL_INDEX_TAGGED == 1;
    static constexpr std::uint64_t kIndexBytes =
        static_cast<std::uint64_t>(kSlots) * 4u
        + (kTagged ? static_cast<std::uint64_t>(kSlots) * 4u : 0u);
    std::vector<std::uint32_t> slots;
    std::vector<std::uint32_t> tags;
    std::uint64_t requests = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t replacements = 0;
    std::uint64_t insertions = 0;
    std::uint64_t clears = 0;
    std::uint64_t tag_mismatches = 0;
    bool telemetry = true;
    void allocate()
    {
        slots.assign(kSlots, kEmpty);
        if (kTagged)
        {
            tags.assign(kSlots, 0u);
        }
        else
        {
            tags.clear();
        }
    }
    void clear_slots()
    {
        if (!slots.empty())
        {
            std::fill(slots.begin(), slots.end(), kEmpty);
            ++clears;
        }
    }
    void reset_counters()
    {
        requests = 0;
        hits = 0;
        misses = 0;
        replacements = 0;
        insertions = 0;
        tag_mismatches = 0;
    }
    std::size_t reserved_bytes() const
    {
        return slots.size() * sizeof(std::uint32_t)
            + tags.size() * sizeof(std::uint32_t);
    }
};
}
#endif
