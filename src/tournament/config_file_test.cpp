#include "tournament/config_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>

namespace
{
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

    std::filesystem::path const &temp_dir()
    {
        static std::filesystem::path const path = []()
        {
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto const created = std::filesystem::temp_directory_path()
                / ("tournament_config_file_test_" + std::to_string(stamp));
            std::filesystem::create_directories(created);
            return created;
        }();
        return path;
    }

    std::filesystem::path write_config(std::string const &name, std::string const &text)
    {
        std::filesystem::path const path = temp_dir() / name;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        return path;
    }

    void test_load_valid_object()
    {
        std::filesystem::path const path = write_config("valid.json", "{\"lease_ms\": 30000}");
        std::string error;
        std::optional<tournament_config::ConfigFile> const loaded = tournament_config::load(path.string(), error);
        check(loaded.has_value() && loaded->values.contains("lease_ms")
                  && loaded->values["lease_ms"].get<std::uint64_t>() == 30000,
              "valid json object loads with its values");
    }

    void test_load_missing_file()
    {
        std::string error;
        std::optional<tournament_config::ConfigFile> const loaded
            = tournament_config::load((temp_dir() / "absent.json").string(), error);
        check(!loaded.has_value() && error.find("cannot open") != std::string::npos,
              "missing file fails with cannot open");
    }

    void test_load_invalid_json()
    {
        std::filesystem::path const path = write_config("broken.json", "{\"lease_ms\": ");
        std::string error;
        std::optional<tournament_config::ConfigFile> const loaded = tournament_config::load(path.string(), error);
        check(!loaded.has_value() && error.find("not valid json") != std::string::npos,
              "malformed json fails with a parse error");
    }

    void test_load_non_object()
    {
        std::filesystem::path const path = write_config("array.json", "[1, 2, 3]");
        std::string error;
        std::optional<tournament_config::ConfigFile> const loaded = tournament_config::load(path.string(), error);
        check(!loaded.has_value() && error.find("json object") != std::string::npos,
              "a json array is rejected");
    }

    void test_reject_unknown_keys()
    {
        nlohmann::json values = nlohmann::json::parse("{\"a\": 1, \"b\": 2}");
        std::string error;
        check(!tournament_config::reject_unknown_keys(values, {"a"}, error)
                  && error.find("b") != std::string::npos,
              "unknown keys are named in the error");
        error.clear();
        check(tournament_config::reject_unknown_keys(values, {"a", "b"}, error),
              "all known keys pass");
    }

    void test_get_int()
    {
        nlohmann::json values = nlohmann::json::parse(
            "{\"present\": 7, \"text\": \"x\", \"fractional\": 1.5, \"huge\": 99999999999999}");
        std::string error;
        int out = 3;
        check(tournament_config::get_int(values, "absent", out, error) && out == 3,
              "absent key leaves the value untouched");
        check(tournament_config::get_int(values, "present", out, error) && out == 7,
              "integer key reads through");
        out = 3;
        check(!tournament_config::get_int(values, "text", out, error) && error.find("integer") != std::string::npos,
              "string value fails the integer getter");
        error.clear();
        check(!tournament_config::get_int(values, "fractional", out, error)
                  && error.find("integer") != std::string::npos,
              "fractional value fails the integer getter");
        error.clear();
        check(!tournament_config::get_int(values, "huge", out, error)
                  && error.find("range") != std::string::npos,
              "value beyond int range fails with a range error");
    }

    void test_get_u64_and_i64()
    {
        nlohmann::json values = nlohmann::json::parse(
            "{\"positive\": 42, \"negative\": -4, \"big\": 5000000000}");
        std::string error;
        std::uint64_t u64 = 0;
        check(tournament_config::get_u64(values, "positive", u64, error) && u64 == 42,
              "unsigned getter reads positive values");
        check(tournament_config::get_u64(values, "big", u64, error) && u64 == 5000000000ULL,
              "unsigned getter reads beyond 32 bits");
        error.clear();
        check(!tournament_config::get_u64(values, "negative", u64, error)
                  && error.find("negative") != std::string::npos,
              "unsigned getter rejects negative values");
        std::int64_t i64 = 0;
        check(tournament_config::get_i64(values, "negative", i64, error) && i64 == -4,
              "signed getter reads negative values");
        check(tournament_config::get_i64(values, "positive", i64, error) && i64 == 42,
              "signed getter reads positive values");
    }

    void test_get_double_string_bool()
    {
        nlohmann::json values = nlohmann::json::parse(
            "{\"ratio\": 0.25, \"count\": 3, \"name\": \"device\", \"quiet\": true}");
        std::string error;
        double ratio = 0.0;
        check(tournament_config::get_double(values, "ratio", ratio, error) && ratio == 0.25,
              "double getter reads fractions");
        check(tournament_config::get_double(values, "count", ratio, error) && ratio == 3.0,
              "double getter accepts whole numbers");
        std::string name;
        check(tournament_config::get_string(values, "name", name, error) && name == "device",
              "string getter reads strings");
        bool quiet = false;
        check(tournament_config::get_bool(values, "quiet", quiet, error) && quiet,
              "bool getter reads booleans");
        error.clear();
        check(!tournament_config::get_bool(values, "name", quiet, error)
                  && !error.empty(),
              "bool getter rejects non booleans");
    }
}

int main()
{
    test_load_valid_object();
    test_load_missing_file();
    test_load_invalid_json();
    test_load_non_object();
    test_reject_unknown_keys();
    test_get_int();
    test_get_u64_and_i64();
    test_get_double_string_bool();
    std::error_code cleanup;
    std::filesystem::remove_all(temp_dir(), cleanup);
    std::println("config file: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
