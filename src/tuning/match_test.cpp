#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <print>
#include <string_view>
#include <vector>

#include "tuning/domain.h"
#include "tuning/match.h"
#ifndef TUNING_MATCH_TEST_SKIP_TOJ
#include "tuning/toj_match.h"
#endif

namespace
{
    int failures = 0;

    void check(bool ok, std::string_view name)
    {
        std::println("{} {}", ok ? "PASS" : "FAIL", name);
        if (!ok)
        {
            ++failures;
        }
    }

    tuning::BatchGame make_game(tuning::GameId id, std::uint64_t root_seed, std::vector<double> theta_a, std::vector<double> theta_b)
    {
        tuning::BatchGame game;
        game.id = id;
        game.theta_a = std::move(theta_a);
        game.theta_b = std::move(theta_b);
        game.seed_a = tuning::derive_game_seed(root_seed, id, 0);
        game.seed_b = tuning::derive_game_seed(root_seed, id, 1);
        return game;
    }

    bool same_outcome(tuning::GameOutcome const& a, tuning::GameOutcome const& b)
    {
        return a.id == b.id && a.winner == b.winner && a.dead_a == b.dead_a && a.dead_b == b.dead_b
            && a.capped == b.capped && a.rounds == b.rounds && a.app_a == b.app_a && a.app_b == b.app_b
            && a.apl_a == b.apl_a && a.apl_b == b.apl_b && a.reason == b.reason;
    }

    struct FakeBatchBackend
    {
        static tuning::ParamSchema schema()
        {
            static char const* const names[] = { "alpha", "beta", "gamma" };
            static double const scales[] = { 1.0, 2.0, 4.0 };
            static double const defaults[] = { 0.5, -1.25, 8.0 };
            return tuning::ParamSchema{
                .adapter_id = "fake3",
                .names = names,
                .scales = scales,
                .defaults = defaults,
            };
        }

        bool validate(std::vector<double> const& theta) const
        {
            return theta.size() == 3;
        }

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const& games, tuning::RunConfig config) const
        {
            std::vector<tuning::GameOutcome> results;
            if (games.empty() || !tuning::valid_run_config(config))
            {
                return results;
            }
            results.reserve(games.size());
            for (tuning::BatchGame const& game : games)
            {
                if (!validate(game.theta_a) || !validate(game.theta_b))
                {
                    return {};
                }
                tuning::GameOutcome outcome;
                outcome.id = game.id;
                std::uint64_t const roll = tuning::mix64(game.seed_a ^ tuning::mix64(game.seed_b + game.id));
                outcome.winner = roll % 3 == 0 ? 0 : (roll % 3 == 1 ? 1 : -1);
                outcome.rounds = static_cast<int>(roll % 13) + 1;
                outcome.reason = outcome.winner > 0 ? tuning::WinReason::ASurvivor
                    : outcome.winner < 0 ? tuning::WinReason::BSurvivor : tuning::WinReason::CapDraw;
                results.push_back(outcome);
            }
            return results;
        }
    };

    static_assert(tuning::MatchBackend<FakeBatchBackend>);
#ifndef TUNING_MATCH_TEST_SKIP_TOJ
    static_assert(tuning::MatchBackend<tuning_toj::TojMatchBackend>);
