#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "tuning/match.h"

namespace tournament_wire
{
    using DeviceId = std::uint64_t;
    using Nonce = std::uint64_t;
    using GameId = std::uint64_t;
    using Signature = std::vector<std::uint8_t>;
    using PublicKey = std::vector<std::uint8_t>;
    using SecretKey = std::vector<std::uint8_t>;

    struct WireGame
    {
        GameId id = 0;
        std::vector<double> theta_a;
        std::vector<double> theta_b;
        std::uint64_t seed_a = 0;
        std::uint64_t seed_b = 0;

        bool operator==(WireGame const &) const = default;
    };

    struct WireOutcome
    {
        GameId id = 0;
        int winner = 0;
        bool dead_a = false;
        bool dead_b = false;
        bool capped = false;
        int rounds = 0;
        double app_a = 0.0;
        double app_b = 0.0;
        double apl_a = 0.0;
        double apl_b = 0.0;
        tuning::WinReason reason = tuning::WinReason::Unknown;

        bool operator==(WireOutcome const &) const = default;
    };

    struct AssignmentBatch
    {
        Nonce nonce = 0;
        DeviceId device = 0;
        tuning::RunConfig config{};
        std::vector<WireGame> games;
    };

    struct ResultBatch
    {
        Nonce nonce = 0;
        DeviceId device = 0;
        std::vector<WireOutcome> outcomes;

        bool operator==(ResultBatch const &) const = default;
    };

    struct SignedResult
    {
        ResultBatch batch;
        Signature signature;
    };

    struct KeyPair
    {
        PublicKey public_key;
        SecretKey secret_key;
    };

    std::string encode_game(WireGame const &game);
    std::string encode_outcome(WireOutcome const &outcome);
    std::string encode_result_payload(ResultBatch const &batch);
    std::uint64_t result_checksum(ResultBatch const &batch);

    KeyPair generate_keypair();
    Signature sign_result(KeyPair const &keys, ResultBatch const &batch);
    bool verify_result(PublicKey const &public_key, ResultBatch const &batch, Signature const &signature);

    WireGame to_wire(tuning::BatchGame const &game);
    tuning::BatchGame from_wire(WireGame const &game);
    WireOutcome to_wire(tuning::GameOutcome const &outcome);
    tuning::GameOutcome from_wire(WireOutcome const &outcome);

    inline constexpr std::uint32_t protocol_version = 1;
    inline constexpr std::uint32_t max_frame_payload = 1u << 20;

    enum class MessageKind : std::uint8_t
    {
        Hello = 1,
        Assignment = 2,
        Result = 3,
        Reject = 4,
        Accept = 5,
    };

    struct HelloMessage
    {
        DeviceId device = 0;
        PublicKey public_key;
        std::uint32_t protocol = protocol_version;
        std::string adapter_id;
        std::uint64_t schema_hash = 0;
        std::uint64_t engine_fingerprint = 0;
    };

    struct FramedMessage
    {
        MessageKind kind = MessageKind::Reject;
        std::string payload;
    };

    std::optional<WireGame> decode_game(std::span<std::uint8_t const> bytes);
    std::optional<WireOutcome> decode_outcome(std::span<std::uint8_t const> bytes);

    std::string encode_hello(HelloMessage const &hello);
    std::optional<HelloMessage> decode_hello(std::span<std::uint8_t const> bytes);

    std::string encode_assignment(AssignmentBatch const &batch);
    std::optional<AssignmentBatch> decode_assignment(std::span<std::uint8_t const> bytes);

    std::string encode_result_message(SignedResult const &result);
    std::optional<SignedResult> decode_result_message(std::span<std::uint8_t const> bytes);

    std::string encode_reject(std::string const &reason);
    std::optional<std::string> decode_reject(std::span<std::uint8_t const> bytes);

    std::string frame_message(MessageKind kind, std::string const &payload);
    std::optional<FramedMessage> unframe_message(std::span<std::uint8_t const> bytes);
}
