#include "tournament/wire.h"

#include <cstdint>
#include <print>
#include <string>
#include <vector>

namespace
{
    namespace tw = tournament_wire;

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

    std::vector<std::uint8_t> to_bytes(std::string const &text)
    {
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }

    tw::WireGame sample_game(std::uint64_t id)
    {
        tw::WireGame game;
        game.id = id;
        game.seed_a = id * 3 + 1;
        game.seed_b = id * 5 + 2;
        game.theta_a = {1.5, -2.25, 0.0, 3.75};
        game.theta_b = {-0.5, 2.0, -4.5, 1.25};
        return game;
    }

    tw::WireOutcome sample_outcome(std::uint64_t id)
    {
        tw::WireOutcome outcome;
        outcome.id = id;
        outcome.winner = 1;
        outcome.dead_a = false;
        outcome.dead_b = true;
        outcome.capped = false;
        outcome.rounds = static_cast<int>(id) * 7 + 3;
        outcome.app_a = 1.25;
        outcome.app_b = 0.75;
        outcome.apl_a = 2.5;
        outcome.apl_b = 1.5;
        outcome.reason = tuning::WinReason::ASurvivor;
        return outcome;
    }

    tw::AssignmentBatch sample_assignment(std::uint32_t game_count)
    {
        tw::AssignmentBatch batch;
        batch.nonce = 1234567890123ULL;
        batch.device = 7;
        batch.config.threads = 2;
        batch.config.iterations_per_move = 50;
        batch.config.max_rounds = 600;
        for (std::uint32_t i = 0; i < game_count; ++i)
        {
            batch.games.push_back(sample_game(i + 1));
        }
        return batch;
    }

    void test_game_and_outcome_roundtrips()
    {
        tw::WireGame const game = sample_game(9);
        std::string const encoded = tw::encode_game(game);
        std::optional<tw::WireGame> const decoded = tw::decode_game(to_bytes(encoded));
        check(decoded.has_value() && *decoded == game, "game roundtrip preserves all fields");
        check(tw::encode_game(game) == encoded, "game encoding is deterministic");

        tw::WireOutcome const outcome = sample_outcome(9);
        std::string const outcome_bytes = tw::encode_outcome(outcome);
        std::optional<tw::WireOutcome> const outcome_back = tw::decode_outcome(to_bytes(outcome_bytes));
        check(outcome_back.has_value() && *outcome_back == outcome, "outcome roundtrip preserves all fields");

        check(!tw::decode_game(to_bytes(encoded.substr(0, encoded.size() - 1))).has_value(),
              "truncated game is rejected");
        check(!tw::decode_game(to_bytes(encoded + "x")).has_value(), "trailing bytes on game are rejected");
        check(!tw::decode_outcome(to_bytes(outcome_bytes.substr(0, 4))).has_value(),
              "short outcome is rejected");
    }

    void test_outcome_domain_rejections()
    {
        tw::WireOutcome outcome = sample_outcome(4);
        outcome.winner = 7;
        check(!tw::decode_outcome(to_bytes(tw::encode_outcome(outcome))).has_value(),
              "winner outside domain is rejected");
        outcome = sample_outcome(4);
        outcome.rounds = -1;
        check(!tw::decode_outcome(to_bytes(tw::encode_outcome(outcome))).has_value(),
              "negative rounds is rejected");
        outcome = sample_outcome(4);
        outcome.reason = static_cast<tuning::WinReason>(9);
        check(!tw::decode_outcome(to_bytes(tw::encode_outcome(outcome))).has_value(),
              "unknown reason value is rejected");
    }

    void test_hello_roundtrip_and_rejections()
    {
        tw::HelloMessage hello;
        hello.device = 42;
        hello.public_key = std::vector<std::uint8_t>(32, 0xAB);
        hello.protocol = tw::protocol_version;
        hello.adapter_id = "toj_adapter";
        hello.schema_hash = 0xDEADBEEFCAFEBABEULL;
        hello.engine_fingerprint = 0x1234567890ABCDEFULL;
        hello.max_concurrent_assignments = 4;
        std::string const encoded = tw::encode_hello(hello);
        std::optional<tw::HelloMessage> const decoded = tw::decode_hello(to_bytes(encoded));
        check(decoded.has_value() && decoded->device == hello.device
                  && decoded->public_key == hello.public_key && decoded->protocol == hello.protocol
                  && decoded->adapter_id == hello.adapter_id && decoded->schema_hash == hello.schema_hash
                  && decoded->engine_fingerprint == hello.engine_fingerprint
                  && decoded->max_concurrent_assignments == hello.max_concurrent_assignments,
              "hello roundtrip preserves all fields");

        hello.public_key.resize(31);
        check(!tw::decode_hello(to_bytes(tw::encode_hello(hello))).has_value(),
              "hello with wrong public key size is rejected");
        hello.public_key = std::vector<std::uint8_t>(32, 0xAB);
        std::string const trailing = encoded + "junk";
        check(!tw::decode_hello(to_bytes(trailing)).has_value(), "hello with trailing bytes is rejected");
        hello.adapter_id = std::string(1100, 'x');
        check(!tw::decode_hello(to_bytes(tw::encode_hello(hello))).has_value(),
              "oversized adapter id is rejected");
    }

