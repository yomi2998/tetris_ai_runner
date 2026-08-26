
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <numbers>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "tuner_match.h"
#include "tournament_optimizer.h"

namespace tournament_opt {

static uint64_t splitmix64(uint64_t x)
{
    return tuner_match::splitmix64(x);
}

static int rademacher(uint64_t seed, int k, int j, int i)
{
    return tuner_match::rademacher(seed, k, j, i);
}


static double const GLICKO_Q = std::log(10.0) / 400.0;
static double const GLICKO_SIGMA_MIN = 40.0;
static double const GLICKO_SIGMA_MAX = 350.0;

static double glicko_g(double sigma_opp)
{
    return 1.0 / std::sqrt(1.0 + 3.0 * GLICKO_Q * GLICKO_Q * sigma_opp * sigma_opp
                           / (std::numbers::pi * std::numbers::pi));
}

static double glicko_expected(double mu, double mu_opp, double g_opp)
{
    return 1.0 / (1.0 + std::pow(10.0, -g_opp * (mu - mu_opp) / 400.0));
}


std::vector<Candidate> init_population(ParameterVector const &mean_theta, size_t n, uint64_t seed)
{
    std::vector<Candidate> cands;
    cands.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        Candidate c;
        c.id = static_cast<uint64_t>(i);
        c.rating = Rating{};
        c.archived = false;
        for (size_t d = 0; d < NUM_PARAMS; ++d)
        {
            int eps = rademacher(seed, 0, static_cast<int>(i), static_cast<int>(d));
            c.theta[d] = mean_theta[d] + static_cast<double>(eps) * param_scale[d];
        }
        cands.push_back(c);
    }
    return cands;
}

std::vector<Comparison> schedule_common_anchor_7x2(
    std::vector<Candidate> const &cands,
    ParameterVector const &anchor_theta,
    uint64_t seed)
{
    (void)anchor_theta;
    std::vector<Comparison> out;
    out.reserve(14);
    if (cands.size() != 7)
    {
        return out;
    }
    for (size_t j = 0; j < 7; ++j)
    {
        uint64_t scenario_seed = splitmix64(seed
            ^ splitmix64(static_cast<uint64_t>(j) * 0x94D049BB133111EBULL));
        uint64_t scenario_seed_a = splitmix64(scenario_seed);
        uint64_t scenario_seed_b = splitmix64(scenario_seed ^ 0x9E3779B97F4A7C15ULL);

        Comparison a;
        a.a_id = cands[j].id;
        a.b_id = ANCHOR_ID;
        a.scenario_seed_p1 = scenario_seed_a;
        a.scenario_seed_p2 = scenario_seed_b;
        Comparison b;
        b.a_id = ANCHOR_ID;
        b.b_id = cands[j].id;
        b.scenario_seed_p1 = scenario_seed_a;
        b.scenario_seed_p2 = scenario_seed_b;
        out.push_back(a);
        out.push_back(b);
    }
    return out;
}

std::vector<Comparison> schedule_screen8_race14(
    std::vector<Candidate> const &cands,
    ParameterVector const &seed_thetas,
    uint64_t seed)
{
    (void)seed_thetas;
    std::vector<Comparison> out;
    out.reserve(14);
    if (cands.size() != 8)
    {
        return out;
    }
    ScreenTournament st(cands, seed);
    while (!st.finished())
    {
        auto games = st.next_games();
        std::vector<Outcome> results;
        results.reserve(games.size());
        for (auto const &g : games)
        {
            results.push_back({ g.a_id, g.b_id, +1.0 });
        }
        st.report_outcomes(results);
        for (auto const &g : games)
        {
            out.push_back(g);
        }
    }
    for (auto const &g : st.tail_games(cands))
    {
        out.push_back(g);
    }
    return out;
}


static uint64_t screen_seed_tag(uint64_t seed, uint64_t tag)
{
    return splitmix64(seed ^ splitmix64(tag * 0x9E3779B97F4A7C15ULL));
}

ScreenTournament::ScreenTournament(std::vector<Candidate> const &cands, uint64_t seed)
    : base_seed_(seed)
{
    std::vector<uint64_t> ids;
    for (auto const &c : cands) ids.push_back(c.id);
    std::sort(ids.begin(), ids.end());
    for (size_t i = 0; i < 8 && i < ids.size(); ++i)
    {
        order_[i] = ids[i];
        for (auto const &c : cands)
        {
            if (c.id == ids[i])
            {
                score_[i] = conservative_score(c);
                break;
            }
        }
    }
}