#endif

    void run_seed_tests()
    {
        check(tuning::derive_game_seed(7, 1, 0) == tuning::derive_game_seed(7, 1, 0), "seed_derivation_is_deterministic");
        check(tuning::derive_game_seed(7, 1, 0) != tuning::derive_game_seed(7, 1, 1), "seed_derivation_separates_sides");
        check(tuning::derive_game_seed(7, 1, 0) != tuning::derive_game_seed(7, 2, 0), "seed_derivation_separates_games");
        check(tuning::derive_game_seed(7, 1, 0) != tuning::derive_game_seed(8, 1, 0), "seed_derivation_separates_roots");
    }

    void run_fake_backend_tests()
    {
        FakeBatchBackend backend;
        auto const schema = backend.schema();
        check(tuning::schema_consistent(schema), "fake_backend_schema_consistent");
        check(backend.validate({ 1.0, 2.0, 3.0 }), "fake_backend_accepts_matching_vector");
        check(!backend.validate({ 1.0, 2.0 }), "fake_backend_rejects_short_vector");

        check(backend.run_games({}, tuning::RunConfig{}).empty(), "backend_returns_empty_for_no_games");
        tuning::RunConfig zero_budget;
        zero_budget.iterations_per_move = 0;
        check(backend.run_games({ make_game(1, 99, { 0.5, -1.25, 8.0 }, { 0.5, -1.0, 8.0 }) }, zero_budget).empty(), "backend_returns_empty_for_zero_budget");

        tuning::RunConfig valid_config;
        check(tuning::valid_run_config(valid_config), "valid_run_config_accepts_defaults");
        tuning::RunConfig negative_threads = valid_config;
        negative_threads.threads = -2;
        check(!tuning::valid_run_config(negative_threads), "valid_run_config_rejects_negative_threads");
        tuning::RunConfig zero_iterations = valid_config;
        zero_iterations.iterations_per_move = 0;
        check(!tuning::valid_run_config(zero_iterations), "valid_run_config_rejects_zero_iterations");
        tuning::RunConfig huge_iterations = valid_config;
        huge_iterations.iterations_per_move = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
        check(!tuning::valid_run_config(huge_iterations), "valid_run_config_rejects_oversized_iterations");
        tuning::RunConfig zero_rounds = valid_config;
        zero_rounds.max_rounds = 0;
        check(!tuning::valid_run_config(zero_rounds), "valid_run_config_rejects_zero_rounds");
        tuning::RunConfig negative_rounds = valid_config;
        negative_rounds.max_rounds = -7;
        check(!tuning::valid_run_config(negative_rounds), "valid_run_config_rejects_negative_rounds");

        std::vector<tuning::BatchGame> const games = {
            make_game(1, 99, { 0.5, -1.25, 8.0 }, { 0.5, -1.0, 8.0 }),
            make_game(2, 99, { 0.5, -1.0, 8.0 }, { 0.5, -1.25, 8.0 }),
        };
        tuning::RunConfig config;
        tuning::RunConfig backend_negative_rounds = config;
        backend_negative_rounds.max_rounds = -5;
        check(backend.run_games(games, backend_negative_rounds).empty(), "backend_returns_empty_for_negative_rounds");
        tuning::RunConfig backend_zero_threads = config;
        backend_zero_threads.threads = 0;
        check(backend.run_games(games, backend_zero_threads).empty(), "backend_returns_empty_for_zero_threads");
        tuning::RunConfig backend_huge = config;
        backend_huge.iterations_per_move = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
        check(backend.run_games(games, backend_huge).empty(), "backend_returns_empty_for_oversized_budget");
        auto const first = backend.run_games(games, config);
        auto const second = backend.run_games(games, config);
        check(first.size() == 2 && second.size() == 2, "fake_backend_returns_one_outcome_per_game");

        bool identical = true;
        for (std::size_t i = 0; i < first.size() && i < second.size(); ++i)
        {
            identical = identical && same_outcome(first[i], second[i]);
        }
        check(identical, "fake_backend_is_deterministic");

        bool order = true;
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            order = order && first[i].id == games[i].id;
        }
        check(order, "fake_backend_preserves_game_ids_and_order");

        bool winner_convention = true;
        for (auto const& outcome : first)
        {
            winner_convention = winner_convention && outcome.winner >= -1 && outcome.winner <= 1
                && outcome.reason != tuning::WinReason::Unknown
                && outcome.rounds > 0;
        }
        check(winner_convention, "fake_backend_winner_convention");

        std::vector<tuning::BatchGame> invalid = games;
        invalid[1].theta_b = { 1.0, 2.0 };
        check(backend.run_games(invalid, config).empty(), "backend_rejects_invalid_vector_batch");
    }

