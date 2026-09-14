#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace tournament_checkpoint
{
    inline constexpr uint32_t envelope_format_version = 1;

    enum class LoadStatus
    {
        Ok,
        Missing,
        IoError,
        Truncated,
        TrailingData,
        ChecksumMismatch,
        InvalidJson,
        Malformed,
        Unsupported,
        SchemaMismatch,
    };

    char const *load_status_name(LoadStatus status);

    struct Identity
    {
        uint32_t schema_version = 0;
        std::string adapter_id;
        uint64_t schema_hash = 0;

        bool operator==(Identity const &) const = default;
    };

    struct Envelope
    {
        Identity identity;
        uint64_t generation = 0;
        uint64_t root_seed = 0;
        nlohmann::json payload;
    };

    struct LoadResult
    {
        LoadStatus status = LoadStatus::Missing;
        std::string detail;
        Envelope envelope;
        bool backup_attempted = false;
        bool backup_used = false;
    };

    std::string canonical_envelope_json(Envelope const &envelope);

    std::string frame_body(std::string const &body_json,
                           uint32_t format_version = envelope_format_version);

    std::string frame_bytes(Envelope const &envelope);

    bool save(std::string const &path, Envelope const &envelope, std::string &error);

    LoadResult decode_bytes(std::string const &bytes, Identity const &expected);

    LoadResult load(std::string const &path, Identity const &expected);

    LoadResult load_with_backup(std::string const &path, Identity const &expected);
}
