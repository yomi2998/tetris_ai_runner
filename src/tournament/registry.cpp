#include "tournament/registry.h"

namespace tournament_registry
{
    bool DeviceRegistry::enroll(DeviceId device, PublicKey const &public_key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return devices_.emplace(device, Entry{public_key, DeviceStats{}}).second;
    }

    bool DeviceRegistry::enrolled(DeviceId device) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return devices_.find(device) != devices_.end();
    }

    PublicKey const *DeviceRegistry::public_key(DeviceId device) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        return it == devices_.end() ? nullptr : &it->second.public_key;
    }

    DeviceStats const *DeviceRegistry::stats(DeviceId device) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        return it == devices_.end() ? nullptr : &it->second.stats;
    }

    bool DeviceRegistry::blacklist(DeviceId device)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = devices_.find(device);
        if (it == devices_.end())
        {
            return false;
        }
        it->second.stats.blacklisted = true;
        return true;
    }

    bool DeviceRegistry::blacklisted(DeviceId device) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        return it != devices_.end() && it->second.stats.blacklisted;
    }

    bool DeviceRegistry::record_accepted(DeviceId device, int games)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        if (it == devices_.end())
        {
            return false;
        }
        it->second.stats.games_accepted += games;
        return true;
    }

    bool DeviceRegistry::record_dropped(DeviceId device, int games)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        if (it == devices_.end())
        {
            return false;
        }
        it->second.stats.games_dropped += games;
        return true;
    }

    bool DeviceRegistry::record_audit(DeviceId device, bool passed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        if (it == devices_.end())
        {
            return false;
        }
        if (passed)
        {
            it->second.stats.audits_passed += 1;
        }
        else
        {
            it->second.stats.audits_failed += 1;
        }
        return true;
    }

    std::vector<DeviceId> DeviceRegistry::active_devices() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<DeviceId> active;
        for (auto const &entry : devices_)
        {
            if (!entry.second.stats.blacklisted)
            {
                active.push_back(entry.first);
            }
        }
        return active;
    }

    std::size_t DeviceRegistry::size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return devices_.size();
    }
}
