#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tournament/wire.h"

namespace tournament_ban
{
    using tournament_wire::DeviceId;
    using tournament_wire::PublicKey;

    struct BanRecord
    {
        DeviceId device = 0;
        PublicKey public_key;
        std::uint64_t generation = 0;
        std::uint64_t failed_verdicts = 0;
        std::uint64_t caught_at_ms = 0;
    };

    class BanFile
    {
    public:
        explicit BanFile(std::string path);

        bool load(std::vector<BanRecord> &records, std::string &error) const;
        bool append(BanRecord const &record, std::string &error);
        bool remove(PublicKey const &public_key, bool &removed, std::string &error) const;
        std::size_t record_count() const;

    private:
        std::string path_;
        std::size_t appended_ = 0;
    };
}
