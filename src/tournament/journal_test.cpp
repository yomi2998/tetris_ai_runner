#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "tournament/checkpoint.h"
#include "tournament/journal.h"

namespace
{
    namespace tc = tournament_checkpoint;
    namespace tj = tournament_journal;
    namespace tp = tournament_provenance;
    namespace tw = tournament_wire;
    namespace tu = tuning;

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
                / ("tournament_journal_test_" + std::to_string(stamp));
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

    void flip_byte(std::string &bytes, std::size_t index)
    {
        bytes[index] = static_cast<char>(static_cast<unsigned char>(bytes[index]) ^ 0x5A);
    }

    std::size_t count_entries(std::filesystem::path const &path)
    {
        std::string const bytes = read_file(path);
        std::size_t count = 0;
        std::size_t offset = 0;
        while (offset + 4 <= bytes.size())
        {
            std::uint32_t length = 0;
            for (int i = 0; i < 4; ++i)
            {
                length |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i]))
                    << (8 * i);
            }
            offset += 4 + length;
            ++count;
        }
        return count;
    }

    tp::ProvenanceRecord make_record(std::uint64_t game_id, std::uint64_t device)
    {
        tp::ProvenanceRecord record;
        record.game_id = game_id;
        record.device = device;
        record.nonce = 9000 + game_id;
        record.signature = {0x10, 0x20, 0x30, static_cast<std::uint8_t>(game_id & 0xFFu)};
        record.game.id = game_id;
        record.game.theta_a = {1.5, -2.0, 0.25};
        record.game.theta_b = {0.75};
        record.game.seed_a = game_id * 7 + 1;
        record.game.seed_b = game_id * 11 + 3;
        record.reported.id = game_id;
        record.reported.winner = game_id % 2 == 0 ? 1 : -1;
        record.reported.dead_a = false;
        record.reported.dead_b = false;
        record.reported.capped = false;
        record.reported.rounds = static_cast<int>(game_id) * 3 + 7;
        record.reported.app_a = 0.5 + static_cast<double>(game_id);
        record.reported.app_b = 1.25;
        record.reported.apl_a = 0.125;
        record.reported.apl_b = 2.0;
        record.reported.reason = game_id % 2 == 0 ? tu::WinReason::ASurvivor : tu::WinReason::BSurvivor;
        record.assigned_at_ms = game_id * 100;
        record.accepted_at_ms = game_id * 100 + 9;
        return record;
    }

    void check_all_fields(tp::ProvenanceRecord const &loaded, tp::ProvenanceRecord const &expected,
                          std::string const &tag)
    {
        check(loaded.game_id == expected.game_id, tag + ": game_id matches");
        check(loaded.device == expected.device, tag + ": device matches");
        check(loaded.nonce == expected.nonce, tag + ": nonce matches");
        check(loaded.signature == expected.signature, tag + ": signature matches");
        check(loaded.game == expected.game, tag + ": game matches");
        check(loaded.reported == expected.reported, tag + ": reported outcome matches");
        check(loaded.assigned_at_ms == expected.assigned_at_ms, tag + ": assigned_at_ms matches");
        check(loaded.accepted_at_ms == expected.accepted_at_ms, tag + ": accepted_at_ms matches");
    }

    void test_round_trip(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "journal.bin";
        tj::ProvenanceJournal journal(path.string());
        std::string error;
        tp::ProvenanceRecord const first = make_record(1, 11);
        tp::ProvenanceRecord second = make_record(2, 22);
        second.signature.clear();
        tp::ProvenanceRecord const third = make_record(3, 11);
        check(journal.append(first, error), "round trip: first append succeeds");
        check(journal.append(second, error), "round trip: second append succeeds");
        check(journal.append(third, error), "round trip: third append succeeds");
        check(journal.appended_count() == 3, "round trip: appended count is three");
        check(error.empty(), "round trip: no error after appends");
        check(count_entries(path) == 3, "round trip: journal file holds three entries");

        tp::ProvenanceLedger ledger;
        check(journal.load(ledger, error), "round trip: load succeeds");
        check(error.empty(), "round trip: load reports no error");
        check(ledger.size() == 3, "round trip: ledger holds three records");
        std::vector<tp::ProvenanceRecord const *> const expected = {&first, &second, &third};
        for (tp::ProvenanceRecord const *record : expected)
        {
            std::string const tag = "round trip record " + std::to_string(record->game_id);
            tp::ProvenanceRecord const *loaded = ledger.find(record->game_id);
            check(loaded != nullptr, tag + " is present");
            if (loaded == nullptr)
            {
                continue;
            }
            check_all_fields(*loaded, *record, tag);
        }
    }

    void test_duplicate_first_wins(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "duplicate.bin";
        tj::ProvenanceJournal journal(path.string());
        std::string error;
        check(journal.append(make_record(1, 11), error), "duplicate: first append succeeds");
        check(journal.append(make_record(2, 22), error), "duplicate: second append succeeds");
        check(journal.append(make_record(3, 33), error), "duplicate: third append succeeds");
        tp::ProvenanceRecord const clash = make_record(2, 99);
        check(journal.append(clash, error), "duplicate: fourth append succeeds");
        check(journal.appended_count() == 4, "duplicate: appended count is four");
        check(count_entries(path) == 4, "duplicate: journal file holds four entries");

        tp::ProvenanceLedger ledger;
        check(journal.load(ledger, error), "duplicate: load succeeds");
        check(error.empty(), "duplicate: clean load reports no error");
        check(ledger.size() == 3, "duplicate: ledger holds three records");
        tp::ProvenanceRecord const *kept = ledger.find(2);
        check(kept != nullptr && kept->device == 22 && kept->nonce == 9002,
              "duplicate: first accepted record wins");
    }

    void test_truncated_tail(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "truncated.bin";
        tj::ProvenanceJournal journal(path.string());
        std::string error;
        for (std::uint64_t game_id = 1; game_id <= 3; ++game_id)
        {
            check(journal.append(make_record(game_id, 11), error), "truncated: append succeeds");
        }
        std::uintmax_t const size = std::filesystem::file_size(path);
        check(size > 10, "truncated: journal is larger than the cut");
        std::filesystem::resize_file(path, size - 10);
        tp::ProvenanceLedger ledger;
        check(!journal.load(ledger, error), "truncated: load reports failure");
        check(!error.empty(), "truncated: load reports an error");
        check(error.find("entry 2") != std::string::npos, "truncated: error names the bad entry index");
        check(ledger.size() == 2, "truncated: the valid prefix is loaded");
    }

    void test_missing_file(TempDir const &temp)
    {
        tj::ProvenanceJournal journal((temp.path / "missing.bin").string());
        tp::ProvenanceLedger ledger;
        std::string error;
        check(journal.load(ledger, error), "missing: load succeeds on an absent file");
        check(error.empty(), "missing: load reports no error");
        check(ledger.empty(), "missing: ledger is empty");
    }

    void test_clear(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "cleared.bin";
        tj::ProvenanceJournal journal(path.string());
        std::string error;
        check(journal.append(make_record(1, 11), error), "clear: first append succeeds");
        check(journal.append(make_record(2, 22), error), "clear: second append succeeds");
        check(journal.clear(error), "clear: clear succeeds");
        check(error.empty(), "clear: clear reports no error");
        check(journal.appended_count() == 0, "clear: appended count is reset");
        tp::ProvenanceLedger ledger;
        check(journal.load(ledger, error), "clear: load succeeds on the cleared journal");
        check(error.empty(), "clear: load reports no error");
        check(ledger.empty(), "clear: ledger is empty");
        check(count_entries(path) == 0, "clear: cleared journal holds zero entries");
        check(journal.append(make_record(5, 11), error), "clear: append after clear succeeds");
        tp::ProvenanceLedger reloaded;
        check(journal.load(reloaded, error), "clear: load after re-append succeeds");
        check(reloaded.size() == 1 && reloaded.find(5) != nullptr, "clear: re-appended record loads");
    }

    void test_persistence_across_instances(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "persist.bin";
        std::string error;
        {
            tj::ProvenanceJournal writer(path.string());
            check(writer.append(make_record(1, 11), error), "persist: first append succeeds");
            check(writer.append(make_record(2, 22), error), "persist: second append succeeds");
        }
        tj::ProvenanceJournal reader(path.string());
        tp::ProvenanceLedger ledger;
        check(reader.load(ledger, error), "persist: new instance loads the journal");
        check(error.empty(), "persist: load reports no error");
        check(ledger.size() == 2, "persist: both records are present");
        tp::ProvenanceRecord const *first = ledger.find(1);
        check(first != nullptr && first->device == 11, "persist: first record matches");
        tp::ProvenanceRecord const *second = ledger.find(2);
        check(second != nullptr && second->device == 22, "persist: second record matches");
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

    void test_checkpoint_atomic_save(TempDir const &temp)
    {
        std::filesystem::path const path = temp.path / "atomic.bin";
        std::string const temp_path = path.string() + ".tmp";
        std::string const backup_path = path.string() + ".bak";
        tc::Envelope first = sample_envelope();
        first.generation = 7;
        tc::Envelope second = first;
        second.generation = 8;
        std::string error;
        check(tc::save(path.string(), first, error), "atomic: first save succeeds");
        check(!std::filesystem::exists(temp_path), "atomic: no temporary file after the first save");
        tc::LoadResult const first_load = tc::load(path.string(), first.identity);
        check(first_load.status == tc::LoadStatus::Ok, "atomic: first save loads ok");

        check(tc::save(path.string(), second, error), "atomic: second save succeeds");
        check(!std::filesystem::exists(temp_path), "atomic: no temporary file after the second save");
        check(std::filesystem::exists(backup_path), "atomic: backup exists after the second save");
        tc::LoadResult const second_load = tc::load(path.string(), second.identity);
        check(second_load.status == tc::LoadStatus::Ok && second_load.envelope.generation == 8,
              "atomic: second save loads the new envelope");
        tc::LoadResult const previous = tc::load(backup_path, first.identity);
        check(previous.status == tc::LoadStatus::Ok && previous.envelope.generation == 7,
              "atomic: backup holds the previous envelope");

        std::string corrupted = read_file(path);
        flip_byte(corrupted, corrupted.size() - 2);
        write_file(path, corrupted);
        tc::LoadResult const recovered = tc::load_with_backup(path.string(), second.identity);
        check(recovered.status == tc::LoadStatus::Ok && recovered.backup_used,
              "atomic: corrupt main falls back to the backup through load_with_backup");
        check(recovered.envelope.generation == 7, "atomic: fallback keeps the backup envelope");
    }
}

int main()
{
    TempDir temp;
    test_round_trip(temp);
    test_duplicate_first_wins(temp);
    test_truncated_tail(temp);
    test_missing_file(temp);
    test_clear(temp);
    test_persistence_across_instances(temp);
    test_checkpoint_atomic_save(temp);
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
