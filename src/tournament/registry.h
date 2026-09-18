#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
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

    struct TrustRecord
    {
        DeviceId device = 0;
        PublicKey public_key;
        int audits_passed = 0;
        int audits_failed = 0;
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

        bool load_trust(std::string const &path, std::string &error);
        void set_trust_file(std::string const &path);
        std::vector<TrustRecord> trust_snapshot() const;

        void set_ban_file(std::string const &path);

    private:
        struct Entry
        {
            PublicKey public_key;
            DeviceStats stats;
        };

        void persist_trust_locked() const;
        void refresh_bans_locked() const;

        mutable std::mutex mutex_;
        std::map<DeviceId, Entry> devices_;
        mutable std::vector<PublicKey> banned_keys_;
        std::map<PublicKey, TrustRecord> trust_preload_;
        std::string trust_file_;
        std::string ban_file_;
        mutable std::uint64_t ban_stamp_ = 0;
        mutable std::uintmax_t ban_bytes_ = 0;
        mutable bool ban_checked_ = false;
    };

    bool remove_trust_record(std::string const &path, PublicKey const &public_key, bool &removed,
                             std::string &error);
}
