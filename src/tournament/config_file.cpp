#include "tournament/config_file.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>

namespace tournament_config
{
    std::optional<ConfigFile> load(std::string const &path, std::string &error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "cannot open " + path;
            return std::nullopt;
        }
        std::string const text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        try
        {
            nlohmann::json values = nlohmann::json::parse(text);
            if (!values.is_object())
            {
                error = path + " must hold a json object";
                return std::nullopt;
            }
            return ConfigFile{std::move(values), path};
        }
        catch (std::exception const &parse_error)
        {
            error = path + " is not valid json: " + parse_error.what();
            return std::nullopt;
        }
    }

    bool reject_unknown_keys(nlohmann::json const &values, std::vector<std::string> const &allowed,
                             std::string &error)
    {
        std::vector<std::string> unknown;
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            {
                unknown.push_back(it.key());
            }
        }
        if (!unknown.empty())
        {
            std::ostringstream joined;
            for (std::size_t i = 0; i < unknown.size(); ++i)
            {
                if (i > 0)
                {
                    joined << ", ";
                }
                joined << unknown[i];
            }
            error = "unknown config key(s): " + joined.str();
            return false;
        }
        return true;
    }

    bool get_int(nlohmann::json const &values, std::string const &key, int &out, std::string &error)
    {
        std::int64_t value = 0;
        if (!get_i64(values, key, value, error))
        {
            return false;
        }
        if (values.contains(key)
            && (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()))
        {
            error = key + " is outside the integer range";
            return false;
        }
        if (values.contains(key))
        {
            out = static_cast<int>(value);
        }
        return true;
    }

    bool get_i64(nlohmann::json const &values, std::string const &key, std::int64_t &out,
                 std::string &error)
    {
        auto const it = values.find(key);
        if (it == values.end())
        {
            return true;
        }
        if (!it->is_number_integer())
        {
            error = key + " must be an integer";
            return false;
        }
        if (it->is_number_unsigned())
        {
            std::uint64_t const value = it->get<std::uint64_t>();
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                error = key + " is outside the signed integer range";
                return false;
            }
            out = static_cast<std::int64_t>(value);
            return true;
        }
        out = it->get<std::int64_t>();
        return true;
    }

    bool get_u64(nlohmann::json const &values, std::string const &key, std::uint64_t &out,
                 std::string &error)
    {
        auto const it = values.find(key);
        if (it == values.end())
        {
            return true;
        }
        if (!it->is_number_integer())
        {
            error = key + " must be an unsigned integer";
            return false;
        }
        if (it->is_number_unsigned())
        {
            out = it->get<std::uint64_t>();
            return true;
        }
        std::int64_t const signed_value = it->get<std::int64_t>();
        if (signed_value < 0)
        {
            error = key + " must not be negative";
            return false;
        }
        out = static_cast<std::uint64_t>(signed_value);
        return true;
    }

    bool get_double(nlohmann::json const &values, std::string const &key, double &out, std::string &error)
    {
        auto const it = values.find(key);
        if (it == values.end())
        {
            return true;
        }
        if (!it->is_number())
        {
            error = key + " must be a number";
            return false;
        }
        out = it->get<double>();
        return true;
    }

    bool get_string(nlohmann::json const &values, std::string const &key, std::string &out,
                    std::string &error)
    {
        auto const it = values.find(key);
        if (it == values.end())
        {
            return true;
        }
        if (!it->is_string())
        {
            error = key + " must be a string";
            return false;
        }
        out = it->get<std::string>();
        return true;
    }

    bool get_bool(nlohmann::json const &values, std::string const &key, bool &out, std::string &error)
    {
        auto const it = values.find(key);
        if (it == values.end())
        {
            return true;
        }
        if (!it->is_boolean())
        {
            error = key + " must be true or false";
            return false;
        }
        out = it->get<bool>();
        return true;
    }
}
