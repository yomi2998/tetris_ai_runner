#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tournament_rating
{
    using CandidateId = std::uint64_t;

    struct GameRecord
    {
        CandidateId candidate_a = 0;
        CandidateId candidate_b = 0;
        double score_for_a = 0.0;
        std::uint64_t correlation_block = 0;
    };

    struct Options
    {
        double l2_lambda = 0.01;
        double gradient_tolerance = 1e-6;
        int max_newton_iterations = 100;
        double cg_relative_tolerance = 1e-12;
        int cg_max_iterations = 1000;
    };

    struct BootstrapOptions
    {
        int samples = 200;
        std::uint64_t seed = 1;
        double lower_probability = 0.025;
        double upper_probability = 0.975;
        int max_attempts = 10000;
    };

    struct Rating
    {
        CandidateId candidate = 0;
        double rating = 0.0;
        double std_error = 0.0;
        double rating_lower = 0.0;
        double rating_upper = 0.0;
        double expected_rank = 0.0;
        double rank_stddev = 0.0;
        std::int64_t games = 0;
        std::int64_t wins = 0;
        std::int64_t draws = 0;
        std::int64_t losses = 0;
    };

    struct Diagnostics
    {
        std::int64_t candidates = 0;
        std::int64_t games = 0;
        std::int64_t pairs = 0;
        double log_likelihood = 0.0;
        double ridge_penalty = 0.0;
        int newton_iterations = 0;
        double max_abs_gradient = 0.0;
        std::int64_t bootstrap_requested = 0;
        std::int64_t bootstrap_attempted = 0;
        std::int64_t bootstrap_valid = 0;
    };

    struct FitResult
    {
        // ok means the fit is usable, not that the gradient reached gradient_tolerance:
        // the fit accepts its floating point plateau, so convergence-sensitive callers
        // must check diagnostics.max_abs_gradient against their own threshold.
        bool ok = false;
        std::string error;
        std::vector<std::vector<CandidateId>> components;
        std::vector<Rating> ratings;
        Diagnostics diagnostics;
    };

    FitResult fit(std::vector<GameRecord> const &records, Options const &options = {});

    FitResult fit_with_bootstrap(std::vector<GameRecord> const &records, Options const &options,
                                 BootstrapOptions const &bootstrap);

    std::vector<CandidateId> optimizer_order(FitResult const &result);
}
