
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <print>
#include <string>
#include <vector>

#include "tuner_match.h"
#include "tournament_optimizer.h"

using namespace tournament_opt;

namespace
{
    struct Config
    {
        std::string arm;
        uint64_t seed = 0;
        int generations = 200;
        int ipm = 80;
        int threads = 1;
        int max_rounds = 3600;
        std::string anchors_dir;
    };

    ParameterVector read_params(std::string const &path)
    {
        ParameterVector v{};
        std::ifstream f(path, std::ios::binary);
        f.read(reinterpret_cast<char *>(v.data()), NUM_PARAMS * sizeof(double));
        return v;
    }

    void write_params(std::string const &path, ParameterVector const &v)
    {
        std::ofstream f(path + ".tmp", std::ios::binary);
        f.write(reinterpret_cast<char const *>(v.data()), NUM_PARAMS * sizeof(double));
        f.close();
        std::rename((path + ".tmp").c_str(), path.c_str());
    }

    double wilson_lo(double wins_equiv, int games)
    {
        double const z = 1.6448536269514722;
        double phat = wins_equiv / static_cast<double>(games);
        double denom = 1.0 + z * z / games;
        double mid = phat + z * z / (2.0 * games);
        double margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / games
                                      + z * z / (4.0 * games * games));
        return (mid - margin) / denom;
    }

    int run(Config const &cfg)
    {
        if (cfg.arm != "a1" && cfg.arm != "a2")
        {
            std::println(stderr, "[ARM] unknown arm {} (use a1 or a2)", cfg.arm);
            return 1;
        }

        std::vector<std::string> anchors;
        if (cfg.arm == "a1")
        {
            namespace fs = std::filesystem;
            for (auto const &e : fs::directory_iterator(cfg.anchors_dir))
            {
                if (e.path().extension() == ".bin")
                {
                    anchors.push_back(e.path().string());
                }
            }
            std::sort(anchors.begin(), anchors.end());
            if (anchors.empty())
            {
                std::println(stderr, "[ARM] no anchors in {}", cfg.anchors_dir);
                return 1;
            }
        }

        size_t const POP = cfg.arm == "a1" ? 7 : 8;
        ParameterVector mean_theta{};
        {
            ai_zzz::TOJ::Param p;
            tuner_match::param_to_array(p, mean_theta.data());
        }

        std::vector<Candidate> cands = init_population(mean_theta, POP, cfg.seed);
        PsoState pso;
        Candidate global_best = cands[0];

        std::atomic<bool> view{ false };
        std::atomic<uint32_t> view_index{ 0 };
        std::mutex view_mutex;

        std::string best_path = "best_arm_" + cfg.arm + "_"
            + std::to_string(cfg.seed) + ".bin";

        std::println("[ARM] {} seed={} pop={} gens={} ipm={} threads={} rounds={} anchors={}",
                     cfg.arm, cfg.seed, POP, cfg.generations, cfg.ipm, cfg.threads,
                     cfg.max_rounds, anchors.size());
        std::fflush(stdout);

        for (int g = 0; g < cfg.generations; ++g)
        {
            ParameterVector anchor_theta = mean_theta;
            std::vector<Outcome> results;
            int total_games = 0;
            auto run_cmps = [&](std::vector<Comparison> const &cmps) -> std::vector<Outcome>
            {
                JobPlan plan = comparisons_to_jobs(cmps, cands, anchor_theta, cfg.ipm, 0);
                auto outs = tuner_match::run_batch(plan.jobs, cfg.threads, cfg.ipm, 0,
                                                   cfg.max_rounds, view, view_mutex, view_index);
                return outcomes_to_results(outs, plan);
            };

            if (cfg.arm == "a1")
            {
                anchor_theta = read_params(anchors[g % anchors.size()]);
                auto sched = schedule_common_anchor_7x2(cands, anchor_theta,
                                                        next_scenario_seed(cfg.seed, g));
                results = run_cmps(sched);
                total_games = static_cast<int>(sched.size());
            }
            else
            {
                ScreenTournament st(cands, next_scenario_seed(cfg.seed, g));
                while (!st.finished())
                {
                    auto round = st.next_games();
                    auto r = run_cmps(round);
                    total_games += static_cast<int>(round.size());
                    st.report_outcomes(r);
                    results.insert(results.end(), r.begin(), r.end());
                }
                auto tail = st.tail_games(cands);
                auto r = run_cmps(tail);
                total_games += static_cast<int>(tail.size());
                results.insert(results.end(), r.begin(), r.end());
            }

            update_ratings_generation(cands, results);

            auto ranked = select_survivors(cands, cands.size());
            std::string top_str;
            for (size_t i = 0; i < std::min<size_t>(3, ranked.size()); ++i)
            {
                top_str += std::to_string(ranked[i].id) + "("
                    + std::to_string(static_cast<int>(conservative_score(ranked[i]))) + ") ";
            }
            std::println("[ARM] gen {:4d} | games={} | top3 [{}]", g, total_games, top_str);
            std::fflush(stdout);

            bool new_global = conservative_score(ranked[0]) > conservative_score(global_best)
                || (conservative_score(ranked[0]) == conservative_score(global_best)
                    && ranked[0].id < global_best.id);
            if (new_global)
            {
                global_best = ranked[0];
            }
            if (cfg.arm == "a1")
            {
                cands = update_population_pso(cands, pso, global_best, cfg.seed);
            }
            else
            {
                cands = update_population_survivor_reseed(cands, 4, cfg.seed);
            }

            ParameterVector best{};
            std::memcpy(best.data(), global_best.theta, NUM_PARAMS * sizeof(double));
            write_params(best_path, best);
        }

        std::println("[ARM] done: {} generations; best published to {}", cfg.generations, best_path);
        return 0;
    }
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);
    Config cfg;
    if (argc < 6)
    {
        std::println(stderr, "usage: arm_driver <a1|a2> <seed> <generations> <iters_per_move> <threads> <max_rounds> [anchors_dir]");
        return 1;
    }
    cfg.arm = argv[1];
    cfg.seed = static_cast<unsigned>(std::stoul(argv[2]));
    cfg.generations = std::max(1, std::stoi(argv[3]));
    cfg.ipm = std::max(1, std::stoi(argv[4]));
    cfg.threads = std::max(1, std::stoi(argv[5]));
    cfg.max_rounds = std::max(1, std::stoi(argv[6]));
    cfg.anchors_dir = argc > 7 ? argv[7] : "out/anchors";
    return run(cfg);
}
