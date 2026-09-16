#include "tournament/journal.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace tournament_journal
{
    namespace
    {
        constexpr std::uint32_t kMaxEntryBytes = 1u << 20;
        constexpr std::uint32_t kMaxSignatureBytes = 1024;
        constexpr std::uint32_t kMaxThetaCount = 1u << 16;
        constexpr std::size_t kLengthBytes = 4;
        constexpr std::size_t kTailBytes = 16;

        void append_u32(std::string &out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
            }
        }

        void append_u64(std::string &out, std::uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
            }
        }

        std::uint32_t read_u32(char const *data)
        {
            auto const *raw = reinterpret_cast<unsigned char const *>(data);
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value |= static_cast<std::uint32_t>(raw[i]) << (8 * i);
            }
            return value;
        }

        std::uint64_t read_u64(char const *data)
        {
            std::uint64_t value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i])) << (8 * i);
            }
            return value;
        }

        std::string entry_body(tournament_provenance::ProvenanceRecord const &record)
        {
            std::string body;
            body.reserve(96 + record.signature.size());
            append_u64(body, record.game_id);
            append_u64(body, record.device);
            append_u64(body, record.nonce);
            append_u32(body, static_cast<std::uint32_t>(record.signature.size()));
            if (!record.signature.empty())
            {
                body.append(reinterpret_cast<char const *>(record.signature.data()),
                            record.signature.size());
            }
            body += tournament_wire::encode_game(record.game);
            body += tournament_wire::encode_outcome(record.reported);
            append_u64(body, record.assigned_at_ms);
            append_u64(body, record.accepted_at_ms);
            return body;
        }

        struct Reader
        {
            std::string const &bytes;
            std::size_t offset = 0;
            bool ok = true;

            bool require(std::size_t count)
            {
                if (!ok || remaining() < count)
                {
                    ok = false;
                    return false;
                }
                return true;
            }

            std::size_t remaining() const
            {
                return ok ? bytes.size() - offset : 0;
            }

            std::uint32_t u32()
            {
                if (!require(4))
                {
                    return 0;
                }
                std::uint32_t const value = read_u32(bytes.data() + offset);
                offset += 4;
                return value;
            }

            std::uint64_t u64()
            {
                if (!require(8))
                {
                    return 0;
                }
                std::uint64_t const value = read_u64(bytes.data() + offset);
                offset += 8;
                return value;
            }

            void skip(std::size_t count)
            {
                if (!require(count))
                {
                    return;
                }
                offset += count;
            }

            std::vector<std::uint8_t> blob(std::size_t count)
            {
                std::vector<std::uint8_t> out;
                if (!require(count))
                {
                    return out;
                }
                out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
                offset += count;
                return out;
            }
        };

        bool skip_game(Reader &reader)
        {
            reader.u64();
            reader.u64();
            reader.u64();
            std::uint32_t const count_a = reader.u32();
            if (!reader.ok || count_a > kMaxThetaCount || count_a > reader.remaining() / 8)
            {
                return false;
            }
            reader.skip(static_cast<std::size_t>(count_a) * 8);
            std::uint32_t const count_b = reader.u32();
            if (!reader.ok || count_b > kMaxThetaCount || count_b > reader.remaining() / 8)
            {
                return false;
            }
            reader.skip(static_cast<std::size_t>(count_b) * 8);
            return reader.ok;
        }

        bool parse_entry_body(std::string const &body, tournament_provenance::ProvenanceRecord &record)
        {
            Reader reader{body};
            record.game_id = reader.u64();
            record.device = reader.u64();
            record.nonce = reader.u64();
            std::uint32_t const signature_length = reader.u32();
            if (!reader.ok || signature_length > kMaxSignatureBytes)
            {
                return false;
            }
            record.signature = reader.blob(signature_length);
            std::size_t const game_begin = reader.offset;
            if (!skip_game(reader))
            {
                return false;
            }
            std::size_t const game_end = reader.offset;
            if (game_end + kTailBytes > body.size())
            {
                return false;
            }
            auto const *base = reinterpret_cast<std::uint8_t const *>(body.data());
            std::span<std::uint8_t const> const game_span(base + game_begin, game_end - game_begin);
            std::span<std::uint8_t const> const outcome_span(base + game_end,
                                                             body.size() - kTailBytes - game_end);
            std::optional<tournament_wire::WireGame> const game = tournament_wire::decode_game(game_span);
            std::optional<tournament_wire::WireOutcome> const reported =
                tournament_wire::decode_outcome(outcome_span);
            if (!game.has_value() || !reported.has_value())
            {
                return false;
            }
            record.game = std::move(*game);
            record.reported = std::move(*reported);
            record.assigned_at_ms = read_u64(body.data() + body.size() - kTailBytes);
            record.accepted_at_ms = read_u64(body.data() + body.size() - kTailBytes + 8);
            return true;
        }
    }

    ProvenanceJournal::ProvenanceJournal(std::string path)
        : path_(std::move(path))
    {
    }

    bool ProvenanceJournal::load(tournament_provenance::ProvenanceLedger &ledger, std::string &error)
    {
        error.clear();
        std::error_code probe;
        bool const present = std::filesystem::exists(std::filesystem::path(path_), probe);
        if (probe)
        {
            error = "cannot inspect provenance journal " + path_ + ": " + probe.message();
            return false;
        }
        if (!present)
        {
            return true;
        }
        std::ifstream input(path_, std::ios::binary);
        if (!input.good())
        {
            error = "cannot open provenance journal " + path_;
            return false;
        }
        std::string const bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
        {
            error = "read failed on provenance journal " + path_;
            return false;
        }
        std::size_t offset = 0;
        std::size_t entry_index = 0;
        std::size_t duplicates = 0;
        auto fail = [&](std::string const &reason)
        {
            error = "provenance journal " + path_ + ": entry " + std::to_string(entry_index) + " "
                + reason;
            if (duplicates > 0)
            {
                error += " (" + std::to_string(duplicates) + " duplicate entries were skipped)";
            }
            return false;
        };
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < kLengthBytes)
            {
                return fail("is truncated");
            }
            std::uint32_t const body_length = read_u32(bytes.data() + offset);
            if (body_length > kMaxEntryBytes)
            {
                return fail("declares a body over the entry cap");
            }
            if (bytes.size() - offset - kLengthBytes < body_length)
            {
                return fail("is truncated");
            }
            std::string const body = bytes.substr(offset + kLengthBytes, body_length);
            tournament_provenance::ProvenanceRecord record;
            if (!parse_entry_body(body, record))
            {
                return fail("is malformed");
            }
            if (!ledger.record(std::move(record)))
            {
                ++duplicates;
            }
            offset += kLengthBytes + body_length;
            ++entry_index;
        }
        return true;
    }

    bool ProvenanceJournal::append(tournament_provenance::ProvenanceRecord const &record,
                                   std::string &error)
    {
        error.clear();
        std::string const body = entry_body(record);
        if (body.size() > kMaxEntryBytes)
        {
            error = "provenance journal entry for game " + std::to_string(record.game_id)
                + " exceeds the entry cap";
            return false;
        }
        std::string entry;
        entry.reserve(kLengthBytes + body.size());
        append_u32(entry, static_cast<std::uint32_t>(body.size()));
        entry += body;
        std::ofstream output(path_, std::ios::app | std::ios::binary);
        if (!output.good())
        {
            error = "cannot open provenance journal " + path_;
            return false;
        }
        output.write(entry.data(), static_cast<std::streamsize>(entry.size()));
        output.flush();
        output.close();
        if (output.fail())
        {
            error = "write failed on provenance journal " + path_;
            return false;
        }
        ++appended_count_;
        return true;
    }

    bool ProvenanceJournal::clear(std::string &error)
    {
        error.clear();
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        if (!output.good())
        {
            error = "cannot open provenance journal " + path_;
            return false;
        }
        output.flush();
        output.close();
        if (output.fail())
        {
            error = "write failed on provenance journal " + path_;
            return false;
        }
        appended_count_ = 0;
        return true;
    }

    std::uint64_t ProvenanceJournal::appended_count() const
    {
        return appended_count_;
    }
}
