#include "tournament/wire.h"

#include <bit>
#include <limits>

namespace tournament_wire
{
    namespace
    {
        void append_u8(std::string &out, std::uint8_t value)
        {
            out.push_back(static_cast<char>(value));
        }

        void append_u32(std::string &out, std::uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                append_u8(out, static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
            }
        }

        void append_u64(std::string &out, std::uint64_t value)
        {
            for (int i = 0; i < 8; ++i)
            {
                append_u8(out, static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
            }
        }

        void append_i32(std::string &out, std::int32_t value)
        {
            append_u32(out, static_cast<std::uint32_t>(value));
        }

        void append_double(std::string &out, double value)
        {
            append_u64(out, std::bit_cast<std::uint64_t>(value));
        }

        void append_bool(std::string &out, bool value)
        {
            append_u8(out, value ? 1u : 0u);
        }

        void append_count(std::string &out, std::size_t count)
        {
            append_u32(out, static_cast<std::uint32_t>(count));
        }

        constexpr std::uint32_t kMaxThetaCount = 1u << 16;
        constexpr std::uint32_t kMaxBatchGames = 1u << 12;
        constexpr std::uint32_t kMaxOutcomes = 1u << 12;
        constexpr std::uint32_t kMaxBlobBytes = 1024;
        constexpr std::uint32_t kEd25519KeySize = 32;

        struct Reader
        {
            std::span<std::uint8_t const> bytes;
            std::size_t offset = 0;
            bool ok = true;

            std::size_t remaining() const
            {
                return ok ? bytes.size() - offset : 0;
            }

            std::uint8_t u8()
            {
                if (!ok || remaining() < 1)
                {
                    ok = false;
                    return 0;
                }
                return bytes[offset++];
            }

            std::uint32_t u32()
            {
                if (!ok || remaining() < 4)
                {
                    ok = false;
                    return 0;
                }
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    value |= static_cast<std::uint32_t>(bytes[offset + static_cast<std::size_t>(i)])
                        << (8 * i);
                }
                offset += 4;
                return value;
            }

            std::uint64_t u64()
            {
                std::uint64_t const low = u32();
                std::uint64_t const high = u32();
                return ok ? (low | (high << 32)) : 0;
            }

            std::int32_t i32()
            {
                return static_cast<std::int32_t>(u32());
            }

            double f64()
            {
                return std::bit_cast<double>(u64());
            }

            bool boolean()
            {
                std::uint8_t const value = u8();
                if (!ok || value > 1)
                {
                    ok = false;
                    return false;
                }
                return value != 0;
            }

            std::vector<std::uint8_t> blob(std::uint32_t count)
            {
                std::vector<std::uint8_t> out;
                if (!ok || count > remaining())
                {
                    ok = false;
                    return out;
                }
                out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
                offset += count;
                return out;
            }

            std::string text(std::uint32_t count)
            {
                std::vector<std::uint8_t> const raw = blob(count);
                return std::string(raw.begin(), raw.end());
            }

            void expect_end()
            {
                if (ok && offset != bytes.size())
                {
                    ok = false;
                }
            }
        };

        bool parse_game(Reader &reader, WireGame &game)
        {
            game.id = reader.u64();
            game.seed_a = reader.u64();
            game.seed_b = reader.u64();
            std::uint32_t const count_a = reader.u32();
            if (!reader.ok || count_a > kMaxThetaCount)
            {
                return false;
            }
            game.theta_a.resize(count_a);
            for (double &value : game.theta_a)
            {
                value = reader.f64();
            }
            std::uint32_t const count_b = reader.u32();
            if (!reader.ok || count_b > kMaxThetaCount)
            {
                return false;
            }
            game.theta_b.resize(count_b);
            for (double &value : game.theta_b)
            {
                value = reader.f64();
            }
            return reader.ok;
        }

        bool parse_outcome(Reader &reader, WireOutcome &outcome)
        {
            outcome.id = reader.u64();
            outcome.winner = reader.i32();
            outcome.dead_a = reader.boolean();
            outcome.dead_b = reader.boolean();
            outcome.capped = reader.boolean();
            outcome.rounds = reader.i32();
            outcome.app_a = reader.f64();
            outcome.app_b = reader.f64();
            outcome.apl_a = reader.f64();
            outcome.apl_b = reader.f64();
            std::uint8_t const reason = reader.u8();
            if (!reader.ok || outcome.winner < -1 || outcome.winner > 1 || outcome.rounds < 0
                || reason > static_cast<std::uint8_t>(tuning::WinReason::Unknown))
            {
                return false;
            }
            outcome.reason = static_cast<tuning::WinReason>(reason);
            return true;
        }
    }

