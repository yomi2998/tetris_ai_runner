#include "tournament/promotion.h"

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
        std::println("{}: {}", condition ? "PASS" : "FAIL", name);
        if (!condition)
        {
            ++failures;
        }
    }

    enum class Mode
    {
        Strength,
        SeatA,
        Short,
        Reordered,
        InvalidWinner,
    };

    struct FakeBackend
    {
        Mode mode = Mode::Strength;
        mutable std::vector<tuning::BatchGame> seen;

        tuning::ParamSchema schema() const
        {
            static char const *const names[] = {"strength"};
            static double const scales[] = {1.0};
            static double const defaults[] = {0.0};
            return tuning::ParamSchema{
                .adapter_id = "promotion_fake",
                .names = names,
                .scales = scales,
                .defaults = defaults,
            };
        }

        bool validate(std::vector<double> const &theta) const
        {
            return theta.size() == 1 && std::isfinite(theta[0]);
        }

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const &games,
                                                    tuning::RunConfig const &config) const
        {
            seen = games;
            if (!tuning::valid_run_config(config))
            {
                return {};
            }
            std::vector<tuning::GameOutcome> outcomes;
            outcomes.reserve(games.size());
            for (tuning::BatchGame const &game : games)
            {
                tuning::GameOutcome outcome;
                outcome.id = game.id;
                if (mode == Mode::SeatA)
                {
                    outcome.winner = 1;
                }
                else if (mode == Mode::InvalidWinner)
                {
                    outcome.winner = 2;
                }
                else if (game.theta_a[0] > game.theta_b[0])
                {
                    outcome.winner = 1;
                }
                else if (game.theta_a[0] < game.theta_b[0])
                {
                    outcome.winner = -1;
                }
                else
                {
                    outcome.winner = 0;
                }
                outcomes.push_back(outcome);
            }
            if (mode == Mode::Short && !outcomes.empty())
            {
                outcomes.pop_back();
            }
            if (mode == Mode::Reordered && outcomes.size() >= 2)
            {
                std::swap(outcomes[0], outcomes[1]);
            }
            return outcomes;
        }
    };

    static_assert(tuning::MatchBackend<FakeBackend>);

    bool same_games(std::vector<tuning::BatchGame> const &a,
                    std::vector<tuning::BatchGame> const &b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].id != b[i].id || a[i].theta_a != b[i].theta_a
                || a[i].theta_b != b[i].theta_b || a[i].seed_a != b[i].seed_a
                || a[i].seed_b != b[i].seed_b)
            {
                return false;
            }
        }
        return true;
    }

    tournament_promotion::Options options()
    {
        tournament_promotion::Options value;
        value.pairs = 8;
        value.seed = 12345;
        value.first_game_id = 100;
        return value;
    }

    void test_strong_candidate()
    {
        FakeBackend backend;
        tuning::RunConfig config;
        auto const result = tournament_promotion::evaluate(
            backend, std::vector<double>{1.0}, std::vector<double>{0.0}, config, options());
        check(result.ok, "strong candidate evaluation succeeds");
        check(result.promoted, "strong candidate promotes");
        check(result.mean_score == 1.0 && result.lower_bound == 1.0,
              "strong candidate has a perfect paired interval");
        check(result.pairs == 8 && result.games == 16 && result.outcomes.size() == 16,
              "strong candidate reports every pair and game");
        bool mirrored = backend.seen.size() == 16;
        for (std::size_t i = 0; mirrored && i < backend.seen.size(); i += 2)
        {
            auto const &first = backend.seen[i];
            auto const &second = backend.seen[i + 1];
            mirrored = first.id + 1 == second.id
                && first.theta_a == second.theta_b
                && first.theta_b == second.theta_a
                && first.seed_a == second.seed_a
                && first.seed_b == second.seed_b;
        }
        check(mirrored, "challenge games are seat-swapped mirrored pairs");
    }

    void test_equal_and_seat_bias()
    {
        tuning::RunConfig config;
        FakeBackend equal;
        auto const tied = tournament_promotion::evaluate(
            equal, std::vector<double>{0.0}, std::vector<double>{0.0}, config, options());
        check(tied.ok && !tied.promoted && tied.mean_score == 0.5,
              "equal candidates do not promote");

        FakeBackend biased;
        biased.mode = Mode::SeatA;
        auto const neutral = tournament_promotion::evaluate(
            biased, std::vector<double>{1.0}, std::vector<double>{0.0}, config, options());
        check(neutral.ok && !neutral.promoted && neutral.mean_score == 0.5,
              "paired seat swaps cancel a pure seat advantage");
    }

    void test_determinism()
    {
        tuning::RunConfig config;
        FakeBackend first;
        FakeBackend second;
        auto const a = tournament_promotion::evaluate(
            first, std::vector<double>{1.0}, std::vector<double>{0.0}, config, options());
        auto const b = tournament_promotion::evaluate(
            second, std::vector<double>{1.0}, std::vector<double>{0.0}, config, options());
        check(a.ok && b.ok && same_games(first.seen, second.seen),
              "challenge construction is deterministic");
        bool separated = first.seen.size() >= 4
            && first.seen[0].seed_a != first.seen[0].seed_b
            && first.seen[0].seed_a != first.seen[2].seed_a;
        check(separated, "challenge seeds separate seats and pairs");
    }

    void test_invalid_options()
    {
        FakeBackend backend;
        tuning::RunConfig config;
        auto value = options();
        value.pairs = 1;
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0},
              std::vector<double>{0.0}, config, value).ok, "one pair is rejected");
        value = options();
        value.seed = 0;
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0},
              std::vector<double>{0.0}, config, value).ok, "zero seed is rejected");
        value = options();
        value.promotion_threshold = std::numeric_limits<double>::quiet_NaN();
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0},
              std::vector<double>{0.0}, config, value).ok, "non-finite threshold is rejected");
        value = options();
        value.first_game_id = std::numeric_limits<tuning::GameId>::max() - 2;
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0},
              std::vector<double>{0.0}, config, value).ok, "overflowing game ids are rejected");
        value = options();
        config.iterations_per_move = 0;
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0},
              std::vector<double>{0.0}, config, value).ok, "invalid run config is rejected");
        config = {};
        check(!tournament_promotion::evaluate(backend, std::vector<double>{1.0, 2.0},
              std::vector<double>{0.0}, config, value).ok, "invalid theta is rejected");
    }

    void test_malformed_backend_results()
    {
        tuning::RunConfig config;
        for (Mode mode : {Mode::Short, Mode::Reordered, Mode::InvalidWinner})
        {
            FakeBackend backend;
            backend.mode = mode;
            auto const result = tournament_promotion::evaluate(
                backend, std::vector<double>{1.0}, std::vector<double>{0.0}, config, options());
            check(!result.ok, "malformed backend result is rejected");
        }
    }
}

int main()
{
    test_strong_candidate();
    test_equal_and_seat_bias();
    test_determinism();
    test_invalid_options();
    test_malformed_backend_results();
    std::println("{} promotion test failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