#ifndef TUNING_MATCH_TEST_SKIP_TOJ
    void run_toj_backend_tests()
    {
        tuning_toj::TojMatchBackend backend;
        auto const schema = backend.schema();
        check(tuning::schema_consistent(schema), "toj_backend_schema_consistent");
        check(backend.validate({ schema.defaults.begin(), schema.defaults.end() }), "toj_backend_accepts_defaults");

        std::vector<double> short_theta(schema.defaults.begin(), schema.defaults.end() - 1);
        check(!backend.validate(short_theta), "toj_backend_rejects_short_vector");
        std::vector<double> dirty_theta(schema.defaults.begin(), schema.defaults.end());
        dirty_theta[3] = std::nan("");
        check(!backend.validate(dirty_theta), "toj_backend_rejects_nonfinite_vector");

        std::vector<double> const base(schema.defaults.begin(), schema.defaults.end());
        std::vector<double> shifted = base;
        shifted[0] += schema.scales[0];
        std::vector<tuning::BatchGame> const games = {
            make_game(1, 1234, base, shifted),
            make_game(2, 1234, shifted, base),
        };
        tuning::RunConfig config;
        config.threads = 2;
        config.iterations_per_move = 256;
        config.max_rounds = 24;

        auto const first = backend.run_games(games, config);
        check(first.size() == games.size(), "toj_backend_returns_one_outcome_per_game");

        bool shape = true;
        for (std::size_t i = 0; i < first.size(); ++i)
        {
            shape = shape && first[i].id == games[i].id
                && first[i].winner >= -1 && first[i].winner <= 1
                && first[i].rounds > 0 && first[i].rounds <= config.max_rounds
                && first[i].reason != tuning::WinReason::Unknown
                && first[i].app_a >= 0.0 && first[i].app_b >= 0.0
                && first[i].apl_a >= 0.0 && first[i].apl_b >= 0.0;
        }
        check(shape, "toj_backend_outcomes_normalized");

        auto const second = backend.run_games(games, config);
        bool replay = second.size() == first.size();
        for (std::size_t i = 0; i < first.size() && i < second.size(); ++i)
        {
            replay = replay && same_outcome(first[i], second[i]);
        }
        check(replay, "toj_backend_replays_identically_under_iteration_budget");

        check(backend.run_games({}, config).empty(), "toj_backend_returns_empty_for_no_games");
        tuning::RunConfig zero_budget = config;
        zero_budget.iterations_per_move = 0;
        check(backend.run_games(games, zero_budget).empty(), "toj_backend_returns_empty_for_zero_budget");
        tuning::RunConfig toj_negative_rounds = config;
        toj_negative_rounds.max_rounds = -1;
        check(backend.run_games(games, toj_negative_rounds).empty(), "toj_backend_returns_empty_for_negative_rounds");
        tuning::RunConfig toj_huge = config;
        toj_huge.iterations_per_move = static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1;
        check(backend.run_games(games, toj_huge).empty(), "toj_backend_returns_empty_for_oversized_budget");

        std::vector<tuning::BatchGame> invalid = games;
        invalid[0].theta_a.push_back(1.0);
        check(backend.run_games(invalid, config).empty(), "toj_backend_rejects_invalid_vector_batch");
    }
#endif
}

int main()
{
    run_seed_tests();
    run_fake_backend_tests();
#ifndef TUNING_MATCH_TEST_SKIP_TOJ
    run_toj_backend_tests();
#endif
    if (failures == 0)
    {
        std::println("ALL TUNING MATCH CHECKS PASSED");
        return 0;
    }
    std::println("{} TUNING MATCH CHECK(S) FAILED", failures);
    return 1;
}
