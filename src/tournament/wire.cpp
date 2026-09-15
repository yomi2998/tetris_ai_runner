#include "tournament/wire.h"

#include <bit>

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
}
