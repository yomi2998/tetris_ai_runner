#include "profile_value_support.h"
#include "quality_seats.h"
#include "tuner_match.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include <unistd.h>

#include <type_traits>

namespace
{
    using tuner_match::MatchJob;
    using tuner_match::MatchOutcome;
    using tuner_match::MatchResult;
    using tuner_match::BotInstance;
    using quality_harness::ValueSeat;
    using Seat = std::variant<BotInstance, ValueSeat>;

    uint64_t vs_pair_seed(uint64_t seed, size_t j)
    {
        return quality_harness::splitmix64(seed
            ^ quality_harness::splitmix64(static_cast<uint64_t>(j) * 0x94D049BB133111EBULL));
    }

    uint64_t mc_scenario_seed(uint64_t seed, size_t pair, int seat)
    {
        return quality_harness::splitmix64(seed * 0x9e3779b97f4a7c15ull
            + static_cast<uint64_t>(pair) * 2ull + static_cast<uint64_t>(seat));
    }

    struct BatchConfig
    {
        int default_iters = 0;
        int default_ms = 20;
        int max_rounds = 3600;
        bool collect_digests = false;
    };

    struct PairDigests
    {
        uint64_t seat1 = 0;
        uint64_t seat2 = 0;
        size_t value_wall_samples = 0;
        double value_wall_median_ms = 0.0;
        double value_wall_p95_ms = 0.0;
        double value_wall_max_ms = 0.0;
        size_t value_replay_failures = 0;
        size_t value_arena_exhaustions = 0;
        size_t value_deaths_spawn = 0;
        size_t value_deaths_lockout = 0;
        size_t value_deaths_invalid = 0;
    };

    template <typename S>
    double wall_stat(S &seat, double percentile)
    {
        auto const &v = seat.move_wall_ms();
        if (v.empty())
        {
            return 0.0;
        }
        std::vector<double> s(v.begin(), v.end());
        std::sort(s.begin(), s.end());
        auto idx = static_cast<size_t>(percentile * static_cast<double>(s.size()));
        if (idx >= s.size())
        {
            idx = s.size() - 1;
        }
        return s[idx];
    }

    struct PlayOutput
    {
        MatchResult result;
    };

    template <typename S1, typename S2>
    PlayOutput play_one(S1 &s1, S2 &s2, int max_rounds,
        std::counting_semaphore<> *permits)
    {
        PlayOutput out;
        out.result = quality_harness::play_match_seats(s1, s2, max_rounds, permits);
        return out;
    }

    PlayOutput play_variant_seat(Seat &s1, Seat &s2, int max_rounds,
        std::counting_semaphore<> *permits)
    {
        return std::visit(
            [&](auto &a)
            {
                return std::visit(
                    [&](auto &b) { return play_one(a, b, max_rounds, permits); },
                    s2);
            },
            s1);
    }

    struct SeatRuntime
    {
        int engine_id = 0;
        double const *theta = nullptr;
        int iters = -1;
        int ms = -1;
    };

    void make_seat(SeatRuntime const &r, Seat &s)
    {
        if (r.engine_id == quality_harness::engine_value)
        {
            s.emplace<ValueSeat>();
            ValueSeat &v = std::get<ValueSeat>(s);
            v.init(r.theta);
            v.set_budget(r.iters > 0 ? r.iters : 0, r.iters > 0 ? 0 : r.ms);
            return;
        }
        s.emplace<BotInstance>();
        BotInstance &b = std::get<BotInstance>(s);
        b.init(r.theta);
        if (r.iters > 0)
        {
            b.search_budget = m_tetris::SearchBudget::by_iterations(
                static_cast<size_t>(r.iters));
        }
        else
        {
            b.search_budget = m_tetris::SearchBudget{
                static_cast<time_t>(r.ms > 0 ? r.ms : 0)
            };
        }
    }

