#include "tournament/registry.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <system_error>
#include <vector>

#include "tournament/ban_file.h"
#include "tournament/bytes.h"

namespace
{
    namespace tr = tournament_registry;
    namespace tw = tournament_wire;

    int g_checks = 0;
    int g_failures = 0;

    void check(bool condition, std::string const &name)
    {
        ++g_checks;
        if (condition)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            ++g_failures;
            std::println("FAIL: {}", name);
        }
    }

    tw::PublicKey key_for(std::uint8_t tag)
    {
        return {tag, static_cast<std::uint8_t>(tag + 1), static_cast<std::uint8_t>(tag + 2)};
    }

    tw::PublicKey full_key(std::uint8_t tag)
    {
        return std::vector<std::uint8_t>(32, tag);
    }

    std::filesystem::path fresh_dir(char const *label)
    {
        std::filesystem::path const dir = std::filesystem::temp_directory_path()
            / (std::string(label) + "_"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(dir);
        return dir;
    }

    void test_enroll_and_lookups()
    {
        tr::DeviceRegistry registry;
        check(!registry.enrolled(1), "enroll: an unknown id is not enrolled");
        check(registry.enroll(1, key_for(10)), "enroll: first enrollment succeeds");
        check(registry.enrolled(1), "enroll: enrolled id reports enrolled");
        check(!registry.enroll(1, key_for(99)), "enroll: duplicate enrollment is refused");
        check(registry.size() == 1, "enroll: duplicate enrollment does not change the size");
        check(registry.public_key(1) != nullptr, "enroll: enrolled id has a public key");
        check(registry.public_key(1) && *registry.public_key(1) == key_for(10),
              "enroll: stored key matches the enrolled key");
        check(registry.stats(1) != nullptr, "enroll: enrolled id has stats");
        check(registry.stats(1) && *registry.stats(1) == tr::DeviceStats{},
              "enroll: fresh stats are zeroed");
        check(!registry.enrolled(2), "enroll: unknown id is not enrolled");
        check(registry.public_key(2) == nullptr, "enroll: unknown id has no public key");
        check(registry.stats(2) == nullptr, "enroll: unknown id has no stats");
    }

    void test_unknown_id_operations()
    {
        tr::DeviceRegistry registry;
        check(!registry.blacklist(9), "unknown: blacklisting an unknown id fails");
        check(!registry.blacklisted(9), "unknown: an unknown id is not blacklisted");
        check(!registry.record_accepted(9, 1), "unknown: accepted games on an unknown id fail");
        check(!registry.record_dropped(9, 1), "unknown: dropped games on an unknown id fail");
        check(!registry.record_audit(9, true), "unknown: audits on an unknown id fail");
        check(registry.active_devices().empty(), "unknown: the active list is empty");
        check(registry.size() == 0, "unknown: the size is zero");
    }

    void test_counter_accumulation()
    {
        tr::DeviceRegistry registry;
        registry.enroll(4, key_for(40));
        check(registry.record_accepted(4, 3), "counters: accepted games on an enrolled id succeed");
        check(registry.record_accepted(4, 2), "counters: a second accepted call succeeds");
        check(registry.record_dropped(4, 1), "counters: dropped games on an enrolled id succeed");
        check(registry.record_dropped(4, 4), "counters: a second dropped call succeeds");
        check(registry.record_audit(4, true), "counters: a passing audit succeeds");
        check(registry.record_audit(4, true), "counters: a second passing audit succeeds");
        check(registry.record_audit(4, false), "counters: a failing audit succeeds");

        tr::DeviceStats const *stats = registry.stats(4);
        check(stats != nullptr, "counters: stats remain available");
        check(stats && stats->games_accepted == 5, "counters: accepted games accumulate across calls");
        check(stats && stats->games_dropped == 5, "counters: dropped games accumulate across calls");
        check(stats && stats->audits_passed == 2, "counters: passing audits accumulate across calls");
        check(stats && stats->audits_failed == 1, "counters: failing audits accumulate across calls");
    }

    void test_blacklist()
    {
        tr::DeviceRegistry registry;
        registry.enroll(5, key_for(50));
        registry.enroll(6, key_for(60));
        check(registry.blacklist(5), "blacklist: blacklisting an enrolled id succeeds");
        check(registry.blacklist(5), "blacklist: blacklisting again is idempotent");
        check(registry.blacklisted(5), "blacklist: the blacklisted id reports blacklisted");
        check(!registry.blacklisted(6), "blacklist: the other id is untouched");
        check(registry.enrolled(5), "blacklist: the blacklisted id stays enrolled");
        check(registry.stats(5) && registry.stats(5)->blacklisted,
              "blacklist: stats flag the blacklist");
        check(registry.active_devices() == std::vector<tw::DeviceId>{6},
              "blacklist: the active list skips the blacklisted id");
        check(!registry.enroll(5, key_for(51)), "blacklist: a blacklisted id cannot re-enroll");
        check(registry.size() == 2, "blacklist: the size still counts the blacklisted id");
    }

    void test_key_bans()
    {
        tr::DeviceRegistry registry;
        registry.enroll(5, key_for(50));
        registry.enroll(6, key_for(60));
        check(registry.ban_key(key_for(50)), "key ban: banning a device key succeeds");
        check(!registry.ban_key(key_for(50)), "key ban: banning the same key again is idempotent");
        check(registry.key_banned(key_for(50)), "key ban: the banned key reports banned");
        check(!registry.key_banned(key_for(60)), "key ban: other keys are unaffected");
        check(!registry.enroll(9, key_for(50)),
              "key ban: enrollment under a new id with the banned key is refused");
        check(registry.enroll(10, key_for(70)),
              "key ban: enrollment with a clean key still works");
        check(registry.blacklisted(5),
              "key ban: the device holding the banned key is blacklisted");
        check(registry.active_devices() == std::vector<tw::DeviceId>{6, 10},
              "key ban: the active list skips the key-banned device");
    }

    void test_concurrency_tracking()
    {
        tr::DeviceRegistry registry;
        check(registry.concurrency(5) == 0, "concurrency: unknown device reports zero");
        check(!registry.set_concurrency(5, 3), "concurrency: setting on unknown device fails");
        registry.enroll(5, key_for(50));
        check(registry.set_concurrency(5, 3), "concurrency: setting on enrolled device succeeds");
        check(registry.concurrency(5) == 3, "concurrency: the advertised value reads back");
        check(registry.set_concurrency(5, 0), "concurrency: clearing back to zero succeeds");
        check(registry.concurrency(5) == 0, "concurrency: zero means unspecified");
        check(registry.stats(5) && registry.stats(5)->concurrent_assignments == 0,
              "concurrency: the value is visible through stats");
    }

    void test_active_devices_sorted()
    {
        tr::DeviceRegistry registry;
        for (tw::DeviceId device : {7, 3, 9, 1, 5})
        {
            registry.enroll(device, key_for(1));
        }
        registry.blacklist(5);
        registry.blacklist(1);
        check(registry.active_devices() == std::vector<tw::DeviceId>{3, 7, 9},
              "active: the list is sorted ascending and skips blacklisted ids");
        registry.blacklist(9);
        check(registry.active_devices() == std::vector<tw::DeviceId>{3, 7},
              "active: further blacklisting shrinks the list");
    }

    void test_size_counts_enrollments()
    {
        tr::DeviceRegistry registry;
        check(registry.size() == 0, "size: an empty registry is zero");
        registry.enroll(1, key_for(10));
        registry.enroll(2, key_for(20));
        registry.enroll(3, key_for(30));
        check(registry.size() == 3, "size: each enrollment is counted");
        registry.blacklist(2);
        check(registry.size() == 3, "size: blacklisting does not change the count");
        check(!registry.enroll(1, key_for(11)), "size: duplicate enrollment is refused");
        check(registry.size() == 3, "size: duplicates do not change the count");
    }

    void test_stats_independent()
    {
        tr::DeviceRegistry registry;
        registry.enroll(11, key_for(110));
        registry.enroll(22, key_for(220));
        registry.record_accepted(11, 4);
        registry.record_audit(11, true);
        registry.record_dropped(22, 2);
        registry.record_audit(22, false);
        registry.blacklist(22);

        tr::DeviceStats const *first = registry.stats(11);
        tr::DeviceStats const *second = registry.stats(22);
        check(first != nullptr && second != nullptr, "independence: both devices have stats");
        check(first && first->games_accepted == 4 && first->games_dropped == 0
                  && first->audits_passed == 1 && first->audits_failed == 0 && !first->blacklisted,
              "independence: the first device keeps its own counters");
        check(second && second->games_accepted == 0 && second->games_dropped == 2
                  && second->audits_passed == 0 && second->audits_failed == 1 && second->blacklisted,
              "independence: the second device keeps its own counters");
        check(first && second && !(*first == *second), "independence: the two stat rows differ");
    }
    void test_trust_persistence()
    {
        auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path const dir = std::filesystem::temp_directory_path()
            / ("tournament_registry_trust_test_" + std::to_string(stamp));
        std::filesystem::create_directories(dir);
        auto find_record = [](std::vector<tr::TrustRecord> const &records,
                              tw::PublicKey const &key) -> tr::TrustRecord const *
        {
            for (tr::TrustRecord const &record : records)
            {
                if (record.public_key == key)
                {
                    return &record;
                }
            }
            return nullptr;
        };

        std::string error;
        std::string const missing_path = (dir / "trust_missing.txt").string();
        tr::DeviceRegistry missing_registry;
        check(!missing_registry.load_trust(missing_path, error),
              "trust: loading a missing trust file fails");
        check(error.find("cannot open") != std::string::npos,
              "trust: a missing trust file reports cannot open");

        tw::KeyPair const keys = tw::generate_keypair();
        tw::KeyPair const other_keys = tw::generate_keypair();
        std::string const round_trip_path = (dir / "trust_round_trip.txt").string();
        {
            std::ofstream output(round_trip_path, std::ios::binary);
            output << "7 " << tournament_bytes::encode_hex(keys.public_key) << " 3 1\n";
            output << "8 " << tournament_bytes::encode_hex(other_keys.public_key) << " 10 2\n";
        }

        tr::DeviceRegistry registry_a;
        check(registry_a.load_trust(round_trip_path, error), "trust: the round trip file loads");
        std::vector<tr::TrustRecord> const preloaded = registry_a.trust_snapshot();
        check(preloaded.size() == 2, "trust: the preloaded snapshot holds both keys");
        tr::TrustRecord const *preloaded_known = find_record(preloaded, keys.public_key);
        check(preloaded_known && preloaded_known->device == 7,
              "trust: the preload keeps the file device id");
        check(registry_a.enroll(42, keys.public_key), "trust: enrolling with a preloaded key succeeds");
        check(registry_a.enroll(43, other_keys.public_key), "trust: enrolling the second key succeeds");
        tw::KeyPair const fresh_keys = tw::generate_keypair();
        check(registry_a.enroll(44, fresh_keys.public_key), "trust: enrolling an unknown key succeeds");
        tr::DeviceStats const *loaded = registry_a.stats(42);
        check(loaded && loaded->audits_passed == 3,
              "trust: preloaded passing audits apply to device 42");
        check(loaded && loaded->audits_failed == 1,
              "trust: preloaded failing audits apply to device 42");
        tr::DeviceStats const *second = registry_a.stats(43);
        check(second && second->audits_passed == 10 && second->audits_failed == 2,
              "trust: the second preloaded record applies to device 43");
        tr::DeviceStats const *fresh = registry_a.stats(44);
        check(fresh && fresh->audits_passed == 0 && fresh->audits_failed == 0,
              "trust: a device with an unknown key starts at zero audits");

        registry_a.set_trust_file(round_trip_path);
        check(registry_a.record_audit(42, true), "trust: a passing audit on device 42 succeeds");
        check(registry_a.record_audit(42, true), "trust: a second passing audit on device 42 succeeds");
        check(registry_a.record_audit(44, false), "trust: a failing audit on device 44 succeeds");

        tr::DeviceRegistry registry_b;
        check(registry_b.load_trust(round_trip_path, error), "trust: the persisted file reloads");
        check(registry_b.enroll(99, keys.public_key), "trust: enrolling the persisted key succeeds");
        tr::DeviceStats const *carried = registry_b.stats(99);
        check(carried && carried->audits_passed == 5, "trust: persisted passing audits carry over");
        check(carried && carried->audits_failed == 1, "trust: persisted failing audits carry over");

        std::vector<tr::TrustRecord> const snapshot = registry_a.trust_snapshot();
        check(snapshot.size() == 3, "trust: the snapshot merges preload and live devices");
        tr::TrustRecord const *known = find_record(snapshot, keys.public_key);
        check(known && known->audits_passed == 5 && known->audits_failed == 1,
              "trust: the snapshot reflects live counts for the known key");
        tr::TrustRecord const *unknown = find_record(snapshot, fresh_keys.public_key);
        check(unknown && unknown->audits_passed == 0 && unknown->audits_failed == 1,
              "trust: the snapshot contains the unknown key");

        std::string const malformed_path = (dir / "trust_malformed.txt").string();
        {
            std::ofstream output(malformed_path, std::ios::binary);
            output << "not a device line\n";
        }
        tr::DeviceRegistry registry_c;
        check(!registry_c.load_trust(malformed_path, error), "trust: a malformed line fails the load");
        check(error.find("line 1") != std::string::npos,
              "trust: a malformed line reports the line number");

        std::string const bad_hex_path = (dir / "trust_bad_hex.txt").string();
        {
            std::ofstream output(bad_hex_path, std::ios::binary);
            output << "7 " << std::string(64, 'z') << " 3 1\n";
        }
        tr::DeviceRegistry registry_d;
        check(!registry_d.load_trust(bad_hex_path, error), "trust: bad public key hex fails the load");
        check(error.find("line 1") != std::string::npos, "trust: bad hex reports the line number");

        std::error_code cleanup_error;
        std::filesystem::remove_all(dir, cleanup_error);
    }
}

    void test_ban_file_live_reload()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_reload");
        std::string const path = (dir / "bans.txt").string();
        tournament_ban::BanFile store(path);
        tournament_ban::BanRecord record;
        record.device = 5;
        record.public_key = full_key(0x50);
        record.generation = 1;
        record.failed_verdicts = 2;
        record.caught_at_ms = 3;
        std::string error;
        check(store.append(record, error), "live reload: ban appended");

        tr::DeviceRegistry registry;
        check(registry.ban_key(full_key(0x50)), "live reload: initial ban applied in memory");
        registry.set_ban_file(path);
        check(registry.key_banned(full_key(0x50)), "live reload: ban visible after the first check");
        check(!registry.key_banned(full_key(0x60)), "live reload: other keys unaffected");

        bool removed = false;
        check(store.remove(full_key(0x50), removed, error) && removed,
              "live reload: ban removed from the file");
        check(!registry.key_banned(full_key(0x50)),
              "live reload: unban picked up without a restart");

        tournament_ban::BanRecord other;
        other.device = 6;
        other.public_key = full_key(0x60);
        other.generation = 2;
        other.failed_verdicts = 1;
        other.caught_at_ms = 4;
        check(store.append(other, error), "live reload: new ban appended while running");
        check(registry.key_banned(full_key(0x60)),
              "live reload: new ban picked up without a restart");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

    void test_remove_trust_record()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_unban");
        std::string const path = (dir / "trust.txt").string();
        {
            std::ofstream output(path, std::ios::trunc);
            output << "5 " << std::string(64, '5') << " 7 1\n";
            output << "6 " << std::string(64, '6') << " 3 2\n";
        }
        bool removed = false;
        std::string error;
        check(tr::remove_trust_record(path, full_key(0x55), removed, error) && removed,
              "unban: trust record for the key is removed");
        {
            std::ifstream input(path);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(input, line))
            {
                if (!line.empty())
                {
                    lines.push_back(line);
                }
            }
            check(lines.size() == 1 && lines[0].find(std::string(64, '6')) != std::string::npos,
                  "unban: the other trust record is untouched");
        }
        removed = false;
        check(tr::remove_trust_record(path, full_key(0x55), removed, error) && !removed,
              "unban: absent key reports nothing removed");
        removed = false;
        check(tr::remove_trust_record((dir / "missing.txt").string(), full_key(0x55), removed,
                                      error) && !removed,
              "unban: missing trust file is not an error");

        std::string const malformed = (dir / "bad.txt").string();
        {
            std::ofstream output(malformed, std::ios::trunc);
            output << "5 " << std::string(64, '5') << " 7 1\nnot a trust line\n";
        }
        removed = false;
        check(!tr::remove_trust_record(malformed, full_key(0x55), removed, error)
                  && error.find("line 2") != std::string::npos,
              "unban: malformed trust file fails with its line number");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

int main()
{
    test_enroll_and_lookups();
    test_unknown_id_operations();
    test_counter_accumulation();
    test_blacklist();
    test_key_bans();
    test_concurrency_tracking();
    test_active_devices_sorted();
    test_size_counts_enrollments();
    test_stats_independent();
    test_trust_persistence();
    test_ban_file_live_reload();
    test_remove_trust_record();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
