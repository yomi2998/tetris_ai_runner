#pragma once

#include <cstdint>
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
}