    void test_assignment_roundtrip_and_rejections()
    {
        tw::AssignmentBatch const batch = sample_assignment(3);
        std::string const encoded = tw::encode_assignment(batch);
        std::optional<tw::AssignmentBatch> const decoded = tw::decode_assignment(to_bytes(encoded));
        check(decoded.has_value() && decoded->nonce == batch.nonce && decoded->device == batch.device
                  && decoded->config.threads == batch.config.threads
                  && decoded->config.iterations_per_move == batch.config.iterations_per_move
                  && decoded->config.max_rounds == batch.config.max_rounds
                  && decoded->games.size() == batch.games.size()
                  && decoded->games[0] == batch.games[0] && decoded->games[2] == batch.games[2],
              "assignment roundtrip preserves config and games");

        tw::AssignmentBatch broken = sample_assignment(1);
        broken.config.threads = 0;
        check(!tw::decode_assignment(to_bytes(tw::encode_assignment(broken))).has_value(),
              "assignment with zero threads is rejected");
        broken = sample_assignment(1);
        broken.config.iterations_per_move = 0;
        check(!tw::decode_assignment(to_bytes(tw::encode_assignment(broken))).has_value(),
              "assignment with zero iterations is rejected");
        broken = sample_assignment(1);
        broken.config.max_rounds = -5;
        check(!tw::decode_assignment(to_bytes(tw::encode_assignment(broken))).has_value(),
              "assignment with negative max rounds is rejected");
        check(!tw::decode_assignment(to_bytes(encoded.substr(0, encoded.size() - 3))).has_value(),
              "truncated assignment is rejected");
    }

    void test_result_message_roundtrip_and_signatures()
    {
        tw::ResultBatch batch;
        batch.nonce = 777;
        batch.device = 5;
        batch.outcomes = {sample_outcome(1), sample_outcome(2)};
        tw::KeyPair const keys = tw::generate_keypair();
        tw::SignedResult signed_result;
        signed_result.batch = batch;
        signed_result.signature = tw::sign_result(keys, batch);
        std::string const encoded = tw::encode_result_message(signed_result);
        std::optional<tw::SignedResult> const decoded = tw::decode_result_message(to_bytes(encoded));
        check(decoded.has_value() && decoded->batch == batch && decoded->signature == signed_result.signature,
              "result message roundtrip preserves batch and signature");
        check(decoded.has_value()
                  && tw::verify_result(keys.public_key, decoded->batch, decoded->signature),
              "decoded result still verifies against the signing key");
        check(!tw::verify_result(tw::generate_keypair().public_key, decoded->batch, decoded->signature),
              "decoded result fails against a different key");

        tw::SignedResult tampered = signed_result;
        tampered.batch.outcomes[0].winner = -1;
        check(!tw::verify_result(keys.public_key, tampered.batch, tampered.signature),
              "payload modification breaks the signature");

        std::string const missing = encoded.substr(0, encoded.size() - 2);
        check(!tw::decode_result_message(to_bytes(missing)).has_value(),
              "truncated result message is rejected");
        tw::SignedResult oversized = signed_result;
        oversized.signature = std::vector<std::uint8_t>(2000, 1);
        check(!tw::decode_result_message(to_bytes(tw::encode_result_message(oversized))).has_value(),
              "oversized signature is rejected");
        tw::SignedResult bad_winner = signed_result;
        bad_winner.batch.outcomes[0].winner = 4;
        check(!tw::decode_result_message(to_bytes(tw::encode_result_message(bad_winner))).has_value(),
              "result message with out of domain winner is rejected");
    }

    void test_reject_and_framing()
    {
        std::string const reason = "device id already enrolled";
        std::string const encoded = tw::encode_reject(reason);
        std::optional<std::string> const decoded = tw::decode_reject(to_bytes(encoded));
        check(decoded.has_value() && *decoded == reason, "reject reason roundtrips");

        for (tw::MessageKind kind : {tw::MessageKind::Hello, tw::MessageKind::Assignment,
                                     tw::MessageKind::Result, tw::MessageKind::Reject,
                                     tw::MessageKind::Accept})
        {
            std::string const payload = kind == tw::MessageKind::Hello ? "hello-body" : "";
            std::string const frame = tw::frame_message(kind, payload);
            std::optional<tw::FramedMessage> const back = tw::unframe_message(to_bytes(frame));
            check(back.has_value() && back->kind == kind && back->payload == payload,
                  "frame roundtrip preserves kind and payload");
        }

        std::string const frame = tw::frame_message(tw::MessageKind::Assignment, "abc");
        check(!tw::unframe_message(to_bytes(frame.substr(0, 3))).has_value(), "short frame header is rejected");
        check(!tw::unframe_message(to_bytes(frame + "z")).has_value(), "frame with trailing bytes is rejected");
        std::string const unknown_kind = tw::frame_message(tw::MessageKind::Assignment, "");
        std::string mutated = unknown_kind;
        mutated[4] = static_cast<char>(9);
        check(!tw::unframe_message(to_bytes(mutated)).has_value(), "unknown message kind is rejected");
        std::string const oversized = tw::frame_message(tw::MessageKind::Result, std::string(64, 'x'));
        std::string bombed = oversized;
        bombed[0] = static_cast<char>(0xFF);
        bombed[1] = static_cast<char>(0xFF);
        bombed[2] = static_cast<char>(0xFF);
        bombed[3] = static_cast<char>(0xFF);
        check(!tw::unframe_message(to_bytes(bombed)).has_value(),
              "frame claiming a huge payload is rejected without allocating");
    }
}

int main()
{
    test_game_and_outcome_roundtrips();
    test_outcome_domain_rejections();
    test_hello_roundtrip_and_rejections();
    test_assignment_roundtrip_and_rejections();
    test_result_message_roundtrip_and_signatures();
    test_reject_and_framing();
    std::println("wire frame: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
