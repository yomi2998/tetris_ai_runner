#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tournament_config
{
    struct ConfigFile
    {
        nlohmann::json values;
        std::string path;
    };

    std::optional<ConfigFile> load(std::string const &path, std::string &error);
    bool reject_unknown_keys(nlohmann::json const &values, std::vector<std::string> const &allowed,
                             std::string &error);
    bool get_int(nlohmann::json const &values, std::string const &key, int &out, std::string &error);
    bool get_i64(nlohmann::json const &values, std::string const &key, std::int64_t &out,
                 std::string &error);
    bool get_u64(nlohmann::json const &values, std::string const &key, std::uint64_t &out,
                 std::string &error);
    bool get_double(nlohmann::json const &values, std::string const &key, double &out,
                    std::string &error);
    bool get_string(nlohmann::json const &values, std::string const &key, std::string &out,
                    std::string &error);
    bool get_bool(nlohmann::json const &values, std::string const &key, bool &out, std::string &error);
}
