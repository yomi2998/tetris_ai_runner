#include "tournament/registry.h"

#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <vector>

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
}

int main()
{
    test_enroll_and_lookups();
    test_unknown_id_operations();
    test_counter_accumulation();
    test_blacklist();
    test_key_bans();
    test_active_devices_sorted();
    test_size_counts_enrollments();
    test_stats_independent();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