uint64_t ScreenTournament::resolve(std::vector<Candidate> const *cands, Outcome const &o) const
{
    if (o.result_a > 0) return o.a_id;
    if (o.result_a < 0) return o.b_id;
    auto s = [&](uint64_t id) -> double
    {
        if (cands)
        {
            for (auto const &c : *cands)
            {
                if (c.id == id) return conservative_score(c);
            }
        }
        for (size_t i = 0; i < 8; ++i)
        {
            if (order_[i] == id) return score_[i];
        }
        return -1e300;
    };
    double sa = s(o.a_id), sb = s(o.b_id);
    if (sa != sb) return sa > sb ? o.a_id : o.b_id;
    return std::min(o.a_id, o.b_id);
}

std::vector<Comparison> ScreenTournament::next_games()
{
    std::vector<Comparison> out;
    if (round_ > 3)
    {
        return out;
    }
    uint64_t rseed = screen_seed_tag(base_seed_, static_cast<uint64_t>(round_));
    auto one = [&](uint64_t a, uint64_t b, int g) -> Comparison
    {
        Comparison c;
        uint64_t sseed = splitmix64(rseed ^ splitmix64(static_cast<uint64_t>(g) * 0x94D049BB133111EBULL));
        uint64_t sa = splitmix64(sseed);
        uint64_t sb = splitmix64(sseed ^ 0x9E3779B97F4A7C15ULL);
        uint64_t h = splitmix64(sseed ^ splitmix64(a * 0x9E3779B97F4A7C15ULL + b));
        if (h & 1ULL) std::swap(a, b);
        c.a_id = a;
        c.b_id = b;
        c.scenario_seed_p1 = sa;
        c.scenario_seed_p2 = sb;
        return c;
    };
    if (round_ == 1)
    {
        for (int g = 0; g < 4; ++g) out.push_back(one(order_[2 * g], order_[2 * g + 1], g));
    }
    else if (round_ == 2)
    {
        for (int g = 0; g < 2; ++g) out.push_back(one(r1_wins_[2 * g], r1_wins_[2 * g + 1], g));
    }
    else if (round_ == 3)
    {
        out.push_back(one(r2_wins_[0], r2_wins_[1], 0));
    }
    started_ = true;
    return out;
}

void ScreenTournament::report_outcomes(std::vector<Outcome> const &results)
{
    if (round_ == 1 && results.size() >= 4)
    {
        for (int g = 0; g < 4; ++g) r1_wins_[g] = resolve(nullptr, results[g]);
    }
    else if (round_ == 2 && results.size() >= 2)
    {
        for (int g = 0; g < 2; ++g)
        {
            r2_wins_[g] = resolve(nullptr, results[g]);
            r2_loses_[g] = (r2_wins_[g] == results[g].a_id) ? results[g].b_id : results[g].a_id;
        }
    }
    else if (round_ == 3 && !results.empty())
    {
        final_winner_ = resolve(nullptr, results[0]);
        if (final_winner_ == results[0].a_id) final_loser_ = results[0].b_id;
        else final_loser_ = results[0].a_id;
    }
    ++round_;
}

