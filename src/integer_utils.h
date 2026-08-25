
#pragma once


#include <cstddef>
#include <cstdint>
#include <bit>
#if __SSE4_2__
#   include <nmmintrin.h>
#endif

namespace zzz
{
    // Hardware popcount when SSE4.2 is available, software fallback otherwise.
    inline size_t BitCount(uint32_t n)
    {
#if __SSE4_2__
        return size_t(_mm_popcnt_u32(n));
#else
        return size_t(std::popcount(n));
#endif
    }

    // Trailing zero count; returns 32 for n == 0 (matching the old convention).
    inline size_t NumberOfTrailingZeros(uint32_t i)
    {
        return i == 0 ? 32 : size_t(std::countr_zero(i));
    }
}
