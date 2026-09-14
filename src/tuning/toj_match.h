#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "tuner_match.h"
#include "tuning/domain.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace tuning_toj
{
    class TojMatchBackend
    {
    public:
        using adapter_type = tuning_toj::TojAdapter;

        tuning::ParamSchema schema() const
        {
            return TojAdapter::schema();
        }

        bool validate(std::vector<double> const& theta) const
        {
            return tuning::validate_theta(TojAdapter::schema(), theta);
        }

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const& games, tuning::RunConfig config) const
        {
            std::vector<tuning::GameOutcome> results;
            if (games.empty() || !tuning::valid_run_config(config))
            {
                return results;
            }
            for (tuning::BatchGame const& game : games)
            {
                if (!validate(game.theta_a) || !validate(game.theta_b))
                {
                    return results;
                }
            }
            std::vector<tuner_match::MatchJob> jobs;
            jobs.reserve(games.size());
            for (tuning::BatchGame const& game : games)
            {
                tuner_match::MatchJob job{};
                copy_theta(game.theta_a, job.p1);
                copy_theta(game.theta_b, job.p2);
                job.scenario_seed_p1 = game.seed_a;
                job.scenario_seed_p2 = game.seed_b;
                job.job_id = static_cast<std::size_t>(game.id);
                job.budget_iters_p1 = static_cast<int>(config.iterations_per_move);
                job.budget_iters_p2 = static_cast<int>(config.iterations_per_move);
                jobs.push_back(job);
            }
            std::atomic<bool> view{false};
            std::atomic<std::uint32_t> view_index{0};
            std::mutex view_mutex;
            std::vector<tuner_match::MatchOutcome> const raw = tuner_match::run_batch(
                jobs, config.threads, static_cast<int>(config.iterations_per_move), 0,
                config.max_rounds, view, view_mutex, view_index);
            results.reserve(games.size());
            for (std::size_t i = 0; i < games.size() && i < raw.size(); ++i)
            {
                results.push_back(normalize(games[i].id, raw[i]));
            }
            return results;
        }

    private:
        static void copy_theta(std::vector<double> const& theta, double* out)
        {
            for (std::size_t i = 0; i < TojAdapter::param_count(); ++i)
            {
                out[i] = theta[i];
            }
        }

        static tuning::GameOutcome normalize(tuning::GameId id, tuner_match::MatchOutcome const& raw)
        {
            tuning::GameOutcome outcome;
            outcome.id = id;
            outcome.winner = raw.winner;
            outcome.dead_a = raw.dead1;
            outcome.dead_b = raw.dead2;
            outcome.capped = raw.capped;
            outcome.rounds = raw.rounds;
            outcome.app_a = raw.app1;
            outcome.app_b = raw.app2;
            outcome.apl_a = raw.apl1;
            outcome.apl_b = raw.apl2;
            outcome.reason = normalize_reason(raw.winner_reason);
            return outcome;
        }

        static tuning::WinReason normalize_reason(int reason)
        {
            using WR = tuner_match::MatchResult::WinnerReason;
            switch (static_cast<WR>(reason))
            {
            case WR::P1_SURVIVOR:
                return tuning::WinReason::ASurvivor;
            case WR::P2_SURVIVOR:
                return tuning::WinReason::BSurvivor;
            case WR::P1_CAP_APL:
                return tuning::WinReason::ACapApl;
            case WR::P2_CAP_APL:
                return tuning::WinReason::BCapApl;
            case WR::P1_BOTH_DEAD_APL:
                return tuning::WinReason::ABothDeadApl;
            case WR::P2_BOTH_DEAD_APL:
                return tuning::WinReason::BBothDeadApl;
            case WR::BOTH_DEAD_DRAW:
                return tuning::WinReason::BothDeadDraw;
            case WR::CAP_DRAW:
                return tuning::WinReason::CapDraw;
            default:
                return tuning::WinReason::Unknown;
            }
        }
    };

    static_assert(tuning::MatchBackend<TojMatchBackend>);
}
