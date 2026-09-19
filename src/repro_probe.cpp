#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <print>
#include <string>
#include <vector>

#include "tournament/toj_conformance.h"
#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace repro_probe
{
    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;

    struct ProbeConfig
    {
        int games = 4;
        std::size_t iterations = 40;
        int max_rounds = 600;
        int threads = 1;
        std::uint64_t seed = 555;
    };

    enum class Flag
    {
        Games,
        Iterations,
        MaxRounds,
        Threads,
        Seed,
        Unknown,
    };

    Flag flag_of(char const *flag)
    {
        if (std::strcmp(flag, "--games") == 0)
        {
            return Flag::Games;
        }
        if (std::strcmp(flag, "--iterations") == 0)
        {
            return Flag::Iterations;
        }
        if (std::strcmp(flag, "--max-rounds") == 0)
        {
            return Flag::MaxRounds;
        }
        if (std::strcmp(flag, "--threads") == 0)
        {
            return Flag::Threads;
        }
        if (std::strcmp(flag, "--seed") == 0)
        {
            return Flag::Seed;
        }
        return Flag::Unknown;
    }

    bool parse_int(char const *raw, int &out)
    {
        try
        {
            std::size_t used = 0;
            out = std::stoi(raw, &used);
            return used == std::strlen(raw);
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_count(char const *raw, std::size_t &out)
    {
        try
        {
            std::size_t used = 0;
            unsigned long long const value = std::stoull(raw, &used);
            out = static_cast<std::size_t>(value);
            return used == std::strlen(raw);
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_seed(char const *raw, std::uint64_t &out)
    {
        try
        {
            std::size_t used = 0;
            unsigned long long const value = std::stoull(raw, &used);
            out = static_cast<std::uint64_t>(value);
            return used == std::strlen(raw);
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool values_valid(ProbeConfig const &config)
    {
        if (config.games < 0 || config.threads <= 0 || config.max_rounds <= 0)
        {
            return false;
        }
        return config.iterations > 0
            && config.iterations <= static_cast<std::size_t>(std::numeric_limits<int>::max());
    }

    bool parse_args(int argc, char *argv[], ProbeConfig &config)
    {
        for (int i = 1; i < argc; ++i)
        {
            Flag const flag = flag_of(argv[i]);
            if (flag == Flag::Unknown)
            {
                std::println(stderr, "unknown flag {}", argv[i]);
                return false;
            }
            if (i + 1 >= argc)
            {
                std::println(stderr, "missing value for {}", argv[i]);
                return false;
            }
            char const *raw = argv[i + 1];
            bool parsed = false;
            switch (flag)
            {
            case Flag::Games:
                parsed = parse_int(raw, config.games);
                break;
            case Flag::Iterations:
                parsed = parse_count(raw, config.iterations);
                break;
            case Flag::MaxRounds:
                parsed = parse_int(raw, config.max_rounds);
                break;
            case Flag::Threads:
                parsed = parse_int(raw, config.threads);
                break;
            case Flag::Seed:
                parsed = parse_seed(raw, config.seed);
                break;
            case Flag::Unknown:
                break;
            }
            if (!parsed)
            {
                std::println(stderr, "invalid numeric value for {}", argv[i]);
                return false;
            }
            ++i;
        }
        if (!values_valid(config))
        {
            std::println(stderr, "invalid configuration values");
            return false;
        }
        return true;
    }

    std::vector<tuning::BatchGame> build_games(ProbeConfig const &config)
    {
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        std::vector<double> const defaults(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> theta_b(defaults.size());
        for (std::size_t i = 0; i < defaults.size(); ++i)
        {
            theta_b[i] = defaults[i] * (1.0 + 0.01 * static_cast<double>(i % 5));
        }
        std::vector<tuning::BatchGame> games;
        games.reserve(static_cast<std::size_t>(config.games));
        for (int k = 1; k <= config.games; ++k)
        {
            tuning::BatchGame game;
            game.id = static_cast<tuning::GameId>(k);
            game.theta_a = defaults;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(config.seed, game.id, 0);
            game.seed_b = tuning::derive_game_seed(config.seed, game.id, 1);
            games.push_back(std::move(game));
        }
        return games;
    }

    tuning::RunConfig run_config_for(ProbeConfig const &config)
    {
        tuning::RunConfig run_config;
        run_config.threads = config.threads;
        run_config.iterations_per_move = config.iterations;
        run_config.max_rounds = config.max_rounds;
        return run_config;
    }

    std::uint64_t outcome_checksum(std::vector<tuning::GameOutcome> const &outcomes)
    {
        std::uint64_t hash = tuning::kFnvOffsetBasis;
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            std::string const encoded = tournament_wire::encode_outcome(tournament_wire::to_wire(outcome));
            hash = tuning::fnv1a_bytes(hash, encoded.data(), encoded.size());
        }
        return hash;
    }

    void print_outcomes(std::vector<tuning::GameOutcome> const &outcomes)
    {
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            std::println("game {} winner {} dead_a {} dead_b {} capped {} rounds {} reason {}"
                         " app_a {:g} app_b {:g} apl_a {:g} apl_b {:g}",
                outcome.id, outcome.winner, outcome.dead_a ? 1 : 0, outcome.dead_b ? 1 : 0,
                outcome.capped ? 1 : 0, outcome.rounds, static_cast<int>(outcome.reason),
                outcome.app_a, outcome.app_b, outcome.apl_a, outcome.apl_b);
        }
    }

    int run(ProbeConfig const &config)
    {
        try
        {
            auto const shared_context = tuning_toj::TojAdapter::make_shared_context();
            if (!shared_context)
            {
                std::println(stderr, "probe failed: cannot prepare shared TOJ context");
                return 1;
            }
            TojBackend backend(shared_context);
            std::vector<tuning::BatchGame> const games = build_games(config);
            tuning::RunConfig const run_config = run_config_for(config);
            std::println("config games={} iterations={} max_rounds={} threads={} seed={}",
                config.games, config.iterations, config.max_rounds, config.threads, config.seed);
            std::vector<tuning::GameOutcome> const outcomes = backend.run_games(games, run_config);
            print_outcomes(outcomes);
            std::println("checksum {:016x}", outcome_checksum(outcomes));
            std::uint64_t const conformance
                = tournament_identity::toj_conformance_fingerprint(shared_context);
            std::println("conformance {:016x}", conformance);
            return 0;
        }
        catch (std::exception const &error)
        {
            std::println(stderr, "probe failed: {}", error.what());
            return 1;
        }
        catch (...)
        {
            std::println(stderr, "probe failed: unknown exception");
            return 1;
        }
    }
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);
    repro_probe::ProbeConfig config;
    if (!repro_probe::parse_args(argc, argv, config))
    {
        return 1;
    }
    return repro_probe::run(config);
}
