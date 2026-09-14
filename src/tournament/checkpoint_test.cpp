#include "tournament/checkpoint.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <print>
#include <string>
#include <utility>

namespace
{
    namespace tc = tournament_checkpoint;

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
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path()
                / ("tournament_checkpoint_test_" + std::to_string(stamp));
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

    std::string read_file(std::filesystem::path const &path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void write_file(std::filesystem::path const &path, std::string const &bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    void flip_byte(std::string &bytes, size_t index)
    {
        bytes[index] = static_cast<char>(static_cast<unsigned char>(bytes[index]) ^ 0x5A);
    }

    tc::Identity sample_identity()
    {
        return tc::Identity{3, "toj_adapter", 0xDEADBEEFCAFEBABEULL};
    }

    tc::Envelope sample_envelope()
    {
        tc::Envelope envelope;
        envelope.identity = sample_identity();
        envelope.generation = 123456789ULL;
        envelope.root_seed = 0xFFFFFFFFFFFFFFFFULL;
        nlohmann::json payload;
        payload["bracket"] = {
            {"rounds", {1, 2, 3}},
            {"winner", "seed_7"},
        };
        payload["cma"] = {
            {"mean", {0.1, -2.5, 0.0}},
            {"sigma", 0.25},
        };
        payload["count"] = 42;
        payload["flag"] = true;
        payload["nothing"] = nullptr;
        payload["note"] = "tournament-\u4e2d\u6587";
        envelope.payload = std::move(payload);
        return envelope;
    }

    void check_round_trip(TempDir const &temp, std::string const &tag, tc::Envelope envelope)
    {
        std::filesystem::path const path = temp.path / (tag + ".bin");
        std::string error;
        check(tc::save(path.string(), envelope, error), "round trip " + tag + ": save succeeds");
        check(error.empty(), "round trip " + tag + ": save reports no error");
        tc::LoadResult const loaded = tc::load(path.string(), envelope.identity);
        check(loaded.status == tc::LoadStatus::Ok, "round trip " + tag + ": load status is ok");
        if (loaded.status != tc::LoadStatus::Ok)
        {
            return;
        }
        check(loaded.envelope.identity == envelope.identity, "round trip " + tag + ": identity matches");
        check(loaded.envelope.generation == envelope.generation, "round trip " + tag + ": generation matches");
        check(loaded.envelope.root_seed == envelope.root_seed, "round trip " + tag + ": root seed matches");
        check(loaded.envelope.payload == envelope.payload, "round trip " + tag + ": payload matches");
    }

    void test_round_trip(TempDir const &temp)
    {
        check_round_trip(temp, "object_payload", sample_envelope());

        tc::Envelope array_envelope = sample_envelope();
        array_envelope.payload = nlohmann::json::array({"alpha", 7, false, 1.5});
        check_round_trip(temp, "array_payload", array_envelope);

        tc::Envelope minimal;
        minimal.identity = tc::Identity{1, "x", 0};
        minimal.payload = nlohmann::json{};
        check_round_trip(temp, "null_payload", minimal);
    }

    void test_determinism(TempDir const &temp)
    {
        tc::Envelope const envelope = sample_envelope();
        std::filesystem::path const first = temp.path / "det_first.bin";
        std::filesystem::path const second = temp.path / "det_second.bin";
        std::filesystem::path const resaved = temp.path / "det_resaved.bin";
        std::string error;
        check(tc::save(first.string(), envelope, error), "determinism: first save succeeds");
        check(tc::save(second.string(), envelope, error), "determinism: second save succeeds");
        std::string const first_bytes = read_file(first);
        std::string const second_bytes = read_file(second);
        check(!first_bytes.empty() && first_bytes == second_bytes,
              "determinism: identical envelopes serialize to identical bytes");

        tc::LoadResult const loaded = tc::load(first.string(), envelope.identity);
        check(loaded.status == tc::LoadStatus::Ok, "determinism: load succeeds");
        check(tc::save(resaved.string(), loaded.envelope, error), "determinism: re-save succeeds");
        check(read_file(resaved) == first_bytes,
              "determinism: re-serializing a loaded envelope is byte-identical");
        check(tc::frame_bytes(envelope) == first_bytes,
              "determinism: frame_bytes matches the written file");
    }

    void test_large_payload_round_trip(TempDir const &temp)
    {
        auto sized_envelope = [](size_t entries)
        {
            tc::Envelope envelope;
            envelope.identity = sample_identity();
            envelope.generation = entries;
            envelope.root_seed = 99ULL;
            nlohmann::json payload = nlohmann::json::array();
            for (size_t i = 0; i < entries; ++i)
            {
                payload.push_back(static_cast<double>(i) * 0.5);
            }
            envelope.payload = std::move(payload);
            return envelope;
        };

        for (size_t entries : {size_t{1200}, size_t{12000}})
        {
            tc::Envelope const envelope = sized_envelope(entries);
            std::string const body = tc::canonical_envelope_json(envelope);
            check(body.size() > entries * 2, "large payload " + std::to_string(entries) + ": body exceeds one length byte");
            check_round_trip(temp, "large_payload_" + std::to_string(entries), envelope);
        }
    }

    void test_missing_and_empty(TempDir const &temp)
    {
        tc::Identity const identity = sample_identity();
        tc::LoadResult const missing = tc::load((temp.path / "missing.bin").string(), identity);
        check(missing.status == tc::LoadStatus::Missing, "missing: absent file reports missing");
        check(!missing.detail.empty(), "missing: absent file reports a detail");

        std::filesystem::path const empty = temp.path / "empty.bin";
        write_file(empty, "");
        check(tc::load(empty.string(), identity).status == tc::LoadStatus::Truncated,
              "missing: empty file reports truncated");
    }

    void test_checksum_corruption(TempDir const &temp)
    {
        tc::Envelope const envelope = sample_envelope();
        std::filesystem::path const path = temp.path / "corrupt.bin";
        std::string error;
        check(tc::save(path.string(), envelope, error), "corruption: save succeeds");
        std::string const bytes = read_file(path);

        std::string body_flip = bytes;
        flip_byte(body_flip, 17);
        check(tc::decode_bytes(body_flip, envelope.identity).status == tc::LoadStatus::ChecksumMismatch,
              "corruption: flipped JSON byte fails the checksum");

        std::string sum_flip = bytes;
        flip_byte(sum_flip, sum_flip.size() - 1);
        check(tc::decode_bytes(sum_flip, envelope.identity).status == tc::LoadStatus::ChecksumMismatch,
              "corruption: flipped checksum byte fails the checksum");
    }

    void test_framing(TempDir const &temp)
    {
        tc::Envelope const envelope = sample_envelope();
        std::filesystem::path const path = temp.path / "framing.bin";
        std::string error;
        check(tc::save(path.string(), envelope, error), "framing: save succeeds");
        std::string const bytes = read_file(path);
        tc::Identity const identity = envelope.identity;

        check(tc::decode_bytes(bytes + "x", identity).status == tc::LoadStatus::TrailingData,
              "framing: one appended byte is rejected as trailing data");
        check(tc::decode_bytes(bytes + "trailing junk", identity).status == tc::LoadStatus::TrailingData,
              "framing: appended junk is rejected as trailing data");
        check(tc::decode_bytes(bytes.substr(0, bytes.size() - 5), identity).status
                  == tc::LoadStatus::Truncated,
              "framing: cutting the tail below the declared frame is truncated");

        check(tc::decode_bytes(bytes.substr(0, 10), identity).status == tc::LoadStatus::Truncated,
              "framing: a file below the minimum size is truncated");
        check(tc::decode_bytes(bytes.substr(0, bytes.size() / 2), identity).status
                  == tc::LoadStatus::Truncated,
              "framing: cutting into the envelope body is truncated");

        std::string const crafted = tc::frame_body("{\"x\":1}");
        std::string longer = crafted;
        longer[8] = static_cast<char>(static_cast<unsigned char>(longer[8]) + 1);
        check(tc::decode_bytes(longer, identity).status == tc::LoadStatus::Truncated,
              "framing: an inflated length field is rejected as truncated");
        std::string shorter = crafted;
        shorter[8] = static_cast<char>(static_cast<unsigned char>(shorter[8]) - 1);
        check(tc::decode_bytes(shorter, identity).status == tc::LoadStatus::TrailingData,
              "framing: a deflated length field is rejected as trailing data");
        check(tc::decode_bytes(crafted, identity).status == tc::LoadStatus::Malformed,
              "framing: a well-formed frame with a foreign body is malformed");
    }

    void test_unsupported()
    {
        tc::Envelope const envelope = sample_envelope();
        tc::Identity const identity = envelope.identity;

        std::string const future = tc::frame_body(tc::canonical_envelope_json(envelope), 2);
        check(tc::decode_bytes(future, identity).status == tc::LoadStatus::Unsupported,
              "unsupported: future format version is rejected");

        std::string const bytes = tc::frame_bytes(envelope);
        std::string magic = bytes;
        flip_byte(magic, 0);
        check(tc::decode_bytes(magic, identity).status == tc::LoadStatus::Unsupported,
              "unsupported: broken magic is rejected");

        std::string version = bytes;
        flip_byte(version, 4);
        check(tc::decode_bytes(version, identity).status == tc::LoadStatus::Unsupported,
              "unsupported: broken format version is rejected");
    }

    void test_invalid_json()
    {
        tc::Identity const identity = sample_identity();
        check(tc::decode_bytes(tc::frame_body("{not json"), identity).status == tc::LoadStatus::InvalidJson,
              "json: invalid syntax is rejected");
        check(tc::decode_bytes(tc::frame_body("{} {}"), identity).status == tc::LoadStatus::InvalidJson,
              "json: trailing content inside the body is rejected");
    }

    void test_malformed()
    {
        tc::Envelope const envelope = sample_envelope();
        tc::Identity const identity = envelope.identity;
        nlohmann::json const base = nlohmann::json::parse(tc::canonical_envelope_json(envelope));

        auto malformed_case = [&](nlohmann::json json_body, std::string const &name)
        {
            check(tc::decode_bytes(tc::frame_body(json_body.dump()), identity).status
                      == tc::LoadStatus::Malformed,
                  name);
        };

        nlohmann::json unknown_key = base;
        unknown_key["surprise"] = 1;
        malformed_case(unknown_key, "malformed: unknown envelope key");

        nlohmann::json string_generation = base;
        string_generation["generation"] = "many";
        malformed_case(string_generation, "malformed: generation is a string");

        nlohmann::json float_version = base;
        float_version["schema_version"] = 1.5;
        malformed_case(float_version, "malformed: schema_version is fractional");

        nlohmann::json zero_version = base;
        zero_version["schema_version"] = 0;
        malformed_case(zero_version, "malformed: schema_version is zero");

        nlohmann::json empty_adapter = base;
        empty_adapter["adapter_id"] = "";
        malformed_case(empty_adapter, "malformed: adapter_id is empty");

        nlohmann::json negative_generation = base;
        negative_generation["generation"] = -1;
        malformed_case(negative_generation, "malformed: generation is negative");

        nlohmann::json missing_payload = base;
        missing_payload.erase("payload");
        malformed_case(missing_payload, "malformed: payload is absent");

        malformed_case(nlohmann::json::array(), "malformed: top level is an array");
        malformed_case(nlohmann::json(42), "malformed: top level is a number");
    }

    void test_schema_mismatch(TempDir const &temp)
    {
        tc::Envelope const envelope = sample_envelope();
        std::filesystem::path const path = temp.path / "schema.bin";
        std::string error;
        check(tc::save(path.string(), envelope, error), "schema: save succeeds");

        tc::Identity wrong_version = envelope.identity;
        wrong_version.schema_version += 1;
        tc::Identity wrong_adapter = envelope.identity;
        wrong_adapter.adapter_id = "other_adapter";
        tc::Identity wrong_hash = envelope.identity;
        wrong_hash.schema_hash ^= 1ULL;

        check(tc::load(path.string(), wrong_version).status == tc::LoadStatus::SchemaMismatch,
              "schema: different schema_version is rejected");
        check(tc::load(path.string(), wrong_adapter).status == tc::LoadStatus::SchemaMismatch,
              "schema: different adapter_id is rejected");
        check(tc::load(path.string(), wrong_hash).status == tc::LoadStatus::SchemaMismatch,
              "schema: different schema_hash is rejected");

        tc::LoadResult const mismatched = tc::load(path.string(), wrong_version);
        check(!mismatched.detail.empty(), "schema: mismatch reports a detail");
        check(tc::load(path.string(), envelope.identity).status == tc::LoadStatus::Ok,
              "schema: matching identity loads");
    }

    void test_backup(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "backup.bin";
        std::string const backup_path = path.string() + ".bak";
        std::string const temp_path = path.string() + ".tmp";

        tc::Envelope first = sample_envelope();
        first.generation = 1;
        tc::Envelope second = first;
        second.generation = 2;

        std::string error;
        check(tc::save(path.string(), first, error), "backup: first save succeeds");
        check(!std::filesystem::exists(backup_path), "backup: no backup after the first save");
        check(!std::filesystem::exists(temp_path), "backup: no temporary file after the first save");

        check(tc::save(path.string(), second, error), "backup: second save succeeds");
        check(std::filesystem::exists(backup_path), "backup: backup exists after the second save");
        check(!std::filesystem::exists(temp_path), "backup: temporary file is gone after rename");
        check(read_file(backup_path) == tc::frame_bytes(first),
              "backup: backup holds the previous envelope bytes");

        tc::LoadResult const current = tc::load(path.string(), first.identity);
        check(current.status == tc::LoadStatus::Ok && current.envelope.generation == 2,
              "backup: main file holds the latest envelope");
        tc::LoadResult const previous = tc::load(backup_path, first.identity);
        check(previous.status == tc::LoadStatus::Ok && previous.envelope.generation == 1,
              "backup: backup file holds the previous envelope");
    }

    void test_save_rejections(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "rejected.bin";
        std::string error;

        tc::Envelope zero_version = sample_envelope();
        zero_version.identity.schema_version = 0;
        check(!tc::save(path.string(), zero_version, error), "reject: zero schema_version is refused");
        check(!error.empty(), "reject: zero schema_version reports an error");

        tc::Envelope empty_adapter = sample_envelope();
        empty_adapter.identity.adapter_id.clear();
        check(!tc::save(path.string(), empty_adapter, error), "reject: empty adapter_id is refused");
        check(!error.empty(), "reject: empty adapter_id reports an error");

        tc::Envelope nan_payload = sample_envelope();
        nan_payload.payload = std::numeric_limits<double>::quiet_NaN();
        check(!tc::save(path.string(), nan_payload, error), "reject: lossy payload is refused");
        check(!error.empty(), "reject: lossy payload reports an error");

        tc::Envelope bad_utf8 = sample_envelope();
        bad_utf8.payload = std::string("bad\xffvalue");
        check(!tc::save(path.string(), bad_utf8, error), "reject: invalid UTF-8 payload is refused");
        check(!error.empty(), "reject: invalid UTF-8 payload reports an error");

        check(!std::filesystem::exists(path), "reject: refused saves create no file");
    }

    void test_missing_versus_io_error(TempDir const &temp)
    {
        tc::Identity const identity = sample_identity();
        tc::LoadResult const absent = tc::load((temp.path / "absent.bin").string(), identity);
        check(absent.status == tc::LoadStatus::Missing, "io: absent path reports missing");
        tc::LoadResult const directory = tc::load(temp.path.string(), identity);
        check(directory.status == tc::LoadStatus::IoError, "io: directory path reports io error");
        check(!directory.detail.empty(), "io: directory path reports a detail");
    }

    void test_load_with_backup(TempDir const &temp)
    {
        tc::Identity const identity = sample_identity();
        std::string error;

        tc::Envelope valid = sample_envelope();
        valid.generation = 31;
        tc::Envelope other = sample_envelope();
        other.generation = 32;

        tc::LoadResult const happy = [&]
        {
            std::filesystem::path const path = temp.path / "happy.bin";
            check(tc::save(path.string(), valid, error), "backup fallback: happy save succeeds");
            return tc::load_with_backup(path.string(), identity);
        }();
        check(happy.status == tc::LoadStatus::Ok && !happy.backup_used && !happy.backup_attempted,
              "backup fallback: valid main file does not use the backup");

        std::filesystem::path const backup_only = temp.path / "backup_only.bin";
        check(tc::save(backup_only.string() + ".bak", valid, error),
              "backup fallback: backup-only save succeeds");
        tc::LoadResult const recovered_missing = tc::load_with_backup(backup_only.string(), identity);
        check(recovered_missing.status == tc::LoadStatus::Ok && recovered_missing.backup_used
                  && recovered_missing.backup_attempted,
              "backup fallback: missing main loads the backup");
        check(recovered_missing.envelope.generation == 31, "backup fallback: missing main keeps backup content");
        check(recovered_missing.detail.find("main checkpoint (missing)") != std::string::npos,
              "backup fallback: missing main detail preserves the main attempt");

        std::filesystem::path const corrupt_main = temp.path / "corrupt_main.bin";
        check(tc::save(corrupt_main.string(), other, error), "backup fallback: corrupt-main save succeeds");
        check(tc::save(corrupt_main.string() + ".bak", valid, error),
              "backup fallback: corrupt-main backup save succeeds");
        std::string corrupted = read_file(corrupt_main);
        flip_byte(corrupted, 20);
        write_file(corrupt_main, corrupted);
        tc::LoadResult const recovered_corrupt = tc::load_with_backup(corrupt_main.string(), identity);
        check(recovered_corrupt.status == tc::LoadStatus::Ok && recovered_corrupt.backup_used
                  && recovered_corrupt.backup_attempted,
              "backup fallback: corrupt main loads the backup");
        check(recovered_corrupt.envelope.generation == 31,
              "backup fallback: corrupt main keeps backup content");
        check(recovered_corrupt.detail.find("checksum_mismatch") != std::string::npos
                  && recovered_corrupt.detail.find("backup checkpoint") != std::string::npos,
              "backup fallback: corrupt main detail preserves both attempts");

        std::filesystem::path const io_main = temp.path / "io_main.bin";
        std::filesystem::create_directories(io_main);
        check(tc::save(io_main.string() + ".bak", valid, error),
              "backup fallback: io-main backup save succeeds");
        tc::LoadResult const recovered_io = tc::load_with_backup(io_main.string(), identity);
        check(recovered_io.status == tc::LoadStatus::Ok && recovered_io.backup_used
                  && recovered_io.backup_attempted,
              "backup fallback: unreadable main loads the backup");

        std::filesystem::path const both_bad = temp.path / "both_bad.bin";
        check(tc::save(both_bad.string(), other, error), "backup fallback: both-bad main save succeeds");
        check(tc::save(both_bad.string() + ".bak", valid, error),
              "backup fallback: both-bad backup save succeeds");
        std::string corrupted_main = read_file(both_bad);
        flip_byte(corrupted_main, 20);
        write_file(both_bad, corrupted_main);
        std::string corrupted_backup = read_file(both_bad.string() + ".bak");
        flip_byte(corrupted_backup, corrupted_backup.size() - 1);
        write_file(both_bad.string() + ".bak", corrupted_backup);
        tc::LoadResult const both_invalid = tc::load_with_backup(both_bad.string(), identity);
        check(both_invalid.status == tc::LoadStatus::ChecksumMismatch
                  && both_invalid.backup_attempted && !both_invalid.backup_used,
              "backup fallback: two corrupt files report the backup failure");
        check(both_invalid.detail.find("main checkpoint (checksum_mismatch)") != std::string::npos
                  && both_invalid.detail.find("backup checkpoint (checksum_mismatch)") != std::string::npos,
              "backup fallback: two corrupt files preserve both details");

        tc::LoadResult const both_missing = tc::load_with_backup((temp.path / "ghost.bin").string(), identity);
        check(both_missing.status == tc::LoadStatus::Missing
                  && both_missing.backup_attempted && !both_missing.backup_used,
              "backup fallback: two absent files report missing");

        std::filesystem::path const schema_path = temp.path / "no_fallback_schema.bin";
        check(tc::save(schema_path.string(), valid, error),
              "backup fallback: schema-mismatch main save succeeds");
        check(tc::save(schema_path.string() + ".bak", other, error),
              "backup fallback: schema-mismatch backup save succeeds");
        tc::Identity wrong_schema = identity;
        wrong_schema.schema_version += 1;
        tc::LoadResult const schema = tc::load_with_backup(schema_path.string(), wrong_schema);
        check(schema.status == tc::LoadStatus::SchemaMismatch && !schema.backup_used
                  && !schema.backup_attempted,
              "backup fallback: schema mismatch never falls back");

        std::filesystem::path const unsupported_path = temp.path / "no_fallback_unsupported.bin";
        check(tc::save(unsupported_path.string(), valid, error),
              "backup fallback: unsupported main save succeeds");
        check(tc::save(unsupported_path.string() + ".bak", other, error),
              "backup fallback: unsupported backup save succeeds");
        write_file(unsupported_path, tc::frame_body(tc::canonical_envelope_json(valid), 2));
        tc::LoadResult const unsupported = tc::load_with_backup(unsupported_path.string(), identity);
        check(unsupported.status == tc::LoadStatus::Unsupported && !unsupported.backup_used
                  && !unsupported.backup_attempted,
              "backup fallback: unsupported format never falls back");
    }

    void test_status_names()
    {
        bool all_named = true;
        for (int value = 0; value <= static_cast<int>(tc::LoadStatus::SchemaMismatch); ++value)
        {
            if (tc::load_status_name(static_cast<tc::LoadStatus>(value)) == nullptr)
            {
                all_named = false;
            }
        }
        check(all_named, "status: every load status has a name");
    }
}

int main()
{
    TempDir temp;
    test_round_trip(temp);
    test_large_payload_round_trip(temp);
    test_determinism(temp);
    test_missing_and_empty(temp);
    test_checksum_corruption(temp);
    test_framing(temp);
    test_unsupported();
    test_invalid_json();
    test_malformed();
    test_schema_mismatch(temp);
    test_backup(temp);
    test_missing_versus_io_error(temp);
    test_load_with_backup(temp);
    test_save_rejections(temp);
    test_status_names();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