    std::string encode_game(WireGame const &game)
    {
        std::string out;
        append_u64(out, game.id);
        append_u64(out, game.seed_a);
        append_u64(out, game.seed_b);
        append_count(out, game.theta_a.size());
        for (double value : game.theta_a)
        {
            append_double(out, value);
        }
        append_count(out, game.theta_b.size());
        for (double value : game.theta_b)
        {
            append_double(out, value);
        }
        return out;
    }

    std::string encode_outcome(WireOutcome const &outcome)
    {
        std::string out;
        append_u64(out, outcome.id);
        append_i32(out, outcome.winner);
        append_bool(out, outcome.dead_a);
        append_bool(out, outcome.dead_b);
        append_bool(out, outcome.capped);
        append_i32(out, outcome.rounds);
        append_double(out, outcome.app_a);
        append_double(out, outcome.app_b);
        append_double(out, outcome.apl_a);
        append_double(out, outcome.apl_b);
        append_u8(out, static_cast<std::uint8_t>(outcome.reason));
        return out;
    }

    std::string encode_result_payload(ResultBatch const &batch)
    {
        std::string out;
        append_u64(out, batch.nonce);
        append_u64(out, batch.device);
        append_count(out, batch.outcomes.size());
        for (WireOutcome const &outcome : batch.outcomes)
        {
            std::string const encoded = encode_outcome(outcome);
            out.append(encoded);
        }
        return out;
    }

    std::uint64_t result_checksum(ResultBatch const &batch)
    {
        std::string const payload = encode_result_payload(batch);
        return tuning::fnv1a_bytes(tuning::kFnvOffsetBasis, payload.data(), payload.size());
    }

    WireGame to_wire(tuning::BatchGame const &game)
    {
        WireGame wire;
        wire.id = game.id;
        wire.theta_a = game.theta_a;
        wire.theta_b = game.theta_b;
        wire.seed_a = game.seed_a;
        wire.seed_b = game.seed_b;
        return wire;
    }

    tuning::BatchGame from_wire(WireGame const &game)
    {
        tuning::BatchGame batch;
        batch.id = game.id;
        batch.theta_a = game.theta_a;
        batch.theta_b = game.theta_b;
        batch.seed_a = game.seed_a;
        batch.seed_b = game.seed_b;
        return batch;
    }

    WireOutcome to_wire(tuning::GameOutcome const &outcome)
    {
        WireOutcome wire;
        wire.id = outcome.id;
        wire.winner = outcome.winner;
        wire.dead_a = outcome.dead_a;
        wire.dead_b = outcome.dead_b;
        wire.capped = outcome.capped;
        wire.rounds = outcome.rounds;
        wire.app_a = outcome.app_a;
        wire.app_b = outcome.app_b;
        wire.apl_a = outcome.apl_a;
        wire.apl_b = outcome.apl_b;
        wire.reason = outcome.reason;
        return wire;
    }

    tuning::GameOutcome from_wire(WireOutcome const &outcome)
    {
        tuning::GameOutcome result;
        result.id = outcome.id;
        result.winner = outcome.winner;
        result.dead_a = outcome.dead_a;
        result.dead_b = outcome.dead_b;
        result.capped = outcome.capped;
        result.rounds = outcome.rounds;
        result.app_a = outcome.app_a;
        result.app_b = outcome.app_b;
        result.apl_a = outcome.apl_a;
        result.apl_b = outcome.apl_b;
        result.reason = outcome.reason;
        return result;
    }

    std::optional<WireGame> decode_game(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        WireGame game;
        if (!parse_game(reader, game))
        {
            return std::nullopt;
        }
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return game;
    }

    std::optional<WireOutcome> decode_outcome(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        WireOutcome outcome;
        if (!parse_outcome(reader, outcome))
        {
            return std::nullopt;
        }
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return outcome;
    }

    std::string encode_hello(HelloMessage const &hello)
    {
        std::string out;
        append_u64(out, hello.device);
        append_count(out, static_cast<std::uint32_t>(hello.public_key.size()));
        for (std::uint8_t value : hello.public_key)
        {
            append_u8(out, value);
        }
        append_u32(out, hello.protocol);
        append_count(out, static_cast<std::uint32_t>(hello.adapter_id.size()));
        out.append(hello.adapter_id);
        append_u64(out, hello.schema_hash);
        return out;
    }

