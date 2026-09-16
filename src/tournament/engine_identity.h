#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/match.h"

namespace tournament_identity
{
    template<class RunGames>
    std::uint64_t engine_fingerprint(RunGames const &run_games,
                                     std::vector<double> const &theta_a,
                                     std::vector<double> const &theta_b)
    {
        tuning::RunConfig config;
        config.threads = 1;
        config.iterations_per_move = 16;
        config.max_rounds = 150;
        std::vector<tuning::BatchGame> games(2);
        for (std::uint64_t id = 1; id <= 2; ++id)
        {
            tuning::BatchGame &game = games[static_cast<std::size_t>(id - 1)];
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(0x1DE67A7E5ULL, id, 0);
            game.seed_b = tuning::derive_game_seed(0x1DE67A7E5ULL, id, 1);
        }
        std::vector<tuning::GameOutcome> const outcomes = run_games(games, config);
        std::uint64_t hash = tuning::kFnvOffsetBasis;
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            std::string const encoded = tournament_wire::encode_outcome(tournament_wire::to_wire(outcome));
            hash = tuning::fnv1a_bytes(hash, encoded.data(), encoded.size());
        }
        return hash;
    }

    template<class Adapter, class RunGames>
    std::uint64_t adapter_engine_fingerprint(RunGames const &run_games)
    {
        tuning::ParamSchema const schema = Adapter::schema();
        std::vector<double> theta_a(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> theta_b(theta_a.size());
        for (std::size_t i = 0; i < theta_a.size(); ++i)
        {
            theta_b[i] = theta_a[i] * (1.0 + 0.01 * static_cast<double>(i % 5));
        }
        return engine_fingerprint(run_games, theta_a, theta_b);
    }
}