std::vector<Comparison> ScreenTournament::tail_games(std::vector<Candidate> const &cands)
{
    std::vector<Comparison> out;
    if (!finished())
    {
        return out;
    }
    uint64_t r4_seed = screen_seed_tag(base_seed_, 4ULL);
    std::array<std::pair<uint64_t, uint64_t>, 2> pairs = {
        std::pair{ final_winner_, final_loser_ },
        std::pair{ r2_loses_[0], r2_loses_[1] },
    };
    for (int p = 0; p < 2; ++p)
    {
        uint64_t a = pairs[p].first, b = pairs[p].second;
        uint64_t base = splitmix64(r4_seed ^ splitmix64(static_cast<uint64_t>(p) * 0x94D049BB133111EBULL));
        uint64_t sa = splitmix64(base);
        uint64_t sb = splitmix64(base ^ 0x9E3779B97F4A7C15ULL);
        Comparison c1;
        c1.a_id = a; c1.b_id = b;
        c1.scenario_seed_p1 = sa;
        c1.scenario_seed_p2 = sb;
        Comparison c2;
        c2.a_id = b; c2.b_id = a;
        c2.scenario_seed_p1 = sa;
        c2.scenario_seed_p2 = sb;
        out.push_back(c1);
        out.push_back(c2);
    }

    uint64_t r5_seed = screen_seed_tag(base_seed_, 5ULL);
    std::array<uint64_t, 4> top4 = { final_winner_, final_loser_, r2_loses_[0], r2_loses_[1] };
    std::vector<std::pair<uint64_t, uint64_t>> edges;
    for (int i = 0; i < 4; ++i)
    {
        for (int j = i + 1; j < 4; ++j)
        {
            edges.push_back({ top4[i], top4[j] });
        }
    }
    auto sigma_of = [&](uint64_t id) -> double
    {
        for (auto const &c : cands)
        {
            if (c.id == id) return c.rating.sigma;
        }
        return GLICKO_SIGMA_MAX;
    };
    std::pair<uint64_t, uint64_t> best_edge = edges.front();
    double best_unc = -1.0;
    for (auto const &e : edges)
    {
        double unc = sigma_of(e.first) + sigma_of(e.second);
        auto key = [](std::pair<uint64_t, uint64_t> const &x)
        {
            return std::pair<uint64_t, uint64_t>(std::min(x.first, x.second),
                                                 std::max(x.first, x.second));
        };
        bool better = unc > best_unc || (unc == best_unc && key(e) < key(best_edge));
        if (better)
        {
            best_unc = unc;
            best_edge = e;
        }
    }
    for (int g = 0; g < 3; ++g)
    {
        uint64_t sseed = splitmix64(r5_seed ^ splitmix64(static_cast<uint64_t>(g) * 0x94D049BB133111EBULL));
        Comparison c;
        uint64_t sa = splitmix64(sseed);
        uint64_t sb = splitmix64(sseed ^ 0x9E3779B97F4A7C15ULL);
        uint64_t a = best_edge.first, b = best_edge.second;
        uint64_t h = splitmix64(sseed ^ splitmix64(a * 0x9E3779B97F4A7C15ULL + b));
        if (h & 1ULL) std::swap(a, b);
        c.a_id = a;
        c.b_id = b;
        c.scenario_seed_p1 = sa;
        c.scenario_seed_p2 = sb;
        out.push_back(c);
    }
    return out;
}

JobPlan comparisons_to_jobs(
    std::vector<Comparison> const &comparisons,
    std::vector<Candidate> const &cands,
    ParameterVector const &anchor_theta,
    int budget_iters, int budget_ms)
{
    std::map<uint64_t, std::vector<double>> param_of;
    for (auto const &c : cands)
    {
        param_of[c.id] = std::vector<double>(c.theta, c.theta + NUM_PARAMS);
    }
    auto theta_of = [&](uint64_t id) -> std::vector<double>
    {
        if (id == ANCHOR_ID)
        {
            return std::vector<double>(anchor_theta.begin(), anchor_theta.end());
        }
        auto it = param_of.find(id);
        return it != param_of.end() ? it->second : std::vector<double>();
    };

    JobPlan plan;
    plan.jobs.reserve(comparisons.size());
    plan.a_ids.reserve(comparisons.size());
    plan.b_ids.reserve(comparisons.size());
    for (auto const &cmp : comparisons)
    {
        auto ta = theta_of(cmp.a_id);
        auto tb = theta_of(cmp.b_id);
        if (ta.size() != NUM_PARAMS || tb.size() != NUM_PARAMS)
        {
            continue;
        }
        tuner_match::MatchJob job{};
        std::memcpy(job.p1, ta.data(), NUM_PARAMS * sizeof(double));
        std::memcpy(job.p2, tb.data(), NUM_PARAMS * sizeof(double));
        job.scenario_seed_p1 = cmp.scenario_seed_p1;
        job.scenario_seed_p2 = cmp.scenario_seed_p2;
        job.job_id = plan.jobs.size();
        job.budget_iters_p1 = budget_iters;
        job.budget_iters_p2 = budget_iters;
        job.budget_ms_p1 = budget_ms;
        job.budget_ms_p2 = budget_ms;
        plan.jobs.push_back(job);
        plan.a_ids.push_back(cmp.a_id);
        plan.b_ids.push_back(cmp.b_id);
    }
    return plan;
}

std::vector<Outcome> outcomes_to_results(
    std::vector<tuner_match::MatchOutcome> const &outs,
    JobPlan const &plan)
{
    std::vector<Outcome> res;
    res.reserve(outs.size());
    for (size_t i = 0; i < outs.size() && i < plan.a_ids.size(); ++i)
    {
        Outcome o;
        o.a_id = plan.a_ids[i];
        o.b_id = plan.b_ids[i];
        o.result_a = static_cast<double>(outs[i].winner);
        res.push_back(o);
    }
    return res;
}


