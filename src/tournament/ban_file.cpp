#include "tournament/ban_file.h"

#include <fstream>
#include <utility>

#include "tournament/bytes.h"

namespace tournament_ban
{
    namespace
    {
        std::string encode_hex(PublicKey const &key)
        {
            return tournament_bytes::encode_hex(key);
        }

        bool parse_u64(std::string const &text, std::uint64_t &out)
        {
            if (text.empty())
            {
                return false;
            }
            std::uint64_t value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
                if (value > (UINT64_MAX - static_cast<std::uint64_t>(c - '0')) / 10)
                {
                    return false;
                }
                value = value * 10 + static_cast<std::uint64_t>(c - '0');
            }
            out = value;
            return true;
        }

        bool parse_record(std::string const &line, BanRecord &record, std::string &error)
        {
            std::size_t const first = line.find(' ');
            if (first == std::string::npos)
            {
                error = "missing fields";
                return false;
            }
            std::size_t const second = line.find(' ', first + 1);
            if (second == std::string::npos)
            {
                error = "missing fields";
                return false;
            }
            std::size_t const third = line.find(' ', second + 1);
            if (third == std::string::npos)
            {
                error = "missing fields";
                return false;
            }
            std::size_t const fourth = line.find(' ', third + 1);
            if (fourth == std::string::npos)
            {
                error = "missing fields";
                return false;
            }
            std::string const device_text = line.substr(0, first);
            std::string const key_text = line.substr(first + 1, second - first - 1);
            std::string const generation_text = line.substr(second + 1, third - second - 1);
            std::string const verdicts_text = line.substr(third + 1, fourth - third - 1);
            std::string const caught_text = line.substr(fourth + 1);
            if (!parse_u64(device_text, record.device)
                || !parse_u64(generation_text, record.generation)
                || !parse_u64(verdicts_text, record.failed_verdicts)
                || !parse_u64(caught_text, record.caught_at_ms))
            {
                error = "malformed number";
                return false;
            }
            if (key_text.size() != 64)
            {
                error = "public key must be 64 lowercase hex chars";
                return false;
            }
            std::optional<std::vector<std::uint8_t>> const key
                = tournament_bytes::decode_hex(key_text);
            if (!key.has_value() || key->size() != 32)
            {
                error = "public key must be 64 lowercase hex chars";
                return false;
            }
            record.public_key = *key;
            return true;
        }
    }

    BanFile::BanFile(std::string path)
        : path_(std::move(path))
    {
    }

    bool BanFile::load(std::vector<BanRecord> &records, std::string &error) const
    {
        records.clear();
        std::ifstream input(path_);
        if (!input.good())
        {
            return true;
        }
        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }
            BanRecord record;
            std::string record_error;
            if (!parse_record(line, record, record_error))
            {
                error = "ban file " + path_ + " line " + std::to_string(line_number)
                    + ": " + record_error;
                return false;
            }
            records.push_back(record);
        }
        return true;
    }

    bool BanFile::append(BanRecord const &record, std::string &error)
    {
        if (record.public_key.size() != 32)
        {
            error = "ban record public key must be 32 bytes";
            return false;
        }
        std::ofstream output(path_, std::ios::app);
        if (!output.good())
        {
            error = "cannot open ban file " + path_ + " for appending";
            return false;
        }
        output << record.device << ' ' << encode_hex(record.public_key) << ' ' << record.generation
               << ' ' << record.failed_verdicts << ' ' << record.caught_at_ms << '\n';
        output.flush();
        if (!output.good())
        {
            error = "cannot write ban file " + path_;
            return false;
        }
        ++appended_;
        return true;
    }

    std::size_t BanFile::record_count() const
    {
        return appended_;
    }
}
