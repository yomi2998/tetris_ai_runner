#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tournament_bytes
{
    inline std::string encode_hex(std::span<std::uint8_t const> bytes)
    {
        static constexpr char digits[] = "0123456789abcdef";
        if (bytes.size() > std::numeric_limits<std::size_t>::max() / 2)
        {
            return {};
        }
        std::string encoded;
        encoded.resize(bytes.size() * 2);
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            encoded[i * 2] = digits[bytes[i] >> 4];
            encoded[i * 2 + 1] = digits[bytes[i] & 0x0F];
        }
        return encoded;
    }

    inline std::optional<std::vector<std::uint8_t>> decode_hex(std::string const &encoded)
    {
        if (encoded.size() % 2 != 0)
        {
            return std::nullopt;
        }
        auto value = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return -1;
        };
        std::vector<std::uint8_t> bytes(encoded.size() / 2);
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            int const high = value(encoded[i * 2]);
            int const low = value(encoded[i * 2 + 1]);
            if (high < 0 || low < 0)
            {
                return std::nullopt;
            }
            bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return bytes;
    }
}
