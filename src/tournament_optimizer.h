#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "tuner_match.h"

namespace tournament_opt
{
    constexpr size_t NUM_PARAMS = tuner_match::NUM_PARAMS;

    inline double const (&param_scale)[NUM_PARAMS] = tuner_match::param_scale;

    struct Rating
    {
        double mu = 1500.0;
        double sigma = 350.0;
        uint64_t games = 0;
    };

    struct Candidate
    {
        uint64_t id;
        double theta[NUM_PARAMS];
        Rating rating;
        bool archived;
    };

    struct Comparison
    {
        uint64_t a_id;
        uint64_t b_id;
        uint64_t scenario_seed_p1;
        uint64_t scenario_seed_p2;
    };

    struct Outcome
    {
        uint64_t a_id;
        uint64_t b_id;
        double result_a;
    };

    using ParameterVector = std::array<double, NUM_PARAMS>;

    constexpr uint64_t ANCHOR_ID = UINT64_MAX;

    struct PsoState
    {
        std::map<uint64_t, std::array<double, NUM_PARAMS>> velocity;
        std::map<uint64_t, std::array<double, NUM_PARAMS>> best_theta;
        std::map<uint64_t, double> best_score;
    };

    struct JobPlan
    {
        std::vector<tuner_match::MatchJob> jobs;
        std::vector<uint64_t> a_ids;
        std::vector<uint64_t> b_ids;
    };

    struct ScreenTournament
    {
        std::array<uint64_t, 8> order_{};
        std::array<double, 8> score_{};
        std::array<uint64_t, 4> r1_wins_{};
        std::array<uint64_t, 2> r2_wins_{};
        std::array<uint64_t, 2> r2_loses_{};
        uint64_t final_winner_ = 0;
        uint64_t final_loser_ = 0;
        int round_ = 1;
        uint64_t base_seed_ = 0;
        bool started_ = false;

        ScreenTournament(std::vector<Candidate> const &cands, uint64_t seed);

        bool finished() const { return round_ == 4; }

        std::vector<Comparison> next_games();

        void report_outcomes(std::vector<Outcome> const &results);

        std::vector<Comparison> tail_games(std::vector<Candidate> const &cands);

    private:
        uint64_t resolve(std::vector<Candidate> const *cands, Outcome const &o) const;
    };

    std::vector<Candidate> init_population(ParameterVector const &mean_theta, size_t n, uint64_t seed);
    std::vector<Comparison> schedule_common_anchor_7x2(
        std::vector<Candidate> const &cands, ParameterVector const &anchor_theta, uint64_t seed);
    std::vector<Comparison> schedule_screen8_race14(
        std::vector<Candidate> const &cands, ParameterVector const &seed_thetas, uint64_t seed);

    void update_ratings_generation(std::vector<Candidate> &cands, std::vector<Outcome> const &outcomes);
    double expected_score(Rating const &a, Rating const &b);
    double conservative_score(Candidate const &c);
    std::vector<Candidate> select_survivors(std::vector<Candidate> const &cands, size_t k);
    uint64_t next_scenario_seed(uint64_t seed, uint64_t generation);
    void maybe_archive(std::vector<Candidate> &pool, Candidate const &c);

    JobPlan comparisons_to_jobs(
        std::vector<Comparison> const &comparisons,
        std::vector<Candidate> const &cands,
        ParameterVector const &anchor_theta,
        int budget_iters, int budget_ms);
    std::vector<Outcome> outcomes_to_results(
        std::vector<tuner_match::MatchOutcome> const &outs, JobPlan const &plan);

    std::vector<Candidate> update_population_survivor_reseed(
        std::vector<Candidate> const &cands, size_t survivors_kept, uint64_t seed);
    std::vector<Candidate> update_population_pso(
        std::vector<Candidate> const &cands, PsoState &state,
        Candidate const &global_best, uint64_t seed);
}
