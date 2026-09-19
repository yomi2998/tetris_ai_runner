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

    tournament_ban::BanRecord ban_record(tw::DeviceId device, tw::PublicKey const &key,
                                         std::uint64_t marker)
    {
        tournament_ban::BanRecord record;
        record.device = device;
        record.public_key = key;
        record.generation = marker;
        record.failed_verdicts = marker;
        record.caught_at_ms = marker;
        return record;
    }

    void write_ban_file(std::string const &path,
                        std::vector<tournament_ban::BanRecord> const &records)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        for (tournament_ban::BanRecord const &record : records)
        {
            output << record.device << ' ' << tournament_bytes::encode_hex(record.public_key)
                   << ' ' << record.generation << ' ' << record.failed_verdicts << ' '
                   << record.caught_at_ms << '\n';
        }
    }

    bool key_banned_on_a_mutable_registry(tr::DeviceRegistry &registry, tw::PublicKey const &key)
    {
        return registry.key_banned(key);
    }

    tw::PublicKey const *bound_key_on_a_const_registry(tr::DeviceRegistry const &registry,
                                                       tw::DeviceId device)
    {
        return registry.bound_key(device);
    }

    void write_trust_file(std::string const &path, std::vector<tr::TrustRecord> const &records)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        for (tr::TrustRecord const &record : records)
        {
            output << record.device << ' '
                   << tournament_bytes::encode_hex(record.public_key) << ' '
                   << record.audits_passed << ' ' << record.audits_failed << '\n';
        }
    }

    static_assert(requires(tr::DeviceRegistry &registry, tw::PublicKey const &key)
                  {
                      registry.key_banned(key);
                  },
                  "key_banned reloads the ban file and must stay callable on a mutable registry");

    static_assert(requires(tr::DeviceRegistry const &registry, tw::DeviceId device)
                  {
                      registry.bound_key(device);
                  },
                  "bound_key only reads the trust preload and must stay callable on a const registry");

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

    void test_ban_file_reload_follows_the_file()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_reload_file");
        std::string const path = (dir / "bans.txt").string();
        std::vector<std::string> notes;

        tr::DeviceRegistry registry;
        registry.set_log([&notes](std::string const &text)
        {
            notes.push_back(text);
        });
        check(registry.enroll(5, full_key(0x50)),
              "ban reload: the first device enrolls before any file ban");
        check(registry.enroll(6, full_key(0x60)),
              "ban reload: the second device enrolls before any file ban");
        check(registry.enroll(7, full_key(0x70)),
              "ban reload: the third device enrolls before any file ban");
        check(registry.blacklist(7), "ban reload: the third device is blacklisted by hand");

        write_ban_file(path, {ban_record(6, full_key(0x60), 1)});
        registry.set_ban_file(path);
        check(registry.key_banned(full_key(0x60)),
              "ban reload: a key listed in the ban file reads banned");
        check(!registry.key_banned(full_key(0x50)),
              "ban reload: a key absent from the ban file reads unbanned");
        check(registry.blacklisted(6),
              "ban reload: the device holding a listed key is blacklisted");
        check(!registry.blacklisted(5),
              "ban reload: the device holding an unlisted key is not blacklisted");
        check(registry.enrolled(6), "ban reload: a file ban leaves the device enrolled");
        check(registry.active_devices() == std::vector<tw::DeviceId>{5},
              "ban reload: the active list drops the banned device and the hand blacklisted device");
        check(notes.size() == 1, "ban reload: reading a new file logs one note");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 1 banned key(s), was 0, 1 device(s) blacklisted",
              "ban reload: the first note counts the keys, the old count, and the blacklisted device");
        notes.clear();

        write_ban_file(path,
                       {ban_record(5, full_key(0x50), 100), ban_record(6, full_key(0x60), 10000)});
        check(registry.key_banned(full_key(0x50)),
              "ban reload: a key added to the file later flips to banned");
        check(registry.blacklisted(5),
              "ban reload: adding a key to the file blacklists the device holding it");
        check(registry.blacklisted(6),
              "ban reload: a key that stays in the file keeps its device blacklisted");
        check(registry.active_devices().empty(),
              "ban reload: every device holding a listed key leaves the active list");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 2 banned key(s), was 1, 1 device(s) blacklisted",
              "ban reload: the growth note counts the added key and the newly blacklisted device");
        notes.clear();

        write_ban_file(path, {ban_record(6, full_key(0x60), 7)});
        check(!registry.key_banned(full_key(0x50)),
              "ban reload: removing a key from the file flips it back to unbanned");
        check(!registry.blacklisted(5),
              "ban reload: removing a key from the file unblacklists the device holding it");
        check(registry.blacklisted(6),
              "ban reload: the key that stayed in the file keeps its device blacklisted");
        check(registry.active_devices() == std::vector<tw::DeviceId>{5},
              "ban reload: the unblacklisted device returns to the active list");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 1 banned key(s), was 2, 1 device(s) unblacklisted",
              "ban reload: the shrink note counts the removed key and the unblacklisted device");
        notes.clear();

        write_ban_file(path, {ban_record(5, full_key(0x50), 200), ban_record(6, full_key(0x60), 3000)});
        check(registry.key_banned(full_key(0x50)), "ban reload: both listed keys read banned again");
        check(registry.key_banned(full_key(0x60)), "ban reload: the second key still reads banned");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 2 banned key(s), was 1, 1 device(s) blacklisted",
              "ban reload: re adding a key counts only the device the reload blacklisted");
        notes.clear();

        write_ban_file(path, {});
        check(!registry.key_banned(full_key(0x60)), "ban reload: an empty ban file unbans every key");
        check(!registry.blacklisted(6), "ban reload: an empty ban file unblacklists that device");
        check(!registry.blacklisted(5), "ban reload: an empty ban file unblacklists the other device");
        check(registry.blacklisted(7),
              "ban reload: a hand blacklist for a key the file never listed survives every reload");
        check(registry.active_devices() == std::vector<tw::DeviceId>{5, 6},
              "ban reload: an empty ban file restores the devices it blacklisted");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 0 banned key(s), was 2, 2 device(s) unblacklisted",
              "ban reload: the empty file note counts both unblacklisted devices");
        notes.clear();

        check(registry.enroll(8, full_key(0x50)),
              "ban reload: a key dropped from the file can enroll a new device again");
        check(!key_banned_on_a_mutable_registry(registry, full_key(0x50)),
              "ban reload: key_banned reads the fresh file through a mutable registry");
        check(!registry.key_banned(full_key(0x70)),
              "ban reload: a key the file never listed reads unbanned");
        check(registry.blacklisted(7), "ban reload: that lookup leaves the hand blacklist in place");
        check(notes.empty(), "ban reload: an unchanged ban file logs nothing");

        write_ban_file(path, {ban_record(5, full_key(0x50), 5)});
        check(registry.key_banned(full_key(0x50)), "ban reload: a key written back into the file bans again");
        check(registry.blacklisted(5), "ban reload: writing the key back blacklists its device");

        write_ban_file(path, {ban_record(5, full_key(0x60), 5)});
        std::error_code time_error;
        std::filesystem::file_time_type const written
            = std::filesystem::last_write_time(path, time_error);
        std::filesystem::last_write_time(path, written + std::chrono::seconds(1), time_error);
        check(registry.key_banned(full_key(0x60)),
              "ban reload: a same size rewrite is picked up through its mtime");
        check(!registry.key_banned(full_key(0x50)),
              "ban reload: the key a same size rewrite dropped reads unbanned");
        check(registry.blacklisted(6),
              "ban reload: the rewrite blacklists the device holding the new key");
        check(!registry.blacklisted(5),
              "ban reload: the rewrite unblacklists the device holding the old key");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

    void test_ban_file_reload_note_rules()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_reload_notes");
        std::string const path = (dir / "bans.txt").string();
        write_ban_file(path, {ban_record(5, full_key(0x50), 1)});
        std::vector<std::string> notes;

        tr::DeviceRegistry unlogged;
        unlogged.set_ban_file(path);
        check(unlogged.key_banned(full_key(0x50)),
              "ban note: a registry without a log callback still reads the ban file");

        tr::DeviceRegistry registry;
        registry.set_log([&notes](std::string const &text)
        {
            notes.push_back(text);
        });
        check(registry.enroll(5, full_key(0x50)),
              "ban note: the device enrolls before its key is banned in memory");
        check(registry.ban_key(full_key(0x50)), "ban note: the key is banned in memory first");
        registry.set_ban_file(path);
        check(registry.key_banned(full_key(0x50)),
              "ban note: a file that matches the in-memory ban still reads banned");
        check(notes.empty(), "ban note: a reload that changes nothing logs nothing");

        write_ban_file(path, {ban_record(5, full_key(0x50), 200000)});
        check(registry.key_banned(full_key(0x50)),
              "ban note: a rewritten file holding the same key still reads banned");
        check(notes.empty(), "ban note: rewriting the same key set logs nothing");

        check(registry.enroll(6, full_key(0x60)), "ban note: a second device enrolls while unbanned");
        check(!registry.blacklisted(6), "ban note: the new device starts unblacklisted");
        write_ban_file(path, {ban_record(6, full_key(0x60), 300)});
        check(registry.key_banned(full_key(0x60)),
              "ban note: swapping the listed key reads the new key banned");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 1 banned key(s), was 1, 1 device(s) unblacklisted, "
                      "1 device(s) blacklisted",
              "ban note: a swap names both the unblacklisted and the blacklisted device");
        notes.clear();

        write_ban_file(path, {});
        check(notes.empty(), "ban note: a file change stays unlogged until a key is looked up");
        check(!registry.key_banned(full_key(0x60)), "ban note: the pending change unbans on lookup");
        check(notes.size() == 1
                  && notes[0] == "ban file reloaded: 0 banned key(s), was 1, 1 device(s) unblacklisted",
              "ban note: the next lookup logs the pending change once");
        check(!registry.key_banned(full_key(0x60)),
              "ban note: a repeat lookup on the emptied file still reads unbanned");
        check(!registry.key_banned(full_key(0x50)),
              "ban note: the key dropped two reloads ago stays unbanned");
        check(notes.size() == 1, "ban note: repeat lookups on an unchanged file add no notes");

        std::vector<std::string> quiet;
        tr::DeviceRegistry fileless;
        fileless.set_log([&quiet](std::string const &text)
        {
            quiet.push_back(text);
        });
        check(!fileless.key_banned(full_key(0x50)),
              "ban note: a registry with no ban file reads unbanned");
        check(quiet.empty(), "ban note: a registry with no ban file logs no note");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

    void test_ban_file_removal_unbans_every_key()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_removal");
        std::string const path = (dir / "bans.txt").string();
        write_ban_file(path, {ban_record(5, full_key(0x50), 1), ban_record(6, full_key(0x60), 12)});
        std::vector<std::string> notes;

        tr::DeviceRegistry registry;
        registry.set_log([&notes](std::string const &text)
        {
            notes.push_back(text);
        });
        check(registry.enroll(5, full_key(0x50)),
              "ban removal: the first device enrolls before the ban");
        check(registry.enroll(6, full_key(0x60)),
              "ban removal: the second device enrolls before the ban");
        check(registry.enroll(7, full_key(0x70)),
              "ban removal: the clean device enrolls before the ban");
        registry.set_ban_file(path);
        check(registry.key_banned(full_key(0x50)),
              "ban removal: both file bans are read before the file goes away");
        check(registry.blacklisted(5), "ban removal: the first listed device is blacklisted");
        check(registry.blacklisted(6), "ban removal: the second listed device is blacklisted");
        check(registry.active_devices() == std::vector<tw::DeviceId>{7},
              "ban removal: only the unlisted device stays active");
        notes.clear();

        std::filesystem::remove(path);
        check(!registry.key_banned(full_key(0x50)),
              "ban removal: deleting the ban file unbans its keys");
        check(!registry.key_banned(full_key(0x60)),
              "ban removal: every key of the deleted file reads unbanned");
        check(!registry.blacklisted(5),
              "ban removal: deleting the ban file unblacklists the first device");
        check(!registry.blacklisted(6),
              "ban removal: deleting the ban file unblacklists every device");
        check(registry.active_devices() == std::vector<tw::DeviceId>{5, 6, 7},
              "ban removal: the active list is restored when the ban file is deleted");
        check(notes.size() == 1 && notes[0] == "ban file removed: every key unbanned",
              "ban removal: the deletion note replaces the reload count note");
        check(registry.enroll(8, full_key(0x50)),
              "ban removal: a key of the deleted file can enroll again");

        check(!registry.key_banned(full_key(0x70)),
              "ban removal: a missing ban file keeps reading unbanned");
        check(notes.size() == 1, "ban removal: a missing ban file logs the removal only once");

        write_ban_file(path, {ban_record(6, full_key(0x60), 900000)});
        check(registry.key_banned(full_key(0x60)),
              "ban removal: a recreated ban file bans its keys again");
        check(registry.blacklisted(6), "ban removal: a recreated ban file blacklists the device again");
        check(!registry.blacklisted(5),
              "ban removal: a recreated file leaves a device it does not list unblacklisted");
        check(notes.size() == 2
                  && notes[1] == "ban file reloaded: 1 banned key(s), was 0, 1 device(s) blacklisted",
              "ban removal: the recreated file logs a reload note counting the new ban");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

    void test_trust_binding_rejects_two_keys_for_one_device()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_trust_conflict");
        std::string const conflict_path = (dir / "trust_conflict.txt").string();
        write_trust_file(conflict_path,
                         {tr::TrustRecord{7, full_key(0x71), 3, 1},
                          tr::TrustRecord{8, full_key(0x80), 1, 0},
                          tr::TrustRecord{7, full_key(0x72), 0, 4}});
        std::string error;
        tr::DeviceRegistry registry;
        check(!registry.load_trust(conflict_path, error),
              "trust binding: one device id bound to two public keys is rejected");
        check(error.find("line 3") != std::string::npos,
              "trust binding: the rejection names the line of the conflicting record");
        check(error.find("different public key") != std::string::npos,
              "trust binding: the rejection says the key differs from the binding");
        check(error.find("device id 7") != std::string::npos,
              "trust binding: the rejection names the device id that clashed");

        std::string const repeat_path = (dir / "trust_repeat.txt").string();
        write_trust_file(repeat_path,
                         {tr::TrustRecord{7, full_key(0x73), 2, 1},
                          tr::TrustRecord{7, full_key(0x73), 2, 1}});
        tr::DeviceRegistry repeat;
        check(repeat.load_trust(repeat_path, error),
              "trust binding: the same key repeated for one device id loads");
        check(repeat.bound_key(7) != nullptr && *repeat.bound_key(7) == full_key(0x73),
              "trust binding: the repeated record still reports one binding");
        check(repeat.trust_snapshot().size() == 1,
              "trust binding: a duplicated binding holds one preload record");

        std::error_code cleanup;
        std::filesystem::remove_all(dir, cleanup);
    }

    void test_trust_binding_gates_enrollment()
    {
        std::filesystem::path const dir = fresh_dir("tournament_registry_trust_binding");
        std::string const path = (dir / "trust.txt").string();
        write_trust_file(path, {tr::TrustRecord{7, full_key(0x71), 3, 1},
                                tr::TrustRecord{9, full_key(0x90), 0, 4}});
        std::string error;
        tr::DeviceRegistry registry;
        check(registry.load_trust(path, error), "trust binding: a one key per device file loads");
        tw::PublicKey const *bound = registry.bound_key(7);
        check(bound != nullptr && *bound == full_key(0x71),
              "trust binding: bound_key reports the preloaded public key");
        check(bound_key_on_a_const_registry(registry, 9) != nullptr
                  && *bound_key_on_a_const_registry(registry, 9) == full_key(0x90),
              "trust binding: a const registry reports the second binding");
        check(registry.bound_key(8) == nullptr,
              "trust binding: a device id missing from the trust file has no binding");

        check(!registry.enroll(7, full_key(0x72)),
              "trust binding: a bound device id refuses a different public key");
        check(!registry.enrolled(7), "trust binding: a refused enrollment leaves the id unenrolled");
        check(registry.size() == 0, "trust binding: a refused enrollment stores no device");
        check(registry.enroll(7, full_key(0x71)),
              "trust binding: the bound public key enrolls its device id");
        check(registry.enroll(8, full_key(0x72)),
              "trust binding: an unbound device id enrolls a key the file never mentions");
        check(registry.bound_key(8) == nullptr,
              "trust binding: an enrollment outside the trust file creates no binding");
        tr::DeviceStats const *stats = registry.stats(7);
        check(stats != nullptr && stats->audits_passed == 3 && stats->audits_failed == 1,
              "trust binding: the bound record carries its audits to the enrolled id");
        check(registry.trust_snapshot().size() == 3,
              "trust binding: the snapshot keeps both preload keys and the unbound enrollment");

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
    test_ban_file_reload_follows_the_file();
    test_ban_file_reload_note_rules();
    test_ban_file_removal_unbans_every_key();
    test_trust_binding_rejects_two_keys_for_one_device();
    test_trust_binding_gates_enrollment();
    test_remove_trust_record();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
