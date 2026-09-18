#include "tournament/registry.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "tournament/ban_file.h"
#include "tournament/bytes.h"

namespace tournament_registry
{
    bool DeviceRegistry::enroll(DeviceId device, PublicKey const &public_key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (PublicKey const &banned : banned_keys_)
        {
            if (banned == public_key)
            {
                return false;
            }
        }
        auto [it, inserted] = devices_.emplace(device, Entry{public_key, DeviceStats{}});
        if (inserted)
        {
            auto const trust = trust_preload_.find(public_key);
            if (trust != trust_preload_.end())
            {
                it->second.stats.audits_passed = trust->second.audits_passed;
                it->second.stats.audits_failed = trust->second.audits_failed;
            }
        }
        return inserted;
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

    bool DeviceRegistry::ban_key(PublicKey const &public_key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (PublicKey const &banned : banned_keys_)
        {
            if (banned == public_key)
            {
                return false;
            }
        }
        banned_keys_.push_back(public_key);
        for (auto &entry : devices_)
        {
            if (entry.second.public_key == public_key)
            {
                entry.second.stats.blacklisted = true;
            }
        }
        return true;
    }

    bool DeviceRegistry::key_banned(PublicKey const &public_key) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        refresh_bans_locked();
        for (PublicKey const &banned : banned_keys_)
        {
            if (banned == public_key)
            {
                return true;
            }
        }
        return false;
    }

    void DeviceRegistry::set_ban_file(std::string const &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ban_file_ = path;
        ban_checked_ = false;
    }

    void DeviceRegistry::refresh_bans_locked() const
    {
        if (ban_file_.empty())
        {
            return;
        }
        std::error_code stat_error;
        std::filesystem::file_time_type const written
            = std::filesystem::last_write_time(ban_file_, stat_error);
        if (stat_error)
        {
            if (stat_error == std::errc::no_such_file_or_directory && !banned_keys_.empty())
            {
                banned_keys_.clear();
                ban_checked_ = true;
                ban_stamp_ = 0;
                ban_bytes_ = 0;
            }
            return;
        }
        std::uintmax_t const bytes = std::filesystem::file_size(ban_file_, stat_error);
        if (stat_error)
        {
            return;
        }
        std::uint64_t const stamp
            = static_cast<std::uint64_t>(written.time_since_epoch().count());
        if (ban_checked_ && stamp == ban_stamp_ && bytes == ban_bytes_)
        {
            return;
        }
        tournament_ban::BanFile store(ban_file_);
        std::vector<tournament_ban::BanRecord> records;
        std::string load_error;
        if (!store.load(records, load_error))
        {
            return;
        }
        banned_keys_.clear();
        for (tournament_ban::BanRecord &record : records)
        {
            banned_keys_.push_back(std::move(record.public_key));
        }
        ban_stamp_ = stamp;
        ban_bytes_ = bytes;
        ban_checked_ = true;
    }

