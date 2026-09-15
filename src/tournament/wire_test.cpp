#include "tournament/wire.h"

#include <cstddef>
#include <cstdint>
#include <print>
#include <stdexcept>
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

    tw::WireOutcome sample_outcome(tw::GameId id)
    {
        tw::WireOutcome outcome;
        outcome.id = id;
        outcome.winner = 1;
        outcome.dead_a = false;
        outcome.dead_b = true;
        outcome.capped = id % 2 == 0;
        outcome.rounds = 900 + static_cast<int>(id) * 7;
        outcome.app_a = 12.5 + static_cast<double>(id);
        outcome.app_b = 3.25;
        outcome.apl_a = -0.75;
        outcome.apl_b = 9.125;
        outcome.reason = tuning::WinReason::ASurvivor;
        return outcome;
    }

    tw::ResultBatch sample_batch()
    {
        tw::ResultBatch batch;
        batch.nonce = 0xA1B2C3D4E5F60718ULL;
        batch.device = 4242;
        batch.outcomes = {sample_outcome(1), sample_outcome(2), sample_outcome(3)};
        return batch;
    }

    void test_payload_determinism()
    {
        tw::ResultBatch const batch = sample_batch();
        std::string const first = tw::encode_result_payload(batch);
        std::string const second = tw::encode_result_payload(batch);
        check(!first.empty(), "payload: encoding a batch produces bytes");
        check(first == second, "payload: encoding the same batch twice is identical");
    }

    void test_payload_sensitivity()
    {
        tw::ResultBatch const base = sample_batch();
        std::string const base_payload = tw::encode_result_payload(base);

        tw::ResultBatch nonce_changed = base;
        nonce_changed.nonce += 1;
        check(tw::encode_result_payload(nonce_changed) != base_payload,
              "sensitivity: nonce change alters the payload");

        tw::ResultBatch device_changed = base;
        device_changed.device += 1;
        check(tw::encode_result_payload(device_changed) != base_payload,
              "sensitivity: device change alters the payload");

        tw::ResultBatch winner_changed = base;
        winner_changed.outcomes[1].winner = 2;
        check(tw::encode_result_payload(winner_changed) != base_payload,
              "sensitivity: winner change alters the payload");

        tw::ResultBatch rounds_changed = base;
        rounds_changed.outcomes[1].rounds += 1;
        check(tw::encode_result_payload(rounds_changed) != base_payload,
              "sensitivity: rounds change alters the payload");

        tw::ResultBatch app_a_changed = base;
        app_a_changed.outcomes[1].app_a += 1.0;
        check(tw::encode_result_payload(app_a_changed) != base_payload,
              "sensitivity: app_a change alters the payload");

        tw::ResultBatch app_b_changed = base;
        app_b_changed.outcomes[1].app_b += 1.0;
        check(tw::encode_result_payload(app_b_changed) != base_payload,
              "sensitivity: app_b change alters the payload");

        tw::ResultBatch apl_a_changed = base;
        apl_a_changed.outcomes[1].apl_a += 1.0;
        check(tw::encode_result_payload(apl_a_changed) != base_payload,
              "sensitivity: apl_a change alters the payload");

        tw::ResultBatch apl_b_changed = base;
        apl_b_changed.outcomes[1].apl_b += 1.0;
        check(tw::encode_result_payload(apl_b_changed) != base_payload,
              "sensitivity: apl_b change alters the payload");

        tw::ResultBatch dead_a_changed = base;
        dead_a_changed.outcomes[1].dead_a = !dead_a_changed.outcomes[1].dead_a;
        check(tw::encode_result_payload(dead_a_changed) != base_payload,
              "sensitivity: dead_a change alters the payload");

        tw::ResultBatch dead_b_changed = base;
        dead_b_changed.outcomes[1].dead_b = !dead_b_changed.outcomes[1].dead_b;
        check(tw::encode_result_payload(dead_b_changed) != base_payload,
              "sensitivity: dead_b change alters the payload");

        tw::ResultBatch capped_changed = base;
        capped_changed.outcomes[1].capped = !capped_changed.outcomes[1].capped;
        check(tw::encode_result_payload(capped_changed) != base_payload,
              "sensitivity: capped change alters the payload");

        tw::ResultBatch reason_changed = base;
        reason_changed.outcomes[1].reason = tuning::WinReason::BCapApl;
        check(tw::encode_result_payload(reason_changed) != base_payload,
              "sensitivity: reason change alters the payload");
    }

    void test_game_round_trip()
    {
        tuning::BatchGame game;
        game.id = 77;
        game.theta_a = {0.5, -1.25, 3.0};
        game.theta_b = {-0.75, 2.5};
        game.seed_a = 0x1122334455667788ULL;
        game.seed_b = 0x99AABBCCDDEEFF00ULL;

        tw::WireGame const wire = tw::to_wire(game);
        check(wire.id == game.id, "game round trip: id preserved");
        check(wire.seed_a == game.seed_a && wire.seed_b == game.seed_b,
              "game round trip: seeds preserved");
        check(wire.theta_a == game.theta_a && wire.theta_b == game.theta_b,
              "game round trip: thetas preserved");

        tuning::BatchGame const back = tw::from_wire(wire);
        check(back.id == game.id && back.seed_a == game.seed_a && back.seed_b == game.seed_b,
              "game round trip: BatchGame scalars preserved");
        check(back.theta_a == game.theta_a && back.theta_b == game.theta_b,
              "game round trip: BatchGame thetas preserved");
        check(tw::to_wire(back) == wire, "game round trip: re-encoded game matches");
    }

    void test_outcome_round_trip()
    {
        tuning::GameOutcome outcome;
        outcome.id = 9;
        outcome.winner = 2;
        outcome.dead_a = true;
        outcome.dead_b = false;
        outcome.capped = true;
        outcome.rounds = 4800;
        outcome.app_a = 1.5;
        outcome.app_b = -2.25;
        outcome.apl_a = 0.125;
        outcome.apl_b = 7.75;
        outcome.reason = tuning::WinReason::BCapApl;

        tw::WireOutcome const wire = tw::to_wire(outcome);
        check(wire.id == outcome.id && wire.winner == outcome.winner,
              "outcome round trip: id and winner preserved");
        check(wire.dead_a == outcome.dead_a && wire.dead_b == outcome.dead_b
                  && wire.capped == outcome.capped,
              "outcome round trip: flags preserved");
        check(wire.rounds == outcome.rounds, "outcome round trip: rounds preserved");
        check(wire.app_a == outcome.app_a && wire.app_b == outcome.app_b
                  && wire.apl_a == outcome.apl_a && wire.apl_b == outcome.apl_b,
              "outcome round trip: doubles preserved");
        check(wire.reason == outcome.reason, "outcome round trip: reason preserved");

        tuning::GameOutcome const back = tw::from_wire(wire);
        check(back.id == outcome.id && back.winner == outcome.winner
                  && back.dead_a == outcome.dead_a && back.dead_b == outcome.dead_b
                  && back.capped == outcome.capped && back.rounds == outcome.rounds,
              "outcome round trip: scalars preserved");
        check(back.app_a == outcome.app_a && back.app_b == outcome.app_b
                  && back.apl_a == outcome.apl_a && back.apl_b == outcome.apl_b,
              "outcome round trip: doubles preserved");
        check(back.reason == outcome.reason, "outcome round trip: reason preserved");
        check(tw::to_wire(back) == wire, "outcome round trip: re-encoded outcome matches");
    }

    void test_checksum()
    {
        tw::ResultBatch const batch = sample_batch();
        std::uint64_t const first = tw::result_checksum(batch);
        std::uint64_t const second = tw::result_checksum(batch);
        check(first == second, "checksum: repeated calls are stable");

        tw::ResultBatch flipped = batch;
        flipped.outcomes[0].winner = 3;
        check(tw::result_checksum(flipped) != first, "checksum: winner flip changes the value");
    }

    void test_keypair_generation()
    {
        tw::KeyPair const first = tw::generate_keypair();
        tw::KeyPair const second = tw::generate_keypair();
        check(first.public_key.size() == 32, "keypair: public key is 32 bytes");
        check(first.secret_key.size() == 32, "keypair: secret key is 32 bytes");
        check(second.public_key.size() == 32 && second.secret_key.size() == 32,
              "keypair: second keypair has 32 byte keys");
        check(first.secret_key != second.secret_key, "keypair: two keypairs have different secrets");
        check(first.public_key != second.public_key, "keypair: two keypairs have different public keys");
    }

    void test_sign_and_verify()
    {
        tw::KeyPair const keys = tw::generate_keypair();
        tw::ResultBatch const batch = sample_batch();
        tw::Signature const signature = tw::sign_result(keys, batch);
        check(signature.size() == 64, "sign: signature is 64 bytes");
        check(tw::verify_result(keys.public_key, batch, signature),
              "sign: matching public key verifies the signature");
    }

    void test_verify_rejections()
    {
        tw::KeyPair const keys = tw::generate_keypair();
        tw::KeyPair const other = tw::generate_keypair();
        tw::ResultBatch const batch = sample_batch();
        tw::Signature const signature = tw::sign_result(keys, batch);

        tw::ResultBatch mutated = batch;
        mutated.outcomes[1].winner = 2;
        check(!tw::verify_result(keys.public_key, mutated, signature),
              "reject: signature over a different batch is refused");

        check(!tw::verify_result(other.public_key, batch, signature),
              "reject: wrong public key is refused");

        tw::Signature const truncated(signature.begin(), signature.end() - 1);
        check(!tw::verify_result(keys.public_key, batch, truncated),
              "reject: truncated signature is refused");

        tw::Signature const empty;
        check(!tw::verify_result(keys.public_key, batch, empty),
              "reject: empty signature is refused");

        tw::Signature garbage(signature.size(), 0);
        for (std::size_t i = 0; i < garbage.size(); ++i)
        {
            garbage[i] = static_cast<std::uint8_t>(i * 37u + 11u);
        }
        check(!tw::verify_result(keys.public_key, batch, garbage),
              "reject: garbage signature bytes are refused");

        tw::Signature const no_key;
        check(!tw::verify_result(no_key, batch, signature),
              "reject: empty public key is refused");
    }

    void test_sign_rejects_bad_seed_size()
    {
        tw::KeyPair const keys = tw::generate_keypair();

        tw::KeyPair short_keys = keys;
        short_keys.secret_key.resize(31);
        bool threw_invalid = false;
        try
        {
            tw::sign_result(short_keys, sample_batch());
        }
        catch (std::invalid_argument const &)
        {
            threw_invalid = true;
        }
        catch (...)
        {
        }
        check(threw_invalid, "sign: 31 byte secret throws std::invalid_argument");

        tw::KeyPair const empty_keys;
        threw_invalid = false;
        try
        {
            tw::sign_result(empty_keys, sample_batch());
        }
        catch (std::invalid_argument const &)
        {
            threw_invalid = true;
        }
        catch (...)
        {
        }
        check(threw_invalid, "sign: empty secret throws std::invalid_argument");
    }

    void test_empty_batch_signs()
    {
        tw::KeyPair const keys = tw::generate_keypair();
        tw::ResultBatch batch;
        batch.nonce = 7;
        batch.device = 3;
        tw::Signature const signature = tw::sign_result(keys, batch);
        check(signature.size() == 64, "empty batch: signature is 64 bytes");
        check(tw::verify_result(keys.public_key, batch, signature),
              "empty batch: signature verifies");
        tw::ResultBatch changed = batch;
        changed.nonce += 1;
        check(!tw::verify_result(keys.public_key, changed, signature),
              "empty batch: nonce change breaks verification");
    }
}

int main()
{
    test_payload_determinism();
    test_payload_sensitivity();
    test_game_round_trip();
    test_outcome_round_trip();
    test_checksum();
    test_keypair_generation();
    test_sign_and_verify();
    test_verify_rejections();
    test_sign_rejects_bad_seed_size();
    test_empty_batch_signs();
    std::println("{} checks, {} failures", g_checks, g_failures);
    return g_failures;
}