void update_ratings_generation(
    std::vector<Candidate> &cands,
    std::vector<Outcome> const &outcomes)
{
    auto index_of = [&](uint64_t id) -> size_t
    {
        for (size_t i = 0; i < cands.size(); ++i)
        {
            if (cands[i].id == id)
            {
                return i;
            }
        }
        return cands.size();
    };

    std::vector<double> mu0(cands.size());
    std::vector<double> sigma0(cands.size());
    for (size_t i = 0; i < cands.size(); ++i)
    {
        mu0[i] = cands[i].rating.mu;
        sigma0[i] = cands[i].rating.sigma;
    }

    std::vector<double> sum_term(cands.size(), 0.0);
    std::vector<double> inv_d2(cands.size(), 0.0);
    std::vector<uint64_t> games_played(cands.size(), 0);

    for (auto const &o : outcomes)
    {
        size_t ia = index_of(o.a_id);
        size_t ib = index_of(o.b_id);
        if (ia >= cands.size() && ib >= cands.size())
        {
            continue;
        }
        Rating ext{};
        double s_a = 0.5 * (o.result_a + 1.0);
        double s_b = 1.0 - s_a;

        double mu_a = ia < cands.size() ? mu0[ia] : ext.mu;
        double sigma_a = ia < cands.size() ? sigma0[ia] : ext.sigma;
        double mu_b = ib < cands.size() ? mu0[ib] : ext.mu;
        double sigma_b = ib < cands.size() ? sigma0[ib] : ext.sigma;

        double ga = glicko_g(sigma_b);
        double gb = glicko_g(sigma_a);
        double Ea = glicko_expected(mu_a, mu_b, ga);
        double Eb = glicko_expected(mu_b, mu_a, gb);

        if (ia < cands.size())
        {
            sum_term[ia] += ga * (s_a - Ea);
            double d2a = 1.0 / (GLICKO_Q * GLICKO_Q * ga * ga * Ea * (1.0 - Ea));
            inv_d2[ia] += 1.0 / d2a;
            ++games_played[ia];
        }
        if (ib < cands.size())
        {
            sum_term[ib] += gb * (s_b - Eb);
            double d2b = 1.0 / (GLICKO_Q * GLICKO_Q * gb * gb * Eb * (1.0 - Eb));
            inv_d2[ib] += 1.0 / d2b;
            ++games_played[ib];
        }
    }

    for (size_t i = 0; i < cands.size(); ++i)
    {
        if (games_played[i] == 0)
        {
            continue;
        }
        double inv_sigma2 = 1.0 / (sigma0[i] * sigma0[i]);
        double denom = inv_sigma2 + inv_d2[i];
        double new_mu = mu0[i] + (GLICKO_Q / denom) * sum_term[i];
        double new_sigma = std::sqrt(1.0 / denom);
        new_sigma = std::max(GLICKO_SIGMA_MIN, std::min(GLICKO_SIGMA_MAX, new_sigma));
        cands[i].rating.mu = new_mu;
        cands[i].rating.sigma = new_sigma;
        cands[i].rating.games += games_played[i];
    }
}

using SelectionBoundary = std::pair<uint64_t, uint64_t>;

static double const CONSERVATIVE_FACTOR = 1.0;

double conservative_score(Candidate const &c)
{
    return c.rating.mu - CONSERVATIVE_FACTOR * c.rating.sigma;
}


std::vector<Candidate> select_survivors(std::vector<Candidate> const &cands, size_t k)
{
    std::vector<Candidate> sorted = cands;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](Candidate const &a, Candidate const &b)
                     {
                         double sa = conservative_score(a);
                         double sb = conservative_score(b);
                         if (sa != sb)
                         {
                             return sa > sb;
                         }
                         return a.id < b.id;
                     });
    if (k > sorted.size())
    {
        k = sorted.size();
    }
    sorted.resize(k);
    return sorted;
}


double expected_score(Rating const &a, Rating const &b)
{
    double g = glicko_g(b.sigma);
    return glicko_expected(a.mu, b.mu, g);
}

uint64_t next_scenario_seed(uint64_t seed, uint64_t generation)
{
    return splitmix64(seed ^ splitmix64(generation * 0x9E3779B97F4A7C15ULL));
}

