#include "tournament/rating.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <print>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool condition, std::string const &name)
    {
        if (condition)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            std::println("FAIL: {}", name);
            ++failures;
        }
    }

    double logistic(double x)
    {
        return 1.0 / (1.0 + std::exp(-x));
    }

    void add_games(std::vector<tournament_rating::GameRecord> &records, std::uint64_t a, std::uint64_t b,
                   int a_wins, int b_wins, int draws, std::uint64_t first_block)
    {
        std::uint64_t block = first_block;
        for (int i = 0; i < a_wins; ++i)
        {
            records.push_back({a, b, 1.0, block});
            ++block;
        }
        for (int i = 0; i < b_wins; ++i)
        {
            records.push_back({a, b, 0.0, block});
            ++block;
        }
        for (int i = 0; i < draws; ++i)
        {
            records.push_back({a, b, 0.5, block});
            ++block;
        }
    }

    bool same_fit(tournament_rating::FitResult const &a, tournament_rating::FitResult const &b)
    {
        if (!a.ok || !b.ok || a.ratings.size() != b.ratings.size())
        {
            return false;
        }
        for (size_t k = 0; k < a.ratings.size(); ++k)
        {
            auto const &x = a.ratings[k];
            auto const &y = b.ratings[k];
            if (x.candidate != y.candidate)
            {
                return false;
            }
            if (x.rating != y.rating || x.std_error != y.std_error)
            {
                return false;
            }
            if (x.rating_lower != y.rating_lower || x.rating_upper != y.rating_upper)
            {
                return false;
            }
            if (x.expected_rank != y.expected_rank || x.rank_stddev != y.rank_stddev)
            {
                return false;
            }
        }
        return a.diagnostics.candidates == b.diagnostics.candidates
            && a.diagnostics.games == b.diagnostics.games
            && a.diagnostics.pairs == b.diagnostics.pairs
            && a.diagnostics.log_likelihood == b.diagnostics.log_likelihood
            && a.diagnostics.newton_iterations == b.diagnostics.newton_iterations
            && a.diagnostics.max_abs_gradient == b.diagnostics.max_abs_gradient
            && a.diagnostics.bootstrap_requested == b.diagnostics.bootstrap_requested
            && a.diagnostics.bootstrap_attempted == b.diagnostics.bootstrap_attempted
            && a.diagnostics.bootstrap_valid == b.diagnostics.bootstrap_valid;
    }

    void test_equal_players()
    {
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t const ids[4] = {11, 22, 33, 44};
        std::uint64_t block = 1;
        for (size_t i = 0; i < 4; ++i)
        {
            for (size_t j = i + 1; j < 4; ++j)
            {
                add_games(records, ids[i], ids[j], 20, 20, 0, block);
                block += 1000;
            }
        }
        auto const result = tournament_rating::fit(records);
        check(result.ok, "equal players: fit succeeds");
        if (!result.ok)
        {
            return;
        }
        double sum = 0.0;
        double max_abs = 0.0;
        for (auto const &entry : result.ratings)
        {
            sum += entry.rating;
            max_abs = std::max(max_abs, std::fabs(entry.rating));
        }
        check(std::fabs(sum) < 1e-12, "equal players: centered ratings sum to zero");
        check(max_abs < 1e-9, "equal players: balanced schedule yields zero ratings");
        check(result.ratings[0].candidate == 11 && result.ratings[1].candidate == 22
                  && result.ratings[2].candidate == 33 && result.ratings[3].candidate == 44,
              "equal players: tied ratings order by candidate id");
        check(std::fabs(result.ratings[0].std_error - result.ratings[3].std_error) < 1e-9
                  && result.ratings[0].std_error > 0.0,
              "equal players: identical uncertainty across players");
        check(result.diagnostics.games == 240, "equal players: game count reported");
        check(result.diagnostics.pairs == 6, "equal players: pair count reported");
        check(result.diagnostics.newton_iterations == 0, "equal players: balanced data starts converged");
        check(result.ratings[0].expected_rank == 2.5 && result.ratings[3].expected_rank == 2.5,
              "equal players: tied expected rank");
        check(result.ratings[0].rating_lower == result.ratings[0].rating
                  && result.ratings[0].rating_upper == result.ratings[0].rating,
              "equal players: degenerate interval without bootstrap");
        auto const order = tournament_rating::optimizer_order(result);
        check(order.size() == 4 && order[0] == 11 && order[3] == 44, "equal players: optimizer order");
    }

    void test_known_strength()
    {
        double const strength[6] = {2.0, 1.2, 0.4, 0.0, -0.8, -1.6};
        std::uint64_t const ids[6] = {101, 102, 103, 104, 105, 106};
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t block = 1;
        for (size_t i = 0; i < 6; ++i)
        {
            for (size_t j = i + 1; j < 6; ++j)
            {
                double const expected = logistic(strength[i] - strength[j]);
                int const wins_i = static_cast<int>(std::llround(300.0 * expected));
                add_games(records, ids[i], ids[j], wins_i, 300 - wins_i, 0, block);
                block += 100000;
            }
        }
        auto const result = tournament_rating::fit(records);
        check(result.ok, "known strength: fit succeeds");
        if (!result.ok)
        {
            return;
        }
        bool order_ok = true;
        for (size_t k = 0; k < 6; ++k)
        {
            order_ok = order_ok && result.ratings[k].candidate == ids[k];
        }
        check(order_ok, "known strength: recovered ordering matches true strengths");
        double const true_mean = (2.0 + 1.2 + 0.4 + 0.0 - 0.8 - 1.6) / 6.0;
        double max_error = 0.0;
        for (size_t k = 0; k < 6; ++k)
        {
            double const target = strength[k] - true_mean;
            max_error = std::max(max_error, std::fabs(result.ratings[k].rating - target));
        }
        check(max_error < 0.05, "known strength: ratings close to centered true strengths");
        bool sorted_desc = true;
        for (size_t k = 1; k < 6; ++k)
        {
            sorted_desc = sorted_desc && result.ratings[k - 1].rating >= result.ratings[k].rating;
        }
        check(sorted_desc, "known strength: ratings sorted for optimizer consumption");
        bool se_ok = true;
        for (auto const &entry : result.ratings)
        {
            se_ok = se_ok && entry.std_error > 0.0 && std::isfinite(entry.std_error);
        }
        check(se_ok, "known strength: positive finite standard errors");
        check(result.diagnostics.games == 4500, "known strength: game count reported");
    }

    void test_ties()
    {
        std::vector<tournament_rating::GameRecord> records;
        add_games(records, 1, 2, 0, 0, 60, 1);
        auto const draws_only = tournament_rating::fit(records);
        check(draws_only.ok, "ties: all-draw pair fits");
        if (draws_only.ok)
        {
            check(std::fabs(draws_only.ratings[0].rating - draws_only.ratings[1].rating) < 1e-12,
                  "ties: all-draw pair has equal ratings");
            check(draws_only.ratings[0].expected_rank == 1.5, "ties: all-draw pair shares expected rank");
        }
        std::vector<tournament_rating::GameRecord> mixed;
        add_games(mixed, 1, 2, 30, 0, 0, 1);
        add_games(mixed, 2, 3, 30, 0, 0, 1000);
        add_games(mixed, 1, 3, 0, 0, 30, 2000);
        auto const result = tournament_rating::fit(mixed);
        check(result.ok, "ties: mixed decisive and drawn games fit");
        if (!result.ok)
        {
            return;
        }
        check(result.ratings[0].candidate == 1 && result.ratings[1].candidate == 2
                  && result.ratings[2].candidate == 3,
              "ties: draw credit keeps decisive ordering");
    }

    void test_disconnected()
    {
        std::vector<tournament_rating::GameRecord> records;
        add_games(records, 1, 2, 10, 10, 5, 1);
        add_games(records, 3, 4, 5, 5, 5, 100);
        auto const result = tournament_rating::fit(records);
        check(!result.ok, "disconnected: fit rejected");
        check(result.error.find("disconnected") != std::string::npos,
              "disconnected: error names the problem");
        check(result.components.size() == 2 && result.components[0].size() == 2
                  && result.components[1].size() == 2,
              "disconnected: both components reported");
        check(result.components[0] == std::vector<tournament_rating::CandidateId>{1, 2}
                  && result.components[1] == std::vector<tournament_rating::CandidateId>{3, 4},
              "disconnected: component members listed by candidate id");
        check(result.ratings.empty(), "disconnected: no ratings returned");
    }

    void test_reordering()
    {
        std::uint64_t const ids[8] = {3, 17, 42, 58, 91, 106, 203, 999};
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t block = 1;
        for (size_t i = 0; i < 8; ++i)
        {
            for (size_t j = i + 1; j < 8; ++j)
            {
                int const a_wins = 5 + static_cast<int>((i * 7 + j * 13) % 20);
                int const b_wins = 3 + static_cast<int>((i * 11 + j * 3) % 15);
                int const draws = static_cast<int>((i + j) % 7);
                add_games(records, ids[i], ids[j], a_wins, b_wins, draws, block);
                block += 10000;
            }
        }
        auto const base = tournament_rating::fit(records);
        check(base.ok, "reordering: base fit succeeds");
        if (!base.ok)
        {
            return;
        }
        std::vector<tournament_rating::GameRecord> strided;
        for (size_t offset = 0; offset < 3; ++offset)
        {
            for (size_t k = offset; k < records.size(); k += 3)
            {
                strided.push_back(records[k]);
            }
        }
        std::vector<tournament_rating::GameRecord> reversed(records.rbegin(), records.rend());
        auto const from_strided = tournament_rating::fit(strided);
        auto const from_reversed = tournament_rating::fit(reversed);
        check(same_fit(base, from_strided) && same_fit(base, from_reversed),
              "reordering: bitwise identical results for permuted input");
    }

    void test_bootstrap()
    {
        std::uint64_t const ids[8] = {3, 17, 42, 58, 91, 106, 203, 999};
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t block = 1;
        for (size_t i = 0; i < 8; ++i)
        {
            for (size_t j = i + 1; j < 8; ++j)
            {
                int const a_wins = 5 + static_cast<int>((i * 7 + j * 13) % 20);
                int const b_wins = 3 + static_cast<int>((i * 11 + j * 3) % 15);
                int const draws = static_cast<int>((i + j) % 7);
                add_games(records, ids[i], ids[j], a_wins, b_wins, draws, block);
                block += 10000;
            }
        }
        for (size_t k = 0; k < records.size(); ++k)
        {
            records[k].correlation_block = k / 2;
        }
        auto const first = tournament_rating::fit_with_bootstrap(records, {}, {60, 42, 0.025, 0.975});
        auto const second = tournament_rating::fit_with_bootstrap(records, {}, {60, 42, 0.025, 0.975});
        check(first.ok && second.ok, "bootstrap: fits succeed");
        if (first.ok && second.ok)
        {
            check(same_fit(first, second), "bootstrap: same seed reproduces results exactly");
        }
        auto const other_seed = tournament_rating::fit_with_bootstrap(records, {}, {60, 43, 0.025, 0.975});
        bool differs = other_seed.ok;
        if (other_seed.ok)
        {
            differs = false;
            for (size_t k = 0; k < first.ratings.size(); ++k)
            {
                auto const &a = first.ratings[k];
                auto const &b = other_seed.ratings[k];
                if (a.rating_lower != b.rating_lower || a.rating_upper != b.rating_upper
                    || a.expected_rank != b.expected_rank || a.rank_stddev != b.rank_stddev)
                {
                    differs = true;
                }
            }
        }
        check(differs, "bootstrap: different seed changes uncertainty estimates");
        check(first.ok && first.diagnostics.bootstrap_requested == 60
                  && first.diagnostics.bootstrap_valid == 60
                  && first.diagnostics.bootstrap_attempted >= 60,
              "bootstrap: requested, attempted, and valid counts reported");
        bool sane = first.ok;
        if (first.ok)
        {
            for (auto const &entry : first.ratings)
            {
                sane = sane && entry.rating_lower <= entry.rating && entry.rating <= entry.rating_upper
                    && entry.rating_lower <= entry.rating_upper && entry.expected_rank >= 1.0
                    && entry.expected_rank <= 8.0 && entry.rank_stddev >= 0.0;
            }
        }
        check(sane, "bootstrap: intervals and rank moments well formed");

        std::vector<tournament_rating::GameRecord> symmetric;
        add_games(symmetric, 5, 6, 0, 0, 60, 1);
        for (size_t k = 0; k < symmetric.size(); ++k)
        {
            symmetric[k].correlation_block = k / 2;
        }
        auto const flat = tournament_rating::fit_with_bootstrap(symmetric, {}, {40, 7, 0.025, 0.975});
        check(flat.ok && flat.ratings[0].rating == 0.0 && flat.ratings[0].rating_lower == 0.0
                  && flat.ratings[0].rating_upper == 0.0 && flat.ratings[0].expected_rank == 1.5
                  && flat.ratings[0].rank_stddev == 0.0,
              "bootstrap: symmetric draw data has collapsed deterministic intervals");
    }

    void test_se_solve_failure()
    {
        std::vector<tournament_rating::GameRecord> records;
        std::uint64_t const ids[4] = {11, 22, 33, 44};
        std::uint64_t block = 1;
        for (size_t i = 0; i < 4; ++i)
        {
            for (size_t j = i + 1; j < 4; ++j)
            {
                add_games(records, ids[i], ids[j], 20, 20, 0, block);
                block += 1000;
            }
        }
        tournament_rating::Options starved;
        starved.cg_max_iterations = 1;
        auto const result = tournament_rating::fit(records, starved);
        check(!result.ok, "se failure: starved cg budget on the uncertainty solve is rejected");
        check(result.error.find("standard error") != std::string::npos
                  && result.error.find("did not converge") != std::string::npos
                  && result.error.find("candidate") != std::string::npos,
              "se failure: structured error names the candidate and the solve");
        check(result.ratings.empty(), "se failure: no ratings returned");
    }

    std::vector<tournament_rating::GameRecord> bridge_dataset()
    {
        std::vector<tournament_rating::GameRecord> records;
        add_games(records, 1, 2, 40, 20, 0, 1);
        add_games(records, 3, 4, 35, 25, 0, 100);
        add_games(records, 5, 6, 30, 30, 0, 200);
        add_games(records, 2, 3, 1, 0, 0, 400);
        add_games(records, 4, 5, 0, 1, 0, 401);
        return records;
    }

    void test_convergence_floor()
    {
        auto const records = bridge_dataset();
        tournament_rating::Options unattainable;
        unattainable.gradient_tolerance = 1e-12;
        auto const floored = tournament_rating::fit(records, unattainable);
        check(floored.ok, "convergence floor: unattainable tolerance still fits at the reachable floor");
        if (floored.ok)
        {
            check(floored.diagnostics.newton_iterations >= 1
                      && floored.diagnostics.newton_iterations < unattainable.max_newton_iterations,
                  "convergence floor: plateau stop lands inside the budget");
            check(floored.diagnostics.max_abs_gradient > 0.0
                      && floored.diagnostics.max_abs_gradient < 1e-4,
                  "convergence floor: reported gradient is the best achieved value");
        }

        tournament_rating::Options tiny_budget;
        tiny_budget.gradient_tolerance = 1e-12;
        tiny_budget.max_newton_iterations = 1;
        auto const failed = tournament_rating::fit(records, tiny_budget);
        check(!failed.ok, "convergence floor: a still-improving fit under a tiny budget fails");
        check(failed.error.find("did not converge") != std::string::npos
                  && failed.error.find("0.000000") == std::string::npos,
              "convergence floor: failure error prints the real gradient: " + failed.error);
        check(failed.diagnostics.newton_iterations == 1 && failed.diagnostics.max_abs_gradient > 0.0,
              "convergence floor: failure diagnostics report iterations and the achieved gradient");
    }

    void test_bootstrap_attempts()
    {
        auto const records = bridge_dataset();
        auto const point = tournament_rating::fit(records);
        check(point.ok, "bootstrap attempts: point fit on the bridged sparse graph succeeds");
        if (!point.ok)
        {
            return;
        }

        auto const short_run = tournament_rating::fit_with_bootstrap(records, {},
                                                                     {40, 2024, 0.025, 0.975, 40});
        check(!short_run.ok, "bootstrap attempts: exhausted attempt budget fails loudly");
        check(short_run.error.find("requested") != std::string::npos
                  && short_run.error.find("resamples") != std::string::npos
                  && short_run.error.find("40") != std::string::npos,
              "bootstrap attempts: shortfall error reports the counts");
        check(short_run.ratings.empty(), "bootstrap attempts: no ratings on shortfall");
        check(short_run.diagnostics.bootstrap_requested == 40
                  && short_run.diagnostics.bootstrap_attempted == 40
                  && short_run.diagnostics.bootstrap_valid < 40,
              "bootstrap attempts: shortfall counts recorded in diagnostics");

        auto const recovered = tournament_rating::fit_with_bootstrap(records, {},
                                                                     {20, 2024, 0.025, 0.975, 4000});
        check(recovered.ok, "bootstrap attempts: retries bridge dropout until the quota is met");
        if (recovered.ok)
        {
            check(recovered.diagnostics.bootstrap_requested == 20
                      && recovered.diagnostics.bootstrap_valid == 20
                      && recovered.diagnostics.bootstrap_attempted > 20,
                  "bootstrap attempts: attempted exceeds valid under dropout");
            check(recovered.ratings.size() == 6,
                  "bootstrap attempts: all candidates rated after retries");
        }

        auto const bad_budget = tournament_rating::fit_with_bootstrap(records, {},
                                                                      {10, 2024, 0.025, 0.975, 5});
        check(!bad_budget.ok && bad_budget.error.find("max_attempts") != std::string::npos,
              "bootstrap attempts: max_attempts below samples is rejected");
    }

    void test_invalid_inputs()
    {
        auto const empty = tournament_rating::fit({});
        check(!empty.ok && !empty.error.empty(), "invalid: empty input rejected");

        std::vector<tournament_rating::GameRecord> self_game{{5, 5, 1.0, 0}};
        auto const self_result = tournament_rating::fit(self_game);
        check(!self_result.ok && self_result.error.find("record 0") != std::string::npos,
              "invalid: self play rejected with record index");

        std::vector<tournament_rating::GameRecord> bad_score{{1, 2, 0.3, 0}};
        auto const score_result = tournament_rating::fit(bad_score);
        check(!score_result.ok && score_result.error.find("score_for_a") != std::string::npos,
              "invalid: non canonical score rejected");

        tournament_rating::Options zero_lambda;
        zero_lambda.l2_lambda = 0.0;
        check(!tournament_rating::fit({{1, 2, 1.0, 0}}, zero_lambda).ok,
              "invalid: zero lambda rejected");

        tournament_rating::Options nan_lambda;
        nan_lambda.l2_lambda = std::numeric_limits<double>::quiet_NaN();
        check(!tournament_rating::fit({{1, 2, 1.0, 0}}, nan_lambda).ok,
              "invalid: nan lambda rejected");

        tournament_rating::Options zero_iters;
        zero_iters.max_newton_iterations = 0;
        check(!tournament_rating::fit({{1, 2, 1.0, 0}}, zero_iters).ok,
              "invalid: zero newton iterations rejected");

        auto const zero_samples = tournament_rating::fit_with_bootstrap({{1, 2, 1.0, 0}}, {},
                                                                        {0, 1, 0.025, 0.975});
        check(!zero_samples.ok && zero_samples.error.find("samples") != std::string::npos,
              "invalid: zero bootstrap samples rejected");

        auto const bad_quantiles = tournament_rating::fit_with_bootstrap({{1, 2, 1.0, 0}}, {},
                                                                         {10, 1, 0.5, 0.5});
        check(!bad_quantiles.ok, "invalid: equal bootstrap quantiles rejected");
    }

    void test_sparse_graph()
    {
        std::vector<tournament_rating::GameRecord> records;
        add_games(records, 9, 10, 60, 20, 0, 1);
        add_games(records, 9, 11, 80, 10, 0, 1000);
        add_games(records, 9, 12, 50, 30, 0, 2000);
        add_games(records, 9, 13, 90, 5, 0, 3000);
        auto const result = tournament_rating::fit(records);
        check(result.ok, "sparse graph: star schedule fits");
        if (!result.ok)
        {
            return;
        }
        check(result.ratings[0].candidate == 9, "sparse graph: dominant center ranks first");
        double min_other = result.ratings[1].std_error;
        for (size_t k = 2; k < result.ratings.size(); ++k)
        {
            min_other = std::min(min_other, result.ratings[k].std_error);
        }
        check(result.ratings[0].std_error < min_other,
              "sparse graph: best sampled player has smallest standard error");
    }

    void test_uncertainty_scaling()
    {
        std::vector<tournament_rating::GameRecord> records;
        for (std::uint64_t opponent : {1, 2, 3})
        {
            add_games(records, 100, opponent, 150, 100, 0, 1000 * opponent);
            add_games(records, 200, opponent, 30, 20, 0, 1000 * opponent + 500);
        }
        auto const result = tournament_rating::fit(records);
        check(result.ok, "uncertainty scaling: fit succeeds");
        if (!result.ok)
        {
            return;
        }
        double se_heavy = 0.0;
        double se_light = 0.0;
        double rating_heavy = 0.0;
        double rating_light = 0.0;
        for (auto const &entry : result.ratings)
        {
            if (entry.candidate == 100)
            {
                se_heavy = entry.std_error;
                rating_heavy = entry.rating;
            }
            if (entry.candidate == 200)
            {
                se_light = entry.std_error;
                rating_light = entry.rating;
            }
        }
        check(se_heavy > 0.0 && se_light > 0.0 && se_heavy < 0.8 * se_light,
              "uncertainty scaling: fivefold games shrink the standard error");
        check(std::fabs(rating_heavy - rating_light) < 0.1,
              "uncertainty scaling: equal win rates give equal strength");
    }

    void test_regularization()
    {
        std::vector<tournament_rating::GameRecord> records;
        add_games(records, 1, 2, 12, 0, 0, 1);
        auto const weak = tournament_rating::fit(records);
        tournament_rating::Options strong;
        strong.l2_lambda = 1e6;
        auto const shrunk = tournament_rating::fit(records, strong);
        check(weak.ok && shrunk.ok, "regularization: fits succeed");
        if (!weak.ok || !shrunk.ok)
        {
            return;
        }
        check(std::fabs(shrunk.ratings[0].rating) < 1e-3 && std::fabs(shrunk.ratings[1].rating) < 1e-3,
              "regularization: strong prior pulls ratings toward zero");
        check(weak.ratings[0].rating - weak.ratings[1].rating > 1.0,
              "regularization: weak prior keeps a clean sweep decisive");
    }
}

int main()
{
    test_equal_players();
    test_known_strength();
    test_ties();
    test_disconnected();
    test_reordering();
    test_bootstrap();
    test_se_solve_failure();
    test_convergence_floor();
    test_bootstrap_attempts();
    test_invalid_inputs();
    test_sparse_graph();
    test_uncertainty_scaling();
    test_regularization();
    if (failures == 0)
    {
        std::println("ALL RATING TESTS PASSED");
        return 0;
    }
    std::println("{} RATING TEST(S) FAILED", failures);
    return 1;
}