    std::optional<HelloMessage> decode_hello(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        HelloMessage hello;
        hello.device = reader.u64();
        std::uint32_t const key_size = reader.u32();
        if (!reader.ok || key_size != kEd25519KeySize)
        {
            return std::nullopt;
        }
        hello.public_key = reader.blob(key_size);
        hello.protocol = reader.u32();
        std::uint32_t const adapter_size = reader.u32();
        if (!reader.ok || adapter_size > kMaxBlobBytes)
        {
            return std::nullopt;
        }
        hello.adapter_id = reader.text(adapter_size);
        hello.schema_hash = reader.u64();
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return hello;
    }

    std::string encode_assignment(AssignmentBatch const &batch)
    {
        std::string out;
        append_u64(out, batch.nonce);
        append_u64(out, batch.device);
        append_i32(out, batch.config.threads);
        append_u64(out, static_cast<std::uint64_t>(batch.config.iterations_per_move));
        append_i32(out, batch.config.max_rounds);
        append_count(out, batch.games.size());
        for (WireGame const &game : batch.games)
        {
            std::string const encoded = encode_game(game);
            out.append(encoded);
        }
        return out;
    }

    std::optional<AssignmentBatch> decode_assignment(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        AssignmentBatch batch;
        batch.nonce = reader.u64();
        batch.device = reader.u64();
        std::int32_t const threads = reader.i32();
        std::uint64_t const iterations = reader.u64();
        std::int32_t const max_rounds = reader.i32();
        std::uint32_t const game_count = reader.u32();
        if (!reader.ok || threads <= 0 || max_rounds <= 0 || iterations == 0
            || iterations > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
            || game_count > kMaxBatchGames)
        {
            return std::nullopt;
        }
        batch.config.threads = threads;
        batch.config.iterations_per_move = static_cast<std::size_t>(iterations);
        batch.config.max_rounds = max_rounds;
        batch.games.resize(game_count);
        for (WireGame &game : batch.games)
        {
            if (!parse_game(reader, game))
            {
                return std::nullopt;
            }
        }
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return batch;
    }

    std::string encode_result_message(SignedResult const &result)
    {
        std::string out;
        append_u64(out, result.batch.nonce);
        append_u64(out, result.batch.device);
        append_count(out, result.batch.outcomes.size());
        for (WireOutcome const &outcome : result.batch.outcomes)
        {
            std::string const encoded = encode_outcome(outcome);
            out.append(encoded);
        }
        append_count(out, static_cast<std::uint32_t>(result.signature.size()));
        for (std::uint8_t value : result.signature)
        {
            append_u8(out, value);
        }
        return out;
    }

    std::optional<SignedResult> decode_result_message(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        SignedResult result;
        result.batch.nonce = reader.u64();
        result.batch.device = reader.u64();
        std::uint32_t const outcome_count = reader.u32();
        if (!reader.ok || outcome_count > kMaxOutcomes)
        {
            return std::nullopt;
        }
        result.batch.outcomes.resize(outcome_count);
        for (WireOutcome &outcome : result.batch.outcomes)
        {
            if (!parse_outcome(reader, outcome))
            {
                return std::nullopt;
            }
        }
        std::uint32_t const signature_size = reader.u32();
        if (!reader.ok || signature_size > kMaxBlobBytes)
        {
            return std::nullopt;
        }
        result.signature = reader.blob(signature_size);
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return result;
    }

    std::string encode_reject(std::string const &reason)
    {
        std::string out;
        append_count(out, static_cast<std::uint32_t>(reason.size()));
        out.append(reason);
        return out;
    }

    std::optional<std::string> decode_reject(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        std::uint32_t const reason_size = reader.u32();
        if (!reader.ok || reason_size > kMaxBlobBytes)
        {
            return std::nullopt;
        }
        std::string const reason = reader.text(reason_size);
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        return reason;
    }

    std::string frame_message(MessageKind kind, std::string const &payload)
    {
        std::string out;
        append_u32(out, static_cast<std::uint32_t>(payload.size()));
        append_u8(out, static_cast<std::uint8_t>(kind));
        out.append(payload);
        return out;
    }

    std::optional<FramedMessage> unframe_message(std::span<std::uint8_t const> bytes)
    {
        Reader reader{bytes};
        std::uint32_t const payload_size = reader.u32();
        std::uint8_t const kind_value = reader.u8();
        if (!reader.ok || payload_size > max_frame_payload || kind_value < 1 || kind_value > 5)
        {
            return std::nullopt;
        }
        std::string const payload = reader.text(payload_size);
        reader.expect_end();
        if (!reader.ok)
        {
            return std::nullopt;
        }
        FramedMessage message;
        message.kind = static_cast<MessageKind>(kind_value);
        message.payload = payload;
        return message;
    }
}
