#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace refa {

struct Candidate {
    uint8_t x;
    uint8_t y;
    uint8_t rot;
    uint8_t arrival;

    constexpr bool operator==(Candidate const &) const = default;
    constexpr bool operator<(Candidate const &o) const
    {
        if (rot != o.rot)
        {
            return rot < o.rot;
        }
        if (y != o.y)
        {
            return y < o.y;
        }
        if (x != o.x)
        {
            return x < o.x;
        }
        return arrival < o.arrival;
    }
};

inline uint64_t fnv1a(std::vector<Candidate> const &sorted)
{
    uint64_t h = 1469598103934665603ull;
    for (auto const &c : sorted)
    {
        h ^= c.x;
        h *= 1099511628211ull;
        h ^= c.y;
        h *= 1099511628211ull;
        h ^= c.rot;
        h *= 1099511628211ull;
        h ^= c.arrival;
        h *= 1099511628211ull;
    }
    return h;
}

inline std::string hex64(uint64_t v)
{
    char buf[17] = {};
    char const *digits = "0123456789abcdef";
    for (int i = 15; i >= 0; --i)
    {
        buf[i] = digits[v & 15];
        v >>= 4;
    }
    return std::string(buf, 16);
}

}
