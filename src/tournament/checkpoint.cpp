#include "tournament/checkpoint.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "param.h"

namespace tournament_checkpoint
{
    namespace
    {
        constexpr size_t kMagicBytes = 4;
        constexpr size_t kVersionBytes = 4;
        constexpr size_t kLengthBytes = 8;
        constexpr size_t kHeaderBytes = kMagicBytes + kVersionBytes + kLengthBytes;
        constexpr size_t kChecksumBytes = 8;
        constexpr size_t kMinFileBytes = kHeaderBytes + kChecksumBytes;

        uint64_t fnv1a64(char const *data, size_t bytes)
        {
            uint64_t hash = 0xCBF29CE484222325ULL;
            auto const *raw = reinterpret_cast<unsigned char const *>(data);
            for (size_t i = 0; i < bytes; ++i)
            {
                hash ^= static_cast<uint64_t>(raw[i]);
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }

        void append_u32(std::string &out, uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
            {
                out.push_back(static_cast<char>((value >> shift) & 0xFFu));
            }
        }

        void append_u64(std::string &out, uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                out.push_back(static_cast<char>((value >> shift) & 0xFFu));
            }
        }

        uint32_t read_u32(char const *data)
        {
            auto const *raw = reinterpret_cast<unsigned char const *>(data);
            return static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8)
                | (static_cast<uint32_t>(raw[2]) << 16) | (static_cast<uint32_t>(raw[3]) << 24);
        }

