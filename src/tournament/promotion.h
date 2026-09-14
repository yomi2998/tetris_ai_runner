#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "tuning/match.h"

namespace tournament_promotion
{
    struct Options
    {
        int pairs = 32;
        double promotion_threshold = 0.5;
        std::uint64_t seed = 1;
        tuning::GameId first_game_id = 0xE000000000000000ULL;
    };

    struct Result
    {
        bool ok = false;
        std::string error;
        int pairs = 0;
        int games = 0;
        double mean_score = 0.0;
        double lower_bound = 0.0;
        double upper_bound = 1.0;
        bool promoted = false;
        std::vector<tuning::GameOutcome> outcomes;
    };

    inline double one_sided_95_multiplier(int degrees_of_freedom)
    {
        if (degrees_of_freedom <= 1) return 6.314;
        if (degrees_of_freedom == 2) return 2.920;
        if (degrees_of_freedom == 3) return 2.353;
        if (degrees_of_freedom == 4) return 2.132;
        if (degrees_of_freedom == 5) return 2.015;
        if (degrees_of_freedom == 6) return 1.943;
        if (degrees_of_freedom == 7) return 1.895;
        if (degrees_of_freedom == 8) return 1.860;
        if (degrees_of_freedom == 9) return 1.833;
        if (degrees_of_freedom == 10) return 1.812;
        if (degrees_of_freedom <= 12) return 1.796;
        if (degrees_of_freedom <= 15) return 1.771;
        if (degrees_of_freedom <= 20) return 1.725;
        if (degrees_of_freedom <= 25) return 1.708;
        if (degrees_of_freedom <= 30) return 1.697;
        if (degrees_of_freedom <= 40) return 1.684;
        if (degrees_of_freedom <= 60) return 1.671;
        if (degrees_of_freedom <= 120) return 1.658;
        return 1.6448536269514722;
    }

    inline double score_for_side_a(int winner)
    {
        if (winner > 0)
        {
            return 1.0;
        }
        if (winner < 0)
        {
            return 0.0;
        }
        return 0.5;
    }

    template<tuning::MatchBackend Backend>
    Result evaluate(Backend const &backend, std::vector<double> const &candidate,
                    std::vector<double> const &incumbent, tuning::RunConfig const &run_config,
                    Options const &options)
    {
        Result result;
        if (options.pairs < 2)
        {
            result.error = "promotion pairs must be at least two";
            return result;
        }
        if (!std::isfinite(options.promotion_threshold)
            || options.promotion_threshold < 0.0 || options.promotion_threshold > 1.0)
        {
            result.error = "promotion threshold must be finite and inside [0, 1]";
            return result;
        }
        if (options.seed == 0)
        {
            result.error = "promotion seed must be nonzero";
            return result;
        }
        if (!tuning::valid_run_config(run_config))
        {
            result.error = "invalid match run configuration";
            return result;
        }
        if (!backend.validate(candidate) || !backend.validate(incumbent))
        {
            result.error = "invalid candidate or incumbent parameter vector";
            return result;
        }
        std::uint64_t const game_count = static_cast<std::uint64_t>(options.pairs) * 2;
        if (options.first_game_id > std::numeric_limits<tuning::GameId>::max() - game_count)
        {
            result.error = "promotion game id range overflows";
            return result;
        }

        std::vector<tuning::BatchGame> games;
        games.reserve(static_cast<std::size_t>(game_count));
        for (int pair = 0; pair < options.pairs; ++pair)
        {
            std::uint64_t const block = tuning::mix64(options.seed
                ^ tuning::mix64(static_cast<std::uint64_t>(pair)));
            std::uint64_t const seed_a = tuning::mix64(block ^ 0xA24BAED4963EE407ULL);
            std::uint64_t const seed_b = tuning::mix64(block ^ 0x9FB21C651E98DF25ULL);
            tuning::BatchGame first;
            first.id = options.first_game_id + static_cast<std::uint64_t>(pair) * 2;
            first.theta_a = candidate;
            first.theta_b = incumbent;
            first.seed_a = seed_a;
            first.seed_b = seed_b;
            tuning::BatchGame second;
            second.id = first.id + 1;
            second.theta_a = incumbent;
            second.theta_b = candidate;
            second.seed_a = seed_a;
            second.seed_b = seed_b;
            games.push_back(std::move(first));
            games.push_back(std::move(second));
        }

        try
        {
            result.outcomes = backend.run_games(games, run_config);
        }
        catch (std::exception const &error)
        {
            result.error = "match backend failed during promotion: " + std::string(error.what());
            return result;
        }
        catch (...)
        {
            result.error = "match backend failed during promotion";
            return result;
        }
        if (result.outcomes.size() != games.size())
        {
            result.error = "match backend returned the wrong promotion outcome count";
            result.outcomes.clear();
            return result;
        }
        double sum = 0.0;
        double sum_squares = 0.0;
        for (int pair = 0; pair < options.pairs; ++pair)
        {
            std::size_t const first_index = static_cast<std::size_t>(pair) * 2;
            std::size_t const second_index = first_index + 1;
            if (result.outcomes[first_index].id != games[first_index].id
                || result.outcomes[second_index].id != games[second_index].id)
            {
                result.error = "match backend returned out-of-order promotion outcomes";
                result.outcomes.clear();
                return result;
            }
            int const first_winner = result.outcomes[first_index].winner;
            int const second_winner = result.outcomes[second_index].winner;
            if (first_winner < -1 || first_winner > 1 || second_winner < -1 || second_winner > 1)
            {
                result.error = "match backend returned an invalid promotion winner";
                result.outcomes.clear();
                return result;
            }
            double const first_score = score_for_side_a(first_winner);
            double const second_score = 1.0 - score_for_side_a(second_winner);
            double const pair_score = 0.5 * (first_score + second_score);
            sum += pair_score;
            sum_squares += pair_score * pair_score;
        }

        double const count = static_cast<double>(options.pairs);
        double const mean = sum / count;
        double const variance = std::max(0.0, (sum_squares - sum * sum / count) / (count - 1.0));
        double const margin = one_sided_95_multiplier(options.pairs - 1) * std::sqrt(variance / count);
        result.ok = true;
        result.pairs = options.pairs;
        result.games = options.pairs * 2;
        result.mean_score = mean;
        result.lower_bound = std::max(0.0, mean - margin);
        result.upper_bound = std::min(1.0, mean + margin);
        result.promoted = result.lower_bound > options.promotion_threshold;
        return result;
    }
}
