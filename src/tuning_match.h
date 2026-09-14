#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "tuning_domain.h"

namespace tuning
{
    using GameId = std::uint64_t;

    enum class WinReason : std::uint8_t
    {
        ASurvivor,
        BSurvivor,
        ACapApl,
        BCapApl,
        ABothDeadApl,
        BBothDeadApl,
        BothDeadDraw,
        CapDraw,
        Unknown,
    };

    struct RunConfig
    {
        int threads = 1;
        std::size_t iterations_per_move = 1000;
        int max_rounds = 3600;
    };

    inline constexpr bool valid_run_config(RunConfig const& config)
    {
        if (config.threads <= 0)
        {
            return false;
        }
        if (config.iterations_per_move == 0
            || config.iterations_per_move > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return false;
        }
        return config.max_rounds > 0;
    }

    struct BatchGame
    {
        GameId id = 0;
        std::vector<double> theta_a;
        std::vector<double> theta_b;
        std::uint64_t seed_a = 0;
        std::uint64_t seed_b = 0;
    };

    struct GameOutcome
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
        WinReason reason = WinReason::Unknown;
    };

    inline constexpr std::uint64_t mix64(std::uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    inline constexpr std::uint64_t derive_game_seed(std::uint64_t root_seed, GameId game_id, std::uint32_t side)
    {
        return mix64(root_seed ^ mix64(game_id * 0x9E3779B97F4A7C15ULL + static_cast<std::uint64_t>(side)));
    }

    template<class B>
    concept MatchBackend = requires(B const& backend, std::vector<BatchGame> const& games, RunConfig const& config, std::vector<double> const& theta)
    {
        { backend.schema() } -> std::convertible_to<ParamSchema>;
        { backend.validate(theta) } -> std::convertible_to<bool>;
        { backend.run_games(games, config) } -> std::same_as<std::vector<GameOutcome>>;
    };
}
