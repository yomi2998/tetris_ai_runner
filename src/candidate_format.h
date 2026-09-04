#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <print>
#include <string_view>
#include <utility>
#include <vector>

namespace candfmt
{

inline constexpr uint64_t fnv_offset = 0xcbf29ce484222325ull;
inline constexpr uint64_t fnv_prime = 0x100000001b3ull;

inline uint64_t fnv_mix(uint64_t hash, uint64_t value)
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & 0xffu;
        hash *= fnv_prime;
    }
    return hash;
}

using Cells = std::array<std::pair<int, int>, 4>;

inline uint64_t occupancy_hash(Cells const &cells)
{
    std::array<int, 4> codes;
    for (int i = 0; i < 4; ++i)
    {
        codes[i] = cells[i].second * 1000 + cells[i].first;
    }
    std::sort(codes.begin(), codes.end());
    uint64_t hash = fnv_offset;
    for (int code : codes)
    {
        hash = fnv_mix(hash, static_cast<uint64_t>(static_cast<int64_t>(code)));
    }
    return hash;
}

class Report
{
public:
    void begin_case(char piece, std::size_t board_index, bool keep_arrival)
    {
        piece_ = piece;
        board_ = board_index;
        keep_arrival_ = keep_arrival;
        keys_.clear();
    }

    void add_candidate(uint64_t occupancy, int arrival)
    {
        int const channel = keep_arrival_ ? (arrival != 0 ? 1 : 0) : 0;
        keys_.push_back(fnv_mix(occupancy, static_cast<uint64_t>(channel)));
    }

    void end_case()
    {
        std::sort(keys_.begin(), keys_.end());
        keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
        uint64_t hash = fnv_offset;
        for (uint64_t key : keys_)
        {
            hash = fnv_mix(hash, key);
        }
        std::println("CASE {} {:>3} {:>5} {:016x}", piece_, board_, keys_.size(), hash);
        corpus_ = fnv_mix(corpus_, hash);
        corpus_ = fnv_mix(corpus_, static_cast<uint64_t>(keys_.size()));
        total_ += keys_.size();
        ++cases_;
    }

    void print_corpus(std::string_view producer) const
    {
        std::println("CORPUS {} cases {} candidates {} {:016x}", producer, cases_, total_, corpus_);
    }

    std::size_t cases() const noexcept
    {
        return cases_;
    }

private:
    char piece_ = '?';
    std::size_t board_ = 0;
    bool keep_arrival_ = false;
    std::size_t cases_ = 0;
    std::size_t total_ = 0;
    uint64_t corpus_ = fnv_offset;
    std::vector<uint64_t> keys_;
};

} // namespace candfmt
