#include "tournament_bytes.h"

#include <cstdint>
#include <print>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool condition, std::string const &name)
    {
        std::println("{}: {}", condition ? "PASS" : "FAIL", name);
        if (!condition)
        {
            ++failures;
        }
    }
}

int main()
{
    std::vector<std::uint8_t> const bytes = {0, 1, 15, 16, 127, 128, 254, 255};
    std::string const encoded = tournament_bytes::encode_hex(bytes);
    check(encoded == "00010f107f80feff", "encoding is canonical lowercase hex");
    auto const decoded = tournament_bytes::decode_hex(encoded);
    check(decoded.has_value() && *decoded == bytes, "hex round trip preserves bytes");
    check(tournament_bytes::encode_hex({}).empty(), "empty bytes encode to empty text");
    auto const empty = tournament_bytes::decode_hex("");
    check(empty.has_value() && empty->empty(), "empty text decodes to empty bytes");
    check(!tournament_bytes::decode_hex("0").has_value(), "odd hex length is rejected");
    check(!tournament_bytes::decode_hex("0g").has_value(), "non-hex digit is rejected");
    check(!tournament_bytes::decode_hex("AF").has_value(), "noncanonical uppercase hex is rejected");
    std::println("{} byte codec test failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
