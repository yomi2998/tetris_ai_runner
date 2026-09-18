#include "tournament/ban_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

namespace
{
    namespace tb = tournament_ban;
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

    struct TempDir
    {
        std::filesystem::path path;

        TempDir()
        {
            path = std::filesystem::temp_directory_path() / ("tournament_ban_test_"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        TempDir(TempDir const &) = delete;
        TempDir &operator=(TempDir const &) = delete;
    };

    tb::BanRecord sample_record(std::uint64_t device)
    {
        tb::BanRecord record;
        record.device = device;
        record.public_key = std::vector<std::uint8_t>(32, 0xCD);
        record.generation = 3;
        record.failed_verdicts = 128;
        record.caught_at_ms = 987654321;
        return record;
    }

    void write_file(std::filesystem::path const &path, std::string const &text)
    {
        std::ofstream output(path, std::ios::trunc);
        output << text;
    }
}

int main()
{
    TempDir temp;

    {
        tb::BanFile file((temp.path / "bans.txt").string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(file.load(records, error) && records.empty(),
              "missing ban file loads as empty without error");

        tb::BanRecord const first = sample_record(2);
        tb::BanRecord second = sample_record(9);
        second.public_key = std::vector<std::uint8_t>(32, 0xAB);
        second.generation = 7;
        second.failed_verdicts = 45;
        check(file.append(first, error) && file.append(second, error) && file.record_count() == 2,
              "appending two ban records succeeds");

        std::vector<tb::BanRecord> loaded;
        check(file.load(loaded, error) && loaded.size() == 2,
              "two ban records survive a reload");
        check(loaded.size() == 2 && loaded[0].device == first.device
                  && loaded[0].public_key == first.public_key
                  && loaded[0].generation == first.generation
                  && loaded[0].failed_verdicts == first.failed_verdicts
                  && loaded[0].caught_at_ms == first.caught_at_ms
                  && loaded[1].device == second.device
                  && loaded[1].public_key == second.public_key,
              "ban record fields roundtrip exactly");

        tb::BanFile second_instance((temp.path / "bans.txt").string());
        std::vector<tb::BanRecord> reloaded;
        check(second_instance.load(reloaded, error) && reloaded.size() == 2,
              "ban file persists across instances");
    }

    {
        std::filesystem::path const malformed = temp.path / "malformed.txt";
        write_file(malformed, "1 " + std::string(64, 'a') + " 0 0 0\nnot a ban line\n");
        tb::BanFile file(malformed.string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(!file.load(records, error) && error.find("line 2") != std::string::npos
                  && records.size() == 1,
              "malformed line fails with its line number and keeps the valid prefix");
    }

    {
        std::filesystem::path const uppercase = temp.path / "uppercase.txt";
        write_file(uppercase, "1 " + std::string(64, 'A') + " 0 0 0\n");
        tb::BanFile file(uppercase.string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(!file.load(records, error), "uppercase hex is rejected");
    }

    {
        std::filesystem::path const bad_key = temp.path / "badkey.txt";
        write_file(bad_key, "1 " + std::string(32, 'a') + " 0 0 0\n");
        tb::BanFile file(bad_key.string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(!file.load(records, error), "short key field is rejected");
    }

    {
        std::filesystem::path const bad_number = temp.path / "badnumber.txt";
        write_file(bad_number, "1 " + std::string(64, 'a') + " x 0 0\n");
        tb::BanFile file(bad_number.string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(!file.load(records, error), "malformed generation number is rejected");
    }

    {
        std::filesystem::path const empty = temp.path / "empty.txt";
        write_file(empty, "");
        tb::BanFile file(empty.string());
        std::vector<tb::BanRecord> records;
        std::string error;
        check(file.load(records, error) && records.empty(),
              "existing empty ban file loads as empty");
    }

    {
        tb::BanFile file((temp.path / "bad_append.txt").string());
        tb::BanRecord record = sample_record(1);
        record.public_key = std::vector<std::uint8_t>(31, 1);
        std::string error;
        check(!file.append(record, error), "append with wrong key size fails");
    }

    {
        std::filesystem::path const removal = temp.path / "removal.txt";
        tb::BanFile writer(removal.string());
        tb::BanRecord first = sample_record(2);
        tb::BanRecord second = sample_record(9);
        second.public_key = std::vector<std::uint8_t>(32, 0xAB);
        std::string error;
        check(writer.append(first, error) && writer.append(second, error),
              "remove setup: two bans appended");

        tb::BanFile file(removal.string());
        bool removed = false;
        check(file.remove(first.public_key, removed, error) && removed,
              "remove deletes the matching key");
        std::vector<tb::BanRecord> loaded;
        check(file.load(loaded, error) && loaded.size() == 1
                  && loaded[0].public_key == second.public_key,
              "remove keeps the other records intact");

        removed = false;
        check(file.remove(first.public_key, removed, error) && !removed,
              "remove of an absent key reports nothing removed");
        std::filesystem::path const stray = removal.string() + ".tmp";
        check(!std::filesystem::exists(stray), "remove leaves no temporary file behind");

        tb::BanFile missing((temp.path / "never_created.txt").string());
        removed = false;
        check(missing.remove(first.public_key, removed, error) && !removed,
              "remove on a missing file reports nothing removed");
    }

    std::println("ban file: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