    bool DeviceRegistry::set_concurrency(DeviceId device, std::uint32_t concurrent_assignments)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        if (it == devices_.end())
        {
            return false;
        }
        it->second.stats.concurrent_assignments = concurrent_assignments;
        return true;
    }

    std::uint32_t DeviceRegistry::concurrency(DeviceId device) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto const it = devices_.find(device);
        return it == devices_.end() ? 0 : it->second.stats.concurrent_assignments;
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
        if (!trust_file_.empty())
        {
            persist_trust_locked();
        }
        return true;
    }

    bool DeviceRegistry::load_trust(std::string const &path, std::string &error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "cannot open " + path;
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        trust_preload_.clear();
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }
            std::istringstream fields(line);
            std::string device_text;
            std::string key_text;
            int passed = 0;
            int failed = 0;
            if (!(fields >> device_text >> key_text >> passed >> failed) || key_text.size() != 64)
            {
                error = path + " line " + std::to_string(line_number)
                    + ": expected '<device id> <64 hex public key> <audits passed> <audits failed>'";
                return false;
            }
            std::optional<std::vector<std::uint8_t>> const key = tournament_bytes::decode_hex(key_text);
            if (!key || key->size() != 32)
            {
                error = path + " line " + std::to_string(line_number) + ": malformed public key hex";
                return false;
            }
            TrustRecord record;
            record.public_key = *key;
            DeviceId parsed_device = 0;
            std::istringstream(device_text) >> parsed_device;
            record.device = parsed_device;
            record.audits_passed = passed;
            record.audits_failed = failed;
            trust_preload_[record.public_key] = record;
        }
        return true;
    }

    void DeviceRegistry::set_trust_file(std::string const &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        trust_file_ = path;
    }

    std::vector<TrustRecord> DeviceRegistry::trust_snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<PublicKey, TrustRecord> merged;
        for (auto const &entry : trust_preload_)
        {
            merged[entry.first] = entry.second;
        }
        for (auto const &entry : devices_)
        {
            TrustRecord record;
            record.device = entry.first;
            record.public_key = entry.second.public_key;
            record.audits_passed = entry.second.stats.audits_passed;
            record.audits_failed = entry.second.stats.audits_failed;
            merged[record.public_key] = record;
        }
        std::vector<TrustRecord> records;
        records.reserve(merged.size());
        for (auto &value : merged)
        {
            records.push_back(std::move(value.second));
        }
        return records;
    }

    void DeviceRegistry::persist_trust_locked() const
    {
        std::map<PublicKey, TrustRecord> merged;
        for (auto const &entry : trust_preload_)
        {
            merged[entry.first] = entry.second;
        }
        for (auto const &entry : devices_)
        {
            TrustRecord record;
            record.device = entry.first;
            record.public_key = entry.second.public_key;
            record.audits_passed = entry.second.stats.audits_passed;
            record.audits_failed = entry.second.stats.audits_failed;
            merged[record.public_key] = record;
        }
        std::string const path = trust_file_ + ".tmp";
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            for (auto const &entry : merged)
            {
                output << entry.second.device << ' '
                       << tournament_bytes::encode_hex(entry.second.public_key) << ' '
                       << entry.second.audits_passed << ' ' << entry.second.audits_failed << '\n';
            }
        }
        std::error_code rename_error;
        std::filesystem::rename(path, trust_file_, rename_error);
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

    bool remove_trust_record(std::string const &path, PublicKey const &public_key, bool &removed,
                             std::string &error)
    {
        removed = false;
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            return true;
        }
        std::vector<TrustRecord> kept;
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }
            std::istringstream fields(line);
            std::string device_text;
            std::string key_text;
            int passed = 0;
            int failed = 0;
            if (!(fields >> device_text >> key_text >> passed >> failed) || key_text.size() != 64)
            {
                error = path + " line " + std::to_string(line_number)
                    + ": expected '<device id> <64 hex public key> <audits passed> <audits failed>'";
                return false;
            }
            std::optional<std::vector<std::uint8_t>> const key = tournament_bytes::decode_hex(key_text);
            if (!key || key->size() != 32)
            {
                error = path + " line " + std::to_string(line_number) + ": malformed public key hex";
                return false;
            }
            if (*key == public_key)
            {
                removed = true;
                continue;
            }
            TrustRecord record;
            record.public_key = *key;
            DeviceId parsed_device = 0;
            std::istringstream(device_text) >> parsed_device;
            record.device = parsed_device;
            record.audits_passed = passed;
            record.audits_failed = failed;
            kept.push_back(record);
        }
        if (!removed)
        {
            return true;
        }
        std::string const temporary = path + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            for (TrustRecord const &record : kept)
            {
                output << record.device << ' '
                       << tournament_bytes::encode_hex(record.public_key) << ' '
                       << record.audits_passed << ' ' << record.audits_failed << '\n';
            }
        }
        std::error_code rename_error;
        std::filesystem::rename(temporary, path, rename_error);
        if (rename_error)
        {
            error = "cannot replace " + path;
            return false;
        }
        return true;
    }
}