static double normalized_distance(Candidate const &a, Candidate const &b)
{
    double sum = 0.0;
    for (size_t d = 0; d < NUM_PARAMS; ++d)
    {
        double diff = (a.theta[d] - b.theta[d]) / param_scale[d];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

void maybe_archive(std::vector<Candidate> &pool, Candidate const &c)
{
    for (auto const &p : pool)
    {
        if (normalized_distance(p, c) < 0.2)
        {
            return;
        }
    }
    bool differs_from_all = true;
    for (auto const &p : pool)
    {
        double dist = normalized_distance(p, c);
        bool differs = dist > 0.5;
        if (!differs && p.rating.games >= 8 && c.rating.games >= 8)
        {
            double e = expected_score(p.rating, c.rating);
            differs = (e > 0.60 || e < 0.40);
        }
        if (!differs)
        {
            differs_from_all = false;
            break;
        }
    }
    if (differs_from_all)
    {
        Candidate copy = c;
        copy.archived = true;
        pool.push_back(copy);
    }
}

std::vector<Candidate> update_population_survivor_reseed(
    std::vector<Candidate> const &cands,
    size_t survivors_kept,
    uint64_t seed)
{
    if (cands.empty() || survivors_kept == 0 || survivors_kept > cands.size())
    {
        return cands;
    }
    auto top = select_survivors(cands, survivors_kept);
    std::vector<Candidate> next = top;
    std::vector<uint64_t> top_ids;
    for (auto const &c : top) top_ids.push_back(c.id);

    uint64_t max_id = 0;
    for (auto const &c : cands) max_id = std::max(max_id, c.id);
    size_t slot = 0;
    while (next.size() < cands.size())
    {
        Candidate const &parent = top[slot % top.size()];
        ++slot;
        int const sgn = (slot % 2 == 0) ? +1 : -1;
        uint64_t const new_id = ++max_id;
        Candidate child;
        child.id = new_id;
        child.rating = Rating{};
        child.archived = false;
        int const j = static_cast<int>(new_id ^ 0x5DEECE66DULL);
        for (size_t d = 0; d < NUM_PARAMS; ++d)
        {
            int eps = rademacher(seed, 0, j, static_cast<int>(d));
            double xn = parent.theta[d] / param_scale[d];
            child.theta[d] = (xn + sgn * eps * 0.30) * param_scale[d];
        }
        next.push_back(child);
    }
    (void)top_ids;
    return next;
}

std::vector<Candidate> update_population_pso(
    std::vector<Candidate> const &cands,
    PsoState &state,
    Candidate const &global_best,
    uint64_t seed)
{
    std::vector<Candidate> next;
    next.reserve(cands.size());
    for (auto const &c : cands)
    {
        bool have_v = state.velocity.contains(c.id);
        std::array<double, NUM_PARAMS> v{};
        std::array<double, NUM_PARAMS> pb{};
        if (have_v)
        {
            v = state.velocity[c.id];
            pb = state.best_theta[c.id];
        }
        else
        {
            for (size_t d = 0; d < NUM_PARAMS; ++d)
            {
                pb[d] = c.theta[d] / param_scale[d];
                int eps = rademacher(seed, 1, static_cast<int>(c.id), static_cast<int>(d));
                v[d] = 0.05 * eps;
            }
        }

        double cur_score = conservative_score(c);
        auto bs = state.best_score.find(c.id);
        if (bs == state.best_score.end() || cur_score > bs->second)
        {
            state.best_score[c.id] = cur_score;
            for (size_t d = 0; d < NUM_PARAMS; ++d)
            {
                pb[d] = c.theta[d] / param_scale[d];
            }
        }

        Candidate nc = c;
        std::array<double, NUM_PARAMS> xn{};
        for (size_t d = 0; d < NUM_PARAMS; ++d)
        {
            double x = nc.theta[d] / param_scale[d];
            double rp = rademacher(seed, 2, static_cast<int>(c.id), static_cast<int>(d));
            double rg = rademacher(seed, 3, static_cast<int>(c.id), static_cast<int>(d));
            double const w = 0.729, cp = 1.494, cg = 1.494;
            double const rp01 = 0.5 * (rp + 1.0);
            double const rg01 = 0.5 * (rg + 1.0);
            double pbest = pb[d];
            double gbest = global_best.theta[d] / param_scale[d];
            v[d] = w * v[d] + cp * rp01 * (pbest - x) + cg * rg01 * (gbest - x);
            xn[d] = x + v[d];
            nc.theta[d] = xn[d] * param_scale[d];
        }
        state.velocity[c.id] = v;
        state.best_theta[c.id] = pb;
        next.push_back(nc);
    }
    return next;
}

}

#ifdef TOURNAMENT_OPT_SELF_CHECK

using namespace tournament_opt;

static int g_failures = 0;

static void check(bool cond, char const *name)
{
    if (cond)
    {
        std::println("PASS: {}", name);
    }
    else
    {
        std::println("FAIL: {}", name);
        ++g_failures;
    }
}

int main()
{
    std::setbuf(stdout, nullptr);

    ParameterVector mean_theta;
    for (size_t d = 0; d < NUM_PARAMS; ++d)
    {
        mean_theta[d] = 10.0 * param_scale[d];
    }

    {
        auto p1 = init_population(mean_theta, 8, 12345ULL);
        auto p2 = init_population(mean_theta, 8, 12345ULL);
        bool same = p1.size() == p2.size();
        if (same)
        {
            for (size_t i = 0; i < p1.size(); ++i)
            {
                if (p1[i].id != p2[i].id
                    || std::memcmp(p1[i].theta, p2[i].theta, sizeof(p1[i].theta)) != 0)
                {
                    same = false;
                    break;
                }
            }
        }
        check(same, "init_population deterministic (byte-identical)");
    }

    {
        auto cands = init_population(mean_theta, 7, 777ULL);
        auto s1 = schedule_common_anchor_7x2(cands, mean_theta, 999ULL);
        auto s2 = schedule_common_anchor_7x2(cands, mean_theta, 999ULL);
        bool same = s1.size() == s2.size() && s1.size() == 14;
        if (same)
        {
            for (size_t i = 0; i < s1.size(); ++i)
            {
                if (s1[i].a_id != s2[i].a_id || s1[i].b_id != s2[i].b_id
                    || s1[i].scenario_seed_p1 != s2[i].scenario_seed_p1
                    || s1[i].scenario_seed_p2 != s2[i].scenario_seed_p2)
                {
                    same = false;
                    break;
                }
            }
        }
        check(same, "schedule_common_anchor_7x2 deterministic, 14 games");
    }

    {
        auto cands = init_population(mean_theta, 8, 4242ULL);
        auto s1 = schedule_screen8_race14(cands, mean_theta, 31337ULL);
        auto s2 = schedule_screen8_race14(cands, mean_theta, 31337ULL);
        bool same = s1.size() == s2.size() && s1.size() == 14;
        if (same)
        {
            for (size_t i = 0; i < s1.size(); ++i)
            {
                if (s1[i].a_id != s2[i].a_id || s1[i].b_id != s2[i].b_id
                    || s1[i].scenario_seed_p1 != s2[i].scenario_seed_p1
                    || s1[i].scenario_seed_p2 != s2[i].scenario_seed_p2)
                {
                    same = false;
                    break;
                }
            }
        }
        check(same, "schedule_screen8_race14 deterministic, 14 games");

        bool structure_ok = s1.size() == 14;
        if (structure_ok)
        {
            std::array<int, 8> appear = {};
            for (int g = 0; g < 4; ++g)
            {
                if (s1[g].a_id < 8) appear[s1[g].a_id]++;
                if (s1[g].b_id < 8) appear[s1[g].b_id]++;
            }
            for (int i = 0; i < 8; ++i)
            {
                if (appear[i] != 1)
                {
                    structure_ok = false;
                }
            }
            for (int g = 4; g < 7; ++g)
            {
                if (s1[g].a_id >= 8 || s1[g].b_id >= 8)
                {
                    structure_ok = false;
                }
            }
        }
        check(structure_ok, "screen8_race14: 3-round bracket structure valid");

        bool tail_ok = s1.size() == 14;
        if (tail_ok)
        {
            if (s1[7].a_id != s1[8].b_id || s1[7].b_id != s1[8].a_id
                || s1[9].a_id != s1[10].b_id || s1[9].b_id != s1[10].a_id)
            {
                tail_ok = false;
            }
        }
        check(tail_ok, "screen8_race14: 4 boundary + 3 adaptive tail");
    }

    {
        auto cands = init_population(mean_theta, 8, 5150ULL);
        cands[0].rating.mu = 1600.0;
        cands[0].rating.sigma = 200.0;
        cands[1].rating.mu = 1500.0;
        cands[1].rating.sigma = 200.0;

        std::vector<Outcome> outcomes;
        outcomes.push_back({ 0, 1, +1.0 });
        outcomes.push_back({ 0, 1, +1.0 });
        outcomes.push_back({ 2, 3, 0.0 });
        outcomes.push_back({ 1, 0, +1.0 });

        uint64_t g0 = cands[0].rating.games;
        uint64_t g1 = cands[1].rating.games;
        double mu0_before = cands[0].rating.mu;
        double mu1_before = cands[1].rating.mu;

        update_ratings_generation(cands, outcomes);

        check(cands[0].rating.games == g0 + 3, "games counter: candidate 0 +3");
        check(cands[1].rating.games == g1 + 3, "games counter: candidate 1 +3");

        bool finite = true;
        for (auto const &c : cands)
        {
            if (!std::isfinite(c.rating.mu) || !std::isfinite(c.rating.sigma))
            {
                finite = false;
            }
        }
        check(finite, "ratings finite");

        bool clamp_ok = true;
        for (auto const &c : cands)
        {
            if (c.rating.sigma < GLICKO_SIGMA_MIN - 1e-9 || c.rating.sigma > GLICKO_SIGMA_MAX + 1e-9)
            {
                clamp_ok = false;
            }
        }
        check(clamp_ok, "sigma within [40, 350]");

        check(cands[0].rating.mu > mu0_before, "2:0 winner gains mu");
        check(cands[1].rating.mu < mu1_before, "0:2 loser loses mu");

        double eab = expected_score(cands[0].rating, cands[1].rating);
        double eba = expected_score(cands[1].rating, cands[0].rating);
        check(eab + eba == 1.0, "expected_score(a,b)+expected_score(b,a)==1 exactly");
    }

    {
        auto cands = init_population(mean_theta, 4, 9910ULL);
        uint64_t g_before = cands[0].rating.games;
        double mu_before = cands[0].rating.mu;
        std::vector<Outcome> outcomes;
        outcomes.push_back({ cands[0].id, ANCHOR_ID, +1.0 });
        outcomes.push_back({ ANCHOR_ID, cands[0].id, +1.0 });
        update_ratings_generation(cands, outcomes);
        bool updated = cands[0].rating.games == g_before + 2
            && std::isfinite(cands[0].rating.mu)
            && cands[0].rating.sigma <= GLICKO_SIGMA_MAX;
        check(updated, "anchor/external-id outcomes update the known endpoint");
        (void)mu_before;
    }

    {
        auto cands = init_population(mean_theta, 8, 1313ULL);
        for (auto &c : cands) { c.rating.sigma = 120.0; }
        cands[4].rating.mu = 1700.0;
        ScreenTournament st(cands, 4242ULL);

        std::vector<Outcome> all;
        int round1_games = 0;
        while (!st.finished())
        {
            auto round = st.next_games();
            std::vector<Outcome> r;
            for (size_t i = 0; i < round.size(); ++i)
            {
                Outcome o;
                o.a_id = round[i].a_id;
                o.b_id = round[i].b_id;
                if (st.finished() || round.size() == 1)
                {
                    o.result_a = +1.0;
                }
                else if (round.size() == 4)
                {
                    if (round[i].a_id == cands[4].id || round[i].b_id == cands[4].id)
                    {
                        o.result_a = (round[i].a_id == cands[4].id) ? -1.0 : +1.0;
                    }
                    else if (i == 2)
                    {
                        o.result_a = 0.0;
                    }
                    else
                    {
                        o.result_a = +1.0;
                    }
                }
                else
                {
                    o.result_a = +1.0;
                }
                r.push_back(o);
            }
            if (round.size() == 4) round1_games = static_cast<int>(round.size());
            st.report_outcomes(r);
            all.insert(all.end(), r.begin(), r.end());
        }
        auto tail = st.tail_games(cands);

        bool upset_respected = st.final_winner_ != cands[4].id;
        bool totals_14 = static_cast<int>(all.size()) + static_cast<int>(tail.size()) == 14
            && round1_games == 4;
        bool tail_ok = tail.size() == 7;
        bool boundary2_ok = tail.size() >= 4;
        if (boundary2_ok)
        {
            uint64_t const l0 = st.r2_loses_[0], l1 = st.r2_loses_[1];
            for (int g = 2; g <= 3; ++g)
            {
                uint64_t a = tail[g].a_id, b = tail[g].b_id;
                bool is_loser_pair = (a == l0 && b == l1) || (a == l1 && b == l0);
                bool not_finalist = a != st.final_winner_ && b != st.final_winner_
                    && a != st.final_loser_ && b != st.final_loser_;
                if (!is_loser_pair || !not_finalist)
                {
                    boundary2_ok = false;
                }
            }
        }
        check(upset_respected, "screen honors round-1 upset (no placeholder winner)");
        check(totals_14, "screen + tail total exactly 14 games");
        check(tail_ok, "screen tail is 4 boundary + 3 adaptive (7 games)");
        check(boundary2_ok, "boundary recheck pair 2 = semifinal losers (not finalists)");
    }

    {
        auto cands = init_population(mean_theta, 8, 6060ULL);
        for (size_t i = 0; i < cands.size(); ++i)
        {
            cands[i].rating.mu = 1500.0 - 10.0 * static_cast<double>(i);
            cands[i].rating.sigma = 100.0;
        }
        auto top = select_survivors(cands, 3);
        bool ok = top.size() == 3;
        if (ok)
        {
                if (top[0].id != 0 || top[1].id != 1 || top[2].id != 2)
            {
                ok = false;
            }
        }
        check(ok, "select_survivors returns top-3 deterministically");

        auto top2 = select_survivors(cands, 3);
        bool same = top.size() == top2.size();
        if (same)
        {
            for (size_t i = 0; i < top.size(); ++i)
            {
                if (top[i].id != top2[i].id)
                {
                    same = false;
                }
            }
        }
        check(same, "select_survivors deterministic");
    }

    {
        std::vector<Candidate> pool;
        auto cands = init_population(mean_theta, 2, 7070ULL);
        maybe_archive(pool, cands[0]);
        check(pool.size() == 1, "maybe_archive inserts into empty pool");

        Candidate near = cands[0];
        for (size_t d = 0; d < NUM_PARAMS; ++d)
        {
            near.theta[d] += 0.02 * param_scale[d];
        }
        near.id = 100;
        size_t before = pool.size();
        maybe_archive(pool, near);
        check(pool.size() == before, "maybe_archive rejects near-duplicate (<0.2)");

        Candidate far = cands[0];
        for (size_t d = 0; d < NUM_PARAMS; ++d)
        {
            far.theta[d] += 1.0 * param_scale[d];
        }
        far.id = 200;
        maybe_archive(pool, far);
        check(pool.size() == before + 1, "maybe_archive inserts far candidate (>0.5)");
    }

    {
        auto cands = init_population(mean_theta, 7, 8080ULL);
        ParameterVector anchor = mean_theta;
        auto sched = schedule_common_anchor_7x2(cands, anchor, 1234ULL);
        JobPlan plan = comparisons_to_jobs(sched, cands, anchor, 80, 0);

        bool ok = plan.jobs.size() == 14 && plan.jobs.size() == plan.a_ids.size()
            && plan.jobs.size() == plan.b_ids.size();
        if (ok)
        {
            for (size_t i = 0; i < plan.jobs.size() && i < sched.size(); ++i)
            {
                auto const &j = plan.jobs[i];
                bool a_ok = false;
                if (sched[i].a_id == ANCHOR_ID)
                {
                    a_ok = std::memcmp(j.p1, anchor.data(), NUM_PARAMS * sizeof(double)) == 0;
                }
                else
                {
                    for (auto const &c : cands)
                    {
                        if (c.id == sched[i].a_id
                            && std::memcmp(j.p1, c.theta, NUM_PARAMS * sizeof(double)) == 0)
                        {
                            a_ok = true;
                            break;
                        }
                    }
                }
                if (!a_ok || plan.a_ids[i] != sched[i].a_id || plan.b_ids[i] != sched[i].b_id
                    || j.scenario_seed_p1 != sched[i].scenario_seed_p1
                    || j.scenario_seed_p2 != sched[i].scenario_seed_p2
                    || j.budget_iters_p1 != 80)
                {
                    ok = false;
                    break;
                }
            }
        }
        check(ok, "comparisons_to_jobs attribute thetas/seeds/budgets faithfully");

        std::vector<tuner_match::MatchOutcome> outs(3);
        outs[0].winner = +1; outs[1].winner = -1; outs[2].winner = 0;
        JobPlan sub = { { plan.jobs[0], plan.jobs[1], plan.jobs[2] },
                        { plan.a_ids[0], plan.a_ids[1], plan.a_ids[2] },
                        { plan.b_ids[0], plan.b_ids[1], plan.b_ids[2] } };
        auto res = outcomes_to_results(outs, sub);
        bool conv = res.size() == 3 && res[0].result_a == 1.0
            && res[1].result_a == -1.0 && res[2].result_a == 0.0;
        check(conv, "outcomes_to_results converts +1/-1/0 correctly");
    }

    std::println("");
    if (g_failures == 0)
    {
        std::println("ALL CHECKS PASSED");
        return 0;
    }
    std::println("{} CHECK(S) FAILED", g_failures);
    return 1;
}

#endif