    MatchOutcome run_single_job(MatchJob const &job, BatchConfig const &cfg,
        std::counting_semaphore<> *permits, PairDigests *digests_out)
    {
        tuner_match::Scenario scenario_p1 = tuner_match::make_scenario(
            job.scenario_seed_p1, static_cast<size_t>(cfg.max_rounds),
            static_cast<size_t>(tuner_match::next_length));
        tuner_match::Scenario scenario_p2 = tuner_match::make_scenario(
            job.scenario_seed_p2, static_cast<size_t>(cfg.max_rounds),
            static_cast<size_t>(tuner_match::next_length));
        auto resolve_budget = [&](int iters, int ms)
        {
            if (iters > 0)
            {
                return std::pair{iters, -1};
            }
            if (ms > 0)
            {
                return std::pair{0, ms};
            }
            if (cfg.default_iters > 0)
            {
                return std::pair{cfg.default_iters, -1};
            }
            return std::pair{0, cfg.default_ms};
        };
        auto [ri1, rm1] = resolve_budget(job.budget_iters_p1, job.budget_ms_p1);
        auto [ri2, rm2] = resolve_budget(job.budget_iters_p2, job.budget_ms_p2);
        SeatRuntime r1{ job.engine1, job.p1, ri1, rm1 };
        SeatRuntime r2{ job.engine2, job.p2, ri2, rm2 };
        Seat s1{std::in_place_type<BotInstance>};
        Seat s2{std::in_place_type<BotInstance>};
        make_seat(r1, s1);
        make_seat(r2, s2);
        std::visit([&](auto &a) { a.scenario = &scenario_p1; }, s1);
        std::visit([&](auto &a) { a.scenario = &scenario_p2; }, s2);
        PlayOutput played = play_variant_seat(s1, s2, cfg.max_rounds, permits);

        MatchOutcome o;
        o.winner = played.result.winner;
        o.dead1 = played.result.dead1;
        o.dead2 = played.result.dead2;
        o.capped = played.result.capped;
        o.winner_reason = played.result.winner_reason;
        o.rounds = played.result.rounds;
        o.app1 = played.result.app1;
        o.app2 = played.result.app2;
        o.apl1 = played.result.apl1;
        o.apl2 = played.result.apl2;
        std::visit([&](auto &a)
            {
                o.attack1 = static_cast<size_t>(a.total_attack);
                o.pieces1 = static_cast<size_t>(a.total_block);
                o.lines1 = static_cast<size_t>(a.total_clear);
                o.tspin_mini1 = static_cast<size_t>(a.tspin_mini);
                o.tspin_single1 = static_cast<size_t>(a.tspin_single);
                o.tspin_double1 = static_cast<size_t>(a.tspin_double);
                o.tspin_triple1 = static_cast<size_t>(a.tspin_triple);
                o.perfect_clear1 = static_cast<size_t>(a.perfect_clears);
            }, s1);
        std::visit([&](auto &b)
            {
                o.attack2 = static_cast<size_t>(b.total_attack);
                o.pieces2 = static_cast<size_t>(b.total_block);
                o.lines2 = static_cast<size_t>(b.total_clear);
                o.tspin_mini2 = static_cast<size_t>(b.tspin_mini);
                o.tspin_single2 = static_cast<size_t>(b.tspin_single);
                o.tspin_double2 = static_cast<size_t>(b.tspin_double);
                o.tspin_triple2 = static_cast<size_t>(b.tspin_triple);
                o.perfect_clear2 = static_cast<size_t>(b.perfect_clears);
            }, s2);

        if (digests_out != nullptr)
        {
            auto consumed = [&](tuner_match::Scenario const &s, uint64_t seed)
            {
                auto gen = tuner_match::make_scenario(seed,
                    static_cast<size_t>(cfg.max_rounds),
                    static_cast<size_t>(tuner_match::next_length));
                return gen.pieces.size() - s.pieces.size();
            };
            digests_out->seat1 = consumed(scenario_p1, job.scenario_seed_p1);
            digests_out->seat2 = consumed(scenario_p2, job.scenario_seed_p2);
            std::visit([&](auto &b)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(b)>, ValueSeat>)
                    {
                        digests_out->value_wall_samples = b.move_wall_ms().size();
                        digests_out->value_wall_median_ms = wall_stat(b, 0.5);
                        digests_out->value_wall_p95_ms = wall_stat(b, 0.95);
                        digests_out->value_wall_max_ms = wall_stat(b, 1.0);
                        digests_out->value_replay_failures = b.replay_failures;
                        digests_out->value_arena_exhaustions = b.arena_exhaustions;
                        digests_out->value_deaths_spawn = b.deaths_spawn;
                        digests_out->value_deaths_lockout = b.deaths_lockout;
                        digests_out->value_deaths_invalid = b.deaths_invalid;
                    }
                }, s2);
        }
        return o;
    }

    std::vector<MatchOutcome> run_quality_batch(std::vector<MatchJob> const &jobs,
        BatchConfig const &cfg, int threads, std::vector<PairDigests> *digests)
    {
        std::vector<MatchOutcome> out(jobs.size());
        if (digests != nullptr)
        {
            digests->assign(jobs.size(), PairDigests{});
        }
        std::atomic<size_t> next_job{ 0 };
        size_t const batch_threads = std::min<size_t>(std::max(1, threads), jobs.size());
        size_t const cpus = std::max(1u, std::thread::hardware_concurrency());
        size_t const slots = std::min(cpus, 2 * batch_threads);
        std::unique_ptr<std::counting_semaphore<>> permits;
        if (slots >= 2)
        {
            permits = std::make_unique<std::counting_semaphore<>>(static_cast<int>(slots));
        }
        auto worker = [&]()
        {
            for (;;)
            {
                size_t idx = next_job.fetch_add(1);
                if (idx >= jobs.size())
                {
                    return;
                }
                out[idx] = run_single_job(jobs[idx], cfg,
                    permits.get(), digests != nullptr ? &(*digests)[idx] : nullptr);
            }
        };
        std::vector<std::thread> pool;
        pool.reserve(batch_threads);
        for (size_t t = 0; t < batch_threads; ++t)
        {
            pool.emplace_back(worker);
        }
        for (auto &th : pool)
        {
            th.join();
        }
        return out;
    }

    struct VsResult
    {
        double wins_value = 0.0;
        int games = 0;
        double phat = 0.0;
        double ci_lo_two = 0.0;
        double ci_hi_two = 0.0;
        double lo_one_sided = 0.0;
        double app_value = 0.0, app_other = 0.0;
        double apl_value = 0.0, apl_other = 0.0;
        int deaths_value = 0, deaths_other = 0;
        double rounds_mean = 0.0;
    };

    VsResult aggregate_vs(std::vector<MatchJob> const &jobs,
        std::vector<MatchOutcome> const &out)
    {
        VsResult r;
        r.games = static_cast<int>(jobs.size());
        double const n = static_cast<double>(r.games);
        double app_v_sum = 0.0, app_o_sum = 0.0, apl_v_sum = 0.0, apl_o_sum = 0.0;
        double rounds_sum = 0.0;
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            MatchOutcome const &o = out[i];
            int value_seat = jobs[i].engine1 == quality_harness::engine_value ? 1
                : (jobs[i].engine2 == quality_harness::engine_value ? 2 : 0);
            double we1 = o.winner > 0 ? 1.0 : (o.winner == 0 ? 0.5 : 0.0);
            double we_value = value_seat == 1 ? we1 : (value_seat == 2 ? 1.0 - we1 : we1);
            r.wins_value += we_value;
            double app_v, app_o, apl_v, apl_o;
            bool dead_v, dead_o;
            if (value_seat == 2)
            {
                app_v = o.app2; apl_v = o.apl2; dead_v = o.dead2;
                app_o = o.app1; apl_o = o.apl1; dead_o = o.dead1;
            }
            else
            {
                app_v = o.app1; apl_v = o.apl1; dead_v = o.dead1;
                app_o = o.app2; apl_o = o.apl2; dead_o = o.dead2;
            }
            app_v_sum += app_v;
            apl_v_sum += apl_v;
            app_o_sum += app_o;
            apl_o_sum += apl_o;
            r.deaths_value += dead_v ? 1 : 0;
            r.deaths_other += dead_o ? 1 : 0;
            rounds_sum += static_cast<double>(o.rounds);
        }
        r.phat = r.wins_value / n;
        double const z2 = 1.96;
        double denom2 = 1.0 + z2 * z2 / n;
        double mid2 = r.phat + z2 * z2 / (2.0 * n);
        double margin2 = z2 * std::sqrt(std::max(0.0, r.phat * (1.0 - r.phat) / n
            + z2 * z2 / (4.0 * n * n)));
        r.ci_lo_two = (mid2 - margin2) / denom2;
        r.ci_hi_two = (mid2 + margin2) / denom2;
        double const z1 = 1.6449;
        double denom1 = 1.0 + z1 * z1 / n;
        double mid1 = r.phat + z1 * z1 / (2.0 * n);
        double margin1 = z1 * std::sqrt(std::max(0.0, r.phat * (1.0 - r.phat) / n
            + z1 * z1 / (4.0 * n * n)));
        r.lo_one_sided = (mid1 - margin1) / denom1;
        r.app_value = app_v_sum / n;
        r.app_other = app_o_sum / n;
        r.apl_value = apl_v_sum / n;
        r.apl_other = apl_o_sum / n;
        r.rounds_mean = rounds_sum / n;
        return r;
    }

    std::string outcome_csv_row(size_t pair, uint64_t seed, MatchJob const &job,
        MatchOutcome const &o)
    {
        std::string row = std::to_string(pair)
            + "," + std::to_string(seed)
            + "," + std::to_string(job.scenario_seed_p1)
            + "," + std::to_string(job.scenario_seed_p2)
            + "," + quality_harness::engine_name(job.engine1)
            + "," + quality_harness::engine_name(job.engine2)
            + "," + std::to_string(o.winner)
            + "," + (o.dead1 ? "1" : "0") + "," + (o.dead2 ? "1" : "0")
            + "," + (o.capped ? "1" : "0")
            + "," + std::to_string(o.winner_reason)
            + "," + std::to_string(o.rounds)
            + "," + std::to_string(o.attack1) + "," + std::to_string(o.attack2)
            + "," + std::to_string(o.pieces1) + "," + std::to_string(o.pieces2)
            + "," + std::to_string(o.lines1) + "," + std::to_string(o.lines2)
            + "," + std::to_string(o.tspin_mini1) + "," + std::to_string(o.tspin_mini2)
            + "," + std::to_string(o.tspin_single1) + "," + std::to_string(o.tspin_single2)
            + "," + std::to_string(o.tspin_double1) + "," + std::to_string(o.tspin_double2)
            + "," + std::to_string(o.tspin_triple1) + "," + std::to_string(o.tspin_triple2)
            + "," + std::to_string(o.perfect_clear1) + "," + std::to_string(o.perfect_clear2)
            + "," + std::to_string(o.replay_failures)
            + "\n";
        return row;
    }

    constexpr char const *csv_header =
        "pair,seed,scenario_seed_p1,scenario_seed_p2,engine1,engine2,winner,dead1,dead2,capped,winner_reason,rounds,"
        "attack1,attack2,pieces1,pieces2,lines1,lines2,"
        "tspin_mini1,tspin_mini2,tspin_single1,tspin_single2,tspin_double1,tspin_double2,tspin_triple1,tspin_triple2,"
        "perfect_clear1,perfect_clear2,replay_failures\n";

    std::string next_str(int &i, int argc, char **argv, std::string_view a)
    {
        if (i + 1 >= argc)
        {
            std::println(stderr, "missing value for {}", a);
            std::exit(2);
        }
        return argv[++i];
    }

    int cmd_match(int argc, char **argv)
    {
        std::string param_a, param_b, out_path;
        int pairs = 32, ms = 20, iters = 0, threads = 8, max_rounds = 3600;
        unsigned long long seed = 555;
        int e1 = quality_harness::engine_legacy, e2 = quality_harness::engine_value;
        for (int i = 2; i < argc; ++i)
        {
            std::string_view a = argv[i];
            if (a == "--param-a") param_a = next_str(i, argc, argv, a);
            else if (a == "--param-b") param_b = next_str(i, argc, argv, a);
            else if (a == "--pairs") pairs = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--ms") ms = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--iters") iters = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--seed") seed = std::stoull(next_str(i, argc, argv, a));
            else if (a == "--threads") threads = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--max-rounds") max_rounds = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--out") out_path = next_str(i, argc, argv, a);
            else if (a == "--engine1") e1 = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--engine2") e2 = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--seats")
            {
                std::string s = next_str(i, argc, argv, a);
                auto parse = [](std::string const &t)
                {
                    return t == "value" ? quality_harness::engine_value : quality_harness::engine_legacy;
                };
                auto comma = s.find(',');
                if (comma == std::string::npos)
                {
                    std::println(stderr, "--seats requires the form legacy,value");
                    return 2;
                }
                e1 = parse(s.substr(0, comma));
                e2 = parse(s.substr(comma + 1));
            }
            else
            {
                std::println(stderr, "unknown argument {}", a);
                return 2;
            }
        }
        double pa[tuner_match::NUM_PARAMS], pb[tuner_match::NUM_PARAMS];
        if (param_a.empty() || param_b.empty()
            || !tuner_match::read_theta_strict(param_a, pa)
            || !tuner_match::read_theta_strict(param_b, pb))
        {
            std::println(stderr, "usage: quality_match match --param-a F --param-b F --pairs N --ms 20 --seed S --threads T --max-rounds R --out CSV [--seats legacy,value] [--iters N] [--engine1 N] [--engine2 N]");
            return 2;
        }
        std::vector<MatchJob> jobs;
        jobs.reserve(static_cast<size_t>(2) * static_cast<size_t>(pairs));
        for (int j = 0; j < pairs; ++j)
        {
            uint64_t s = vs_pair_seed(seed, static_cast<size_t>(j));
            uint64_t sa = quality_harness::splitmix64(s);
            uint64_t sb = quality_harness::splitmix64(s ^ 0x9E3779B97F4A7C15ULL);
            MatchJob a{}, b{};
            std::memcpy(a.p1, pa, sizeof(a.p1));
            std::memcpy(a.p2, pb, sizeof(a.p2));
            a.scenario_seed_p1 = sa;
            a.scenario_seed_p2 = sb;
            a.budget_iters_p1 = iters;
            a.budget_iters_p2 = iters;
            a.budget_ms_p1 = iters > 0 ? -1 : ms;
            a.budget_ms_p2 = iters > 0 ? -1 : ms;
            a.job_id = static_cast<size_t>(2) * static_cast<size_t>(j);
            a.engine1 = e1;
            a.engine2 = e2;
            std::memcpy(b.p1, pb, sizeof(b.p1));
            std::memcpy(b.p2, pa, sizeof(b.p2));
            b.scenario_seed_p1 = sa;
            b.scenario_seed_p2 = sb;
            b.budget_iters_p1 = iters;
            b.budget_iters_p2 = iters;
            b.budget_ms_p1 = iters > 0 ? -1 : ms;
            b.budget_ms_p2 = iters > 0 ? -1 : ms;
            b.job_id = static_cast<size_t>(2) * static_cast<size_t>(j) + 1;
            b.engine1 = e2;
            b.engine2 = e1;
            jobs.push_back(a);
            jobs.push_back(b);
        }
        BatchConfig cfg;
        cfg.default_iters = iters > 0 ? iters : 0;
        cfg.default_ms = iters > 0 ? 0 : ms;
        cfg.max_rounds = max_rounds;
        cfg.collect_digests = true;
        std::vector<PairDigests> digests;
        auto started = std::chrono::steady_clock::now();
        std::vector<MatchOutcome> out = run_quality_batch(jobs, cfg, threads, &digests);
        double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        VsResult vs = aggregate_vs(jobs, out);
        std::string csv = csv_header;
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            csv += outcome_csv_row(static_cast<size_t>(i / 2), seed, jobs[i], out[i]);
        }
        if (!out_path.empty())
        {
            std::ofstream f(out_path, std::ios::binary);
            if (!f.good())
            {
                std::println(stderr, "cannot open {}", out_path);
                return 1;
            }
            f.write(csv.data(), static_cast<std::streamsize>(csv.size()));
        }
        size_t rf = 0, ax = 0, ds = 0, dl = 0, di = 0;
        double wall_med = 0.0, wall_p95 = 0.0, wall_max = 0.0;
        for (auto const &d : digests)
        {
            rf += d.value_replay_failures;
            ax += d.value_arena_exhaustions;
            ds += d.value_deaths_spawn;
            dl += d.value_deaths_lockout;
            di += d.value_deaths_invalid;
            wall_med = std::max(wall_med, d.value_wall_median_ms);
            wall_p95 = std::max(wall_p95, d.value_wall_p95_ms);
            wall_max = std::max(wall_max, d.value_wall_max_ms);
        }
        std::println("QUALITY_MATCH games={} value_win_equiv={:.1f} wr={:.6f} ci95_two=[{:.6f},{:.6f}] lo95_one={:.6f} app_value={:.6f} app_other={:.6f} apl_value={:.6f} apl_other={:.6f} rounds_mean={:.3f} deaths_value={} deaths_other={} replay_failures={} arena_exhaustions={} deaths_spawn={} deaths_lockout={} deaths_invalid={} value_wall_ms_median_across_pairs={:.3f} value_wall_ms_p95_across_pairs={:.3f} value_wall_ms_max_across_pairs={:.3f} total_wall_s={:.3f}",
            jobs.size(), vs.wins_value, vs.phat, vs.ci_lo_two, vs.ci_hi_two, vs.lo_one_sided,
            vs.app_value, vs.app_other, vs.apl_value, vs.apl_other, vs.rounds_mean,
            vs.deaths_value, vs.deaths_other, rf, ax, ds, dl, di,
            wall_med, wall_p95, wall_max, wall_s);
        return 0;
    }

    std::vector<MatchJob> mc_jobs(size_t pairs, uint64_t seed, int iters, int e1, int e2)
    {
        double theta[tuner_match::NUM_PARAMS];
        tuner_match::production_default_theta(theta);
        std::vector<MatchJob> jobs(pairs);
        for (size_t p = 0; p < pairs; ++p)
        {
            for (size_t k = 0; k < tuner_match::NUM_PARAMS; ++k)
            {
                jobs[p].p1[k] = theta[k];
                jobs[p].p2[k] = theta[k];
            }
            jobs[p].scenario_seed_p1 = mc_scenario_seed(seed, p, 1);
            jobs[p].scenario_seed_p2 = mc_scenario_seed(seed, p, 2);
            jobs[p].job_id = p;
            jobs[p].budget_iters_p1 = iters;
            jobs[p].budget_iters_p2 = iters;
            jobs[p].budget_ms_p1 = -1;
            jobs[p].budget_ms_p2 = -1;
            jobs[p].engine1 = e1;
            jobs[p].engine2 = e2;
        }
        return jobs;
    }

    bool rows_equal(MatchOutcome const &a, MatchOutcome const &b)
    {
        return a.winner == b.winner && a.dead1 == b.dead1 && a.dead2 == b.dead2
            && a.capped == b.capped && a.winner_reason == b.winner_reason
            && a.rounds == b.rounds && a.attack1 == b.attack1 && a.attack2 == b.attack2
            && a.pieces1 == b.pieces1 && a.pieces2 == b.pieces2
            && a.lines1 == b.lines1 && a.lines2 == b.lines2
            && a.tspin_mini1 == b.tspin_mini1 && a.tspin_mini2 == b.tspin_mini2
            && a.tspin_single1 == b.tspin_single1 && a.tspin_single2 == b.tspin_single2
            && a.tspin_double1 == b.tspin_double1 && a.tspin_double2 == b.tspin_double2
            && a.tspin_triple1 == b.tspin_triple1 && a.tspin_triple2 == b.tspin_triple2
            && a.perfect_clear1 == b.perfect_clear1 && a.perfect_clear2 == b.perfect_clear2;
    }

    std::string run_capture(std::string const &cmd)
    {
        std::string cmd_mut = cmd + " 2>&1";
        FILE *pipe = popen(cmd_mut.c_str(), "r");
        if (pipe == nullptr)
        {
            return std::string();
        }
        std::string out;
        std::array<char, 4096> buf{};
        while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        {
            out += buf.data();
        }
        int rc = pclose(pipe);
        out += "\nEXIT=" + std::to_string(rc);
        return out;
    }

    int cmd_parity(int argc, char **argv)
    {
        std::string out_path;
        size_t pairs = 512;
        unsigned long long seed = 1;
        int threads = 8, iters = 200, max_rounds = 3600;
        for (int i = 2; i < argc; ++i)
        {
            std::string_view a = argv[i];
            if (a == "--pairs") pairs = std::stoull(next_str(i, argc, argv, a));
            else if (a == "--seed") seed = std::stoull(next_str(i, argc, argv, a));
            else if (a == "--threads") threads = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--iters") iters = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--max-rounds") max_rounds = std::stoi(next_str(i, argc, argv, a));
            else if (a == "--out") out_path = next_str(i, argc, argv, a);
            else
            {
                std::println(stderr, "unknown argument {}", a);
                return 2;
            }
        }
        double theta[tuner_match::NUM_PARAMS];
        tuner_match::production_default_theta(theta);
        std::vector<MatchJob> jobs(pairs);
        for (size_t p = 0; p < pairs; ++p)
        {
            for (size_t k = 0; k < tuner_match::NUM_PARAMS; ++k)
            {
                jobs[p].p1[k] = theta[k];
                jobs[p].p2[k] = theta[k];
            }
            jobs[p].scenario_seed_p1 = mc_scenario_seed(seed, p, 1);
            jobs[p].scenario_seed_p2 = mc_scenario_seed(seed, p, 2);
            jobs[p].job_id = p;
            jobs[p].budget_iters_p1 = iters;
            jobs[p].budget_iters_p2 = iters;
            jobs[p].budget_ms_p1 = -1;
            jobs[p].budget_ms_p2 = -1;
            jobs[p].engine1 = quality_harness::engine_legacy;
            jobs[p].engine2 = quality_harness::engine_legacy;
        }
        BatchConfig cfg;
        cfg.default_iters = iters;
        cfg.default_ms = 0;
        std::vector<MatchOutcome> first = run_quality_batch(jobs, cfg, threads, nullptr);
        std::vector<MatchOutcome> second = run_quality_batch(jobs, cfg, threads, nullptr);
        size_t mismatched = 0;
        for (size_t p = 0; p < pairs; ++p)
        {
            if (!rows_equal(first[p], second[p]))
            {
                ++mismatched;
                std::println(stderr, "nondeterministic pair {}", p);
            }
        }
        if (!out_path.empty())
        {
            std::ofstream f(out_path, std::ios::binary);
            f << csv_header;
            for (size_t p = 0; p < pairs; ++p)
            {
                std::print(f, "{}", outcome_csv_row(p, seed, jobs[p], first[p]));
            }
        }
        std::println("PARITY pairs={} mismatches={}", pairs, mismatched);
        return mismatched == 0 ? 0 : 1;
    }

    struct CheckLog
    {
        int checks = 0;
        int failures = 0;
        void expect(bool ok, std::string const &what)
        {
            ++checks;
            if (!ok)
            {
                ++failures;
                std::println(stderr, "SELFTEST fail: {}", what);
            }
        }
    };

    int selftest_main(bool quick)
    {
        CheckLog log;
        size_t const parity_pairs = quick ? 48 : 512;
        size_t const value_pairs = quick ? 24 : 128;
        int const threads = quick ? 4 : 8;

        std::string self_path = "/proc/self/exe";
        std::array<char, 8192> resolved{};
        ssize_t const len = readlink(self_path.c_str(), resolved.data(), resolved.size() - 1);
        std::string dir = ".";
        if (len > 0)
        {
            resolved[static_cast<size_t>(len)] = '\0';
            dir = std::string(resolved.data());
            auto slash = dir.rfind('/');
            dir = slash == std::string::npos ? "." : dir.substr(0, slash);
        }

        BatchConfig cfg;
        cfg.default_iters = 200;
        cfg.default_ms = 0;

        std::vector<MatchJob> jobs = mc_jobs(parity_pairs, 1, 200,
            quality_harness::engine_legacy, quality_harness::engine_legacy);
        std::vector<MatchOutcome> first = run_quality_batch(jobs, cfg, threads, nullptr);
        std::vector<MatchOutcome> second = run_quality_batch(jobs, cfg, threads, nullptr);
        size_t mismatches = 0;
        for (size_t p = 0; p < jobs.size(); ++p)
        {
            if (!rows_equal(first[p], second[p]))
            {
                ++mismatches;
            }
        }
        log.expect(mismatches == 0, "gate 1 legacy parity rerun zero mismatches");

        std::string mc_csv = dir + "/migration_compare_selftest.csv";
        std::string mc_out = run_capture(dir + "/migration_compare "
            + std::to_string(parity_pairs) + " " + mc_csv + " " + std::to_string(threads)
            + " 1 200 3600");
        log.expect(mc_out.find("EXIT=0") != std::string::npos,
            "gate 5 migration_compare exits zero");
        log.expect(mc_out.find("0 nondeterministic") != std::string::npos,
            "gate 5 migration_compare deterministic");

        std::vector<MatchOutcome> mc_rows;
        {
            std::ifstream f(mc_csv);
            std::string line;
            std::getline(f, line);
            while (std::getline(f, line))
            {
                std::vector<std::string> cols;
                std::string cell;
                std::stringstream ss(line);
                while (std::getline(ss, cell, ','))
                {
                    cols.push_back(cell);
                }
                if (cols.size() < 29)
                {
                    break;
                }
                MatchOutcome o;
                o.winner = std::stoi(cols[6]);
                o.dead1 = cols[7] == "1";
                o.dead2 = cols[8] == "1";
                o.capped = cols[9] == "1";
                o.winner_reason = std::stoi(cols[10]);
                o.rounds = std::stoi(cols[11]);
                o.attack1 = std::stoull(cols[12]);
                o.attack2 = std::stoull(cols[13]);
                o.pieces1 = std::stoull(cols[14]);
                o.pieces2 = std::stoull(cols[15]);
                o.lines1 = std::stoull(cols[16]);
                o.lines2 = std::stoull(cols[17]);
                o.tspin_mini1 = std::stoull(cols[18]);
                o.tspin_mini2 = std::stoull(cols[19]);
                o.tspin_single1 = std::stoull(cols[20]);
                o.tspin_single2 = std::stoull(cols[21]);
                o.tspin_double1 = std::stoull(cols[22]);
                o.tspin_double2 = std::stoull(cols[23]);
                o.tspin_triple1 = std::stoull(cols[24]);
                o.tspin_triple2 = std::stoull(cols[25]);
                o.perfect_clear1 = std::stoull(cols[26]);
                o.perfect_clear2 = std::stoull(cols[27]);
                o.replay_failures = std::stoull(cols[28]);
                mc_rows.push_back(o);
            }
        }
        log.expect(mc_rows.size() == jobs.size(), "gate 1 migration_compare row count");
        size_t field_mismatch = 0;
        for (size_t p = 0; p < jobs.size() && p < mc_rows.size(); ++p)
        {
            if (!rows_equal(first[p], mc_rows[p]))
            {
                ++field_mismatch;
                std::println(stderr, "gate 1 row {} differs from migration_compare", p);
            }
        }
        log.expect(field_mismatch == 0, "gate 1 migration_compare field-for-field agreement");

        std::vector<MatchJob> vjobs = mc_jobs(value_pairs, 2, 200,
            quality_harness::engine_value, quality_harness::engine_value);
        std::vector<MatchOutcome> vfirst = run_quality_batch(vjobs, cfg, threads, nullptr);
        std::vector<MatchOutcome> vsecond = run_quality_batch(vjobs, cfg, threads, nullptr);
        size_t vmismatch = 0;
        for (size_t p = 0; p < vjobs.size(); ++p)
        {
            if (!rows_equal(vfirst[p], vsecond[p]))
            {
                ++vmismatch;
            }
        }
        log.expect(vmismatch == 0, "gate 2 value self-parity rerun zero mismatches");

        BatchConfig digest_cfg = cfg;
        std::vector<PairDigests> digests;
        std::vector<MatchJob> mjobs = mc_jobs(8, 3, 200,
            quality_harness::engine_legacy, quality_harness::engine_value);
        run_quality_batch(mjobs, digest_cfg, 2, &digests);
        size_t zero_consumption = 0;
        size_t same_engine_mismatch = 0;
        for (size_t p = 0; p < mjobs.size(); ++p)
        {
            if (digests[p].seat1 == 0 || digests[p].seat2 == 0)
            {
                ++zero_consumption;
            }
            if (mjobs[p].engine1 == mjobs[p].engine2 && digests[p].seat1 != digests[p].seat2)
            {
                ++same_engine_mismatch;
            }
        }
        log.expect(zero_consumption == 0,
            "gate 3 both seats consumed from their shared-scenario streams");
        log.expect(same_engine_mismatch == 0,
            "gate 3 same-engine seats consumed identical counts (legacy seat equals value seat rate only within an engine)");
        log.expect(std::all_of(digests.begin(), digests.end(),
                        [](PairDigests const &d)
                        { return d.seat1 <= d.seat2 + 64 && d.seat2 <= d.seat1 + 64; }),
            "gate 3 mixed-pair consumption rates within hold-decision divergence bound");

        {
            tuner_match::Scenario s1 = tuner_match::make_scenario(7, 64,
                static_cast<size_t>(tuner_match::next_length));
            tuner_match::Scenario s2 = tuner_match::make_scenario(7, 64,
                static_cast<size_t>(tuner_match::next_length));
            ValueSeat a, b;
            a.scenario = &s1;
            b.scenario = &s2;
            double theta[tuner_match::NUM_PARAMS];
            tuner_match::production_default_theta(theta);
            a.init(theta);
            b.init(theta);
            a.set_budget(200, 0);
            b.set_budget(200, 0);
            for (int round = 0; round < 12 && !a.dead && !b.dead; ++round)
            {
                a.prepare();
                b.prepare();
                a.run();
                b.run();
            }
            log.expect(a.consumed_digest() == b.consumed_digest(),
                "gate 4 value seats consume identical piece streams");
            log.expect(a.total_attack == b.total_attack && a.total_block == b.total_block
                    && a.total_clear == b.total_clear,
                "gate 4 value seats identical accounting on same stream");
            log.expect(a.replay_failures == 0, "gate 4 value seat replay failures zero");
        }

        {
            using profile_value::SpinClass;
            for (int clear = 0; clear <= 4; ++clear)
            {
                for (int spin = 0; spin <= 2; ++spin)
                {
                    int combo_a = 2, b2b_a = 1;
                    int combo_b = 2, b2b_b = 1;
                    auto const sc = static_cast<SpinClass>(spin);
                    int atk_a = profile_value::score_attack(clear, sc, false, combo_b, b2b_b);
                    int atk_b = 0;
                    switch (clear)
                    {
                    case 0:
                        combo_a = 0;
                        break;
                    case 1:
                        if (spin == 1) { atk_b += 1 + b2b_a; b2b_a = 1; }
                        else if (spin == 2) { atk_b += 2 + b2b_a; b2b_a = 1; }
                        else { b2b_a = 0; }
                        atk_b += tuner_match::combo_table[std::min(tuner_match::combo_table_max - 1, ++combo_a)];
                        break;
                    case 2:
                        if (spin != 0) { atk_b += 4 + b2b_a; b2b_a = 1; }
                        else { atk_b += 1; b2b_a = 0; }
                        atk_b += tuner_match::combo_table[std::min(tuner_match::combo_table_max - 1, ++combo_a)];
                        break;
                    case 3:
                        if (spin != 0) { atk_b += 6 + b2b_a * 2; b2b_a = 1; }
                        else { atk_b += 2; b2b_a = 0; }
                        atk_b += tuner_match::combo_table[std::min(tuner_match::combo_table_max - 1, ++combo_a)];
                        break;
                    case 4:
                        atk_b += tuner_match::combo_table[std::min(tuner_match::combo_table_max - 1, ++combo_a)] + 4 + b2b_a;
                        b2b_a = 1;
                        break;
                    default:
                        break;
                    }
                    log.expect(atk_a == atk_b && combo_a == combo_b && b2b_a == b2b_b,
                        "gate 4 attack scoring parity clear=" + std::to_string(clear)
                            + " spin=" + std::to_string(spin));
                }
            }
        }

        {
            tuner_match::Scenario s = tuner_match::make_scenario(11, 64,
                static_cast<size_t>(tuner_match::next_length));
            int const hole_col = tuner_match::scenario_hole(s);
            std::uint32_t const hole = 1u << hole_col;
            std::uint32_t const garbage_row = ((1u << 10) - 1u) & ~hole;

            std::array<std::uint16_t, 48> rows{};
            rows[5] = static_cast<std::uint16_t>(1u << 3);
            tetris::Board board = tetris::Board::from_rows(rows);
            board.add_garbage(2, static_cast<std::uint16_t>(garbage_row));

            m_tetris::TetrisMap lm(10, 40);
            lm.row[5] = 1u << 3;
            lm.count = 1;
            lm.top[3] = 6;
            lm.roof = 6;
            for (int y = 39; y >= 2; --y)
            {
                lm.row[y] = lm.row[y - 2];
            }
            lm.row[0] = garbage_row;
            lm.row[1] = garbage_row;

            bool same = true;
            for (int y = 0; y < 20 && same; ++y)
            {
                for (int x = 0; x < 10; ++x)
                {
                    if (board.full(x, y) != lm.full(x, y))
                    {
                        same = false;
                    }
                }
            }
            bool hole_open = !board.full(hole_col, 0) && !board.full(hole_col, 1);
            bool shifted = board.full(3, 7) && !board.full(3, 5);
            log.expect(same && hole_open && shifted,
                "gate 4 value garbage rows match legacy placement semantics");
        }

        std::println("SELFTEST quality_match: {} checks, {} failures{}",
            log.checks, log.failures, quick ? " (quick)" : "");
        return log.failures == 0 ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::println(stderr, "usage: quality_match <match|parity|selftest> [options]");
        return 2;
    }
    std::string_view mode = argv[1];
    if (mode == "match")
    {
        return cmd_match(argc, argv);
    }
    if (mode == "parity")
    {
        return cmd_parity(argc, argv);
    }
    if (mode == "selftest")
    {
        bool quick = false;
        for (int i = 2; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--quick")
            {
                quick = true;
            }
        }
        return selftest_main(quick);
    }
    std::println(stderr, "unknown mode {}", mode);
    return 2;
}