        uint64_t read_u64(char const *data)
        {
            auto const *raw = reinterpret_cast<unsigned char const *>(data);
            uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<uint64_t>(raw[i]) << (8 * i);
            }
            return value;
        }

        nlohmann::json envelope_to_json(Envelope const &envelope)
        {
            nlohmann::json json;
            json["adapter_id"] = envelope.identity.adapter_id;
            json["generation"] = envelope.generation;
            json["payload"] = envelope.payload;
            json["root_seed"] = envelope.root_seed;
            json["schema_hash"] = envelope.identity.schema_hash;
            json["schema_version"] = envelope.identity.schema_version;
            return json;
        }

        bool require_uint_field(nlohmann::json const &json, char const *key, uint64_t &out,
                                std::string &detail)
        {
            if (!json.contains(key))
            {
                detail = std::string("envelope is missing '") + key + "'";
                return false;
            }
            nlohmann::json const &value = json.at(key);
            if (!value.is_number_unsigned())
            {
                detail = std::string("'") + key + "' must be an unsigned integer";
                return false;
            }
            out = value.get<uint64_t>();
            return true;
        }

        bool require_identity(nlohmann::json const &json, Identity &identity, std::string &detail)
        {
            uint64_t raw_version = 0;
            if (!require_uint_field(json, "schema_version", raw_version, detail))
            {
                return false;
            }
            if (raw_version == 0 || raw_version > 0xFFFFFFFFULL)
            {
                detail = "'schema_version' must be between 1 and 4294967295";
                return false;
            }
            identity.schema_version = static_cast<uint32_t>(raw_version);
            if (!json.contains("adapter_id"))
            {
                detail = "envelope is missing 'adapter_id'";
                return false;
            }
            nlohmann::json const &adapter = json.at("adapter_id");
            if (!adapter.is_string())
            {
                detail = "'adapter_id' must be a string";
                return false;
            }
            identity.adapter_id = adapter.get<std::string>();
            if (identity.adapter_id.empty())
            {
                detail = "'adapter_id' must not be empty";
                return false;
            }
            return require_uint_field(json, "schema_hash", identity.schema_hash, detail);
        }

        bool envelope_from_json(nlohmann::json const &json, Envelope &envelope, std::string &detail)
        {
            if (!json.is_object())
            {
                detail = "envelope is not a JSON object";
                return false;
            }
            constexpr char const *kKnownKeys[] = {
                "adapter_id", "generation", "payload", "root_seed", "schema_hash", "schema_version",
            };
            for (auto it = json.begin(); it != json.end(); ++it)
            {
                bool known = false;
                for (char const *key : kKnownKeys)
                {
                    known = known || it.key() == key;
                }
                if (!known)
                {
                    detail = "unknown envelope key '" + it.key() + "'";
                    return false;
                }
            }
            if (!require_identity(json, envelope.identity, detail))
            {
                return false;
            }
            if (!require_uint_field(json, "generation", envelope.generation, detail))
            {
                return false;
            }
            if (!require_uint_field(json, "root_seed", envelope.root_seed, detail))
            {
                return false;
            }
            if (!json.contains("payload"))
            {
                detail = "envelope is missing 'payload'";
                return false;
            }
            envelope.payload = json.at("payload");
            return true;
        }

        bool identity_matches(Identity const &saved, Identity const &expected, std::string &detail)
        {
            std::string problems;
            if (saved.schema_version != expected.schema_version)
            {
                problems += "schema_version saved=" + std::to_string(saved.schema_version)
                    + " expected=" + std::to_string(expected.schema_version);
            }
            if (saved.adapter_id != expected.adapter_id)
            {
                if (!problems.empty())
                {
                    problems += "; ";
                }
                problems += "adapter_id saved='" + saved.adapter_id + "' expected='"
                    + expected.adapter_id + "'";
            }
            if (saved.schema_hash != expected.schema_hash)
            {
                if (!problems.empty())
                {
                    problems += "; ";
                }
                problems += std::format("schema_hash saved={:016x} expected={:016x}",
                                        saved.schema_hash, expected.schema_hash);
            }
            detail = "checkpoint identity mismatch: " + problems;
            return problems.empty();
        }

        LoadResult decode_bytes_impl(std::string const &bytes, Identity const &expected)
        {
            LoadResult result;
            if (bytes.size() < kMinFileBytes)
            {
                result.status = LoadStatus::Truncated;
                result.detail = "file holds " + std::to_string(bytes.size())
                    + " bytes, a checkpoint needs at least " + std::to_string(kMinFileBytes);
                return result;
            }
            if (std::memcmp(bytes.data(), "TCKP", kMagicBytes) != 0)
            {
                result.status = LoadStatus::Unsupported;
                result.detail = "file magic is not a tournament checkpoint";
                return result;
            }
            uint32_t const format_version = read_u32(bytes.data() + kMagicBytes);
            if (format_version != envelope_format_version)
            {
                result.status = LoadStatus::Unsupported;
                result.detail = "checkpoint format version " + std::to_string(format_version)
                    + " is not supported, expected " + std::to_string(envelope_format_version);
                return result;
            }
            uint64_t const body_length = read_u64(bytes.data() + kMagicBytes + kVersionBytes);
            if (body_length > static_cast<uint64_t>(bytes.size() - kMinFileBytes))
            {
                result.status = LoadStatus::Truncated;
                result.detail = "declared envelope body of " + std::to_string(body_length)
                    + " bytes does not fit the " + std::to_string(bytes.size()) + " byte file";
                return result;
            }
            size_t const body_offset = kHeaderBytes;
            size_t const body_end = body_offset + static_cast<size_t>(body_length);
            size_t const expected_size = body_end + kChecksumBytes;
            if (bytes.size() != expected_size)
            {
                result.status = LoadStatus::TrailingData;
                result.detail = "file holds " + std::to_string(bytes.size())
                    + " bytes but the checkpoint ends at " + std::to_string(expected_size);
                return result;
            }
            uint64_t const stored_checksum = read_u64(bytes.data() + body_end);
            uint64_t const computed_checksum = fnv1a64(bytes.data(), body_end);
            if (stored_checksum != computed_checksum)
            {
                result.status = LoadStatus::ChecksumMismatch;
                result.detail = std::format("checksum mismatch, stored {:016x} computed {:016x}",
                                            stored_checksum, computed_checksum);
                return result;
            }
            nlohmann::json parsed;
            try
            {
                parsed = nlohmann::json::parse(bytes.data() + body_offset, bytes.data() + body_end);
            }
            catch (nlohmann::json::exception const &parse_error)
            {
                result.status = LoadStatus::InvalidJson;
                result.detail = parse_error.what();
                return result;
            }
            std::string detail;
            if (!envelope_from_json(parsed, result.envelope, detail))
            {
                result.status = LoadStatus::Malformed;
                result.detail = std::move(detail);
                return result;
            }
            if (!identity_matches(result.envelope.identity, expected, detail))
            {
                result.status = LoadStatus::SchemaMismatch;
                result.detail = std::move(detail);
                return result;
            }
            result.status = LoadStatus::Ok;
            return result;
        }

        bool fallback_eligible(LoadStatus status)
        {
            switch (status)
            {
            case LoadStatus::Missing:
            case LoadStatus::IoError:
            case LoadStatus::Truncated:
            case LoadStatus::TrailingData:
            case LoadStatus::ChecksumMismatch:
            case LoadStatus::InvalidJson:
            case LoadStatus::Malformed:
                return true;
            case LoadStatus::Ok:
            case LoadStatus::Unsupported:
            case LoadStatus::SchemaMismatch:
                return false;
            }
            return false;
        }
    }

    char const *load_status_name(LoadStatus status)
    {
        switch (status)
        {
        case LoadStatus::Ok: return "ok";
        case LoadStatus::Missing: return "missing";
        case LoadStatus::IoError: return "io_error";
        case LoadStatus::Truncated: return "truncated";
        case LoadStatus::TrailingData: return "trailing_data";
        case LoadStatus::ChecksumMismatch: return "checksum_mismatch";
        case LoadStatus::InvalidJson: return "invalid_json";
        case LoadStatus::Malformed: return "malformed";
        case LoadStatus::Unsupported: return "unsupported";
        case LoadStatus::SchemaMismatch: return "schema_mismatch";
        }
        return "unknown";
    }

    std::string canonical_envelope_json(Envelope const &envelope)
    {
        return envelope_to_json(envelope).dump();
    }

    std::string frame_body(std::string const &body_json, uint32_t format_version)
    {
        std::string framed;
        framed.reserve(kHeaderBytes + body_json.size() + kChecksumBytes);
        framed.append("TCKP", kMagicBytes);
        append_u32(framed, format_version);
        append_u64(framed, static_cast<uint64_t>(body_json.size()));
        framed.append(body_json);
        append_u64(framed, fnv1a64(framed.data(), framed.size()));
        return framed;
    }

    std::string frame_bytes(Envelope const &envelope)
    {
        return frame_body(canonical_envelope_json(envelope));
    }

    bool save(std::string const &path, Envelope const &envelope, std::string &error)
    {
        error.clear();
        if (envelope.identity.schema_version == 0)
        {
            error = "schema_version must be at least 1";
            return false;
        }
        if (envelope.identity.adapter_id.empty())
        {
            error = "adapter_id must not be empty";
            return false;
        }
        std::string body;
        try
        {
            nlohmann::json const canonical = envelope_to_json(envelope);
            body = canonical.dump();
            if (nlohmann::json::parse(body) != canonical)
            {
                error = "payload is not representable in JSON without loss";
                return false;
            }
        }
        catch (nlohmann::json::exception const &serialize_error)
        {
            error = std::string("payload serialization failed: ") + serialize_error.what();
            return false;
        }
        std::string const framed = frame_body(body);
        if (!durable::write_bytes(path, framed.data(), framed.size()))
        {
            error = "durable write failed for " + path;
            return false;
        }
        return true;
    }

    LoadResult decode_bytes(std::string const &bytes, Identity const &expected)
    {
        return decode_bytes_impl(bytes, expected);
    }

    LoadResult load(std::string const &path, Identity const &expected)
    {
        std::error_code probe;
        std::filesystem::file_status const file_status = std::filesystem::status(path, probe);
        bool const report_missing = probe
            ? probe == std::errc::no_such_file_or_directory || probe == std::errc::not_a_directory
            : file_status.type() == std::filesystem::file_type::not_found;
        if (report_missing)
        {
            LoadResult result;
            result.status = LoadStatus::Missing;
            result.detail = "no checkpoint file at " + path;
            return result;
        }
        if (probe)
        {
            LoadResult result;
            result.status = LoadStatus::IoError;
            result.detail = "cannot inspect " + path + ": " + probe.message();
            return result;
        }
        if (file_status.type() != std::filesystem::file_type::regular)
        {
            LoadResult result;
            result.status = LoadStatus::IoError;
            result.detail = path + " exists but is not a regular checkpoint file";
            return result;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input.good())
        {
            LoadResult result;
            result.status = LoadStatus::IoError;
            result.detail = "cannot open " + path;
            return result;
        }
        std::string bytes;
        char buffer[65536];
        for (;;)
        {
            input.read(buffer, static_cast<std::streamsize>(sizeof buffer));
            bytes.append(buffer, static_cast<size_t>(input.gcount()));
            if (input.bad())
            {
                LoadResult result;
                result.status = LoadStatus::IoError;
                result.detail = "read failed on " + path;
                return result;
            }
            if (input.eof() || input.fail())
            {
                break;
            }
        }
        return decode_bytes_impl(bytes, expected);
    }

    LoadResult load_with_backup(std::string const &path, Identity const &expected)
    {
        LoadResult const main_result = load(path, expected);
        if (!fallback_eligible(main_result.status))
        {
            return main_result;
        }
        LoadResult backup_result = load(path + ".bak", expected);
        backup_result.backup_attempted = true;
        std::string const main_summary = std::string("main checkpoint (")
            + load_status_name(main_result.status) + "): " + main_result.detail;
        if (backup_result.status == LoadStatus::Ok)
        {
            backup_result.backup_used = true;
            backup_result.detail = main_summary + "; backup checkpoint: loaded";
        }
        else
        {
            backup_result.detail = main_summary + "; backup checkpoint ("
                + load_status_name(backup_result.status) + "): " + backup_result.detail;
        }
        return backup_result;
    }
}
