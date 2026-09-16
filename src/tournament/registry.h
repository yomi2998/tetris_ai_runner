#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <vector>

#include "tournament/wire.h"

namespace tournament_registry
{
    using tournament_wire::DeviceId;
    using tournament_wire::PublicKey;

    struct DeviceStats
    {
        int games_accepted = 0;
        int games_dropped = 0;
        int audits_passed = 0;
        int audits_failed = 0;
        bool blacklisted = false;
        std::uint32_t concurrent_assignments = 0;

        bool operator==(DeviceStats const &) const = default;
    };

    class DeviceRegistry
    {
    public:
        bool enroll(DeviceId device, PublicKey const &public_key);
        bool enrolled(DeviceId device) const;
        PublicKey const *public_key(DeviceId device) const;
        DeviceStats const *stats(DeviceId device) const;
        bool blacklist(DeviceId device);
        bool blacklisted(DeviceId device) const;
        bool ban_key(PublicKey const &public_key);
        bool key_banned(PublicKey const &public_key) const;
        bool set_concurrency(DeviceId device, std::uint32_t concurrent_assignments);
        std::uint32_t concurrency(DeviceId device) const;
        bool record_accepted(DeviceId device, int games);
        bool record_dropped(DeviceId device, int games);
        bool record_audit(DeviceId device, bool passed);
        std::vector<DeviceId> active_devices() const;
        std::size_t size() const;

    private:
        struct Entry
        {
            PublicKey public_key;
            DeviceStats stats;
        };

        mutable std::mutex mutex_;
        std::map<DeviceId, Entry> devices_;
        std::vector<PublicKey> banned_keys_;
    };
}
