#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "ai_easy.h"
#include "rule_srs.h"
#include "search_simple.h"
#include "tetris_core.h"
#include "tournament/scheduler.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"

#ifndef TUNING_ENGINE_MATCH_TEST_SKIP_TOJ
#include "tuning/toj_adapter.h"
#include "tuning/toj_match.h"
#endif

namespace
{
    int failures = 0;

    void check(bool ok, std::string const& name)
    {
        if (ok)
        {
            std::printf("PASS: %s\n", name.c_str());
        }
        else
        {
            std::printf("FAIL: %s\n", name.c_str());
            ++failures;
        }
    }

    template<class Fn>
    bool throws_any(Fn&& fn)
    {
        try
        {
            fn();
        }
        catch (...)
        {
            return true;
        }
        return false;
    }

    std::atomic<bool> g_rendezvous_enabled{false};
    std::atomic<int> g_rendezvous_count{0};
    std::atomic<int> g_rendezvous_needed{2};
    std::atomic<bool> g_rendezvous_met{false};

    void rendezvous_reset(int needed)
    {
        g_rendezvous_count.store(0, std::memory_order_relaxed);
        g_rendezvous_needed.store(needed, std::memory_order_relaxed);
        g_rendezvous_met.store(false, std::memory_order_relaxed);
        g_rendezvous_enabled.store(true, std::memory_order_release);
    }

    void rendezvous_disable()
    {
        g_rendezvous_enabled.store(false, std::memory_order_release);
    }

    void rendezvous_point()
    {
        if (!g_rendezvous_enabled.load(std::memory_order_acquire))
        {
            return;
        }
        int const side = g_rendezvous_count.fetch_add(1, std::memory_order_acq_rel);
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (g_rendezvous_count.load(std::memory_order_acquire) < g_rendezvous_needed.load(std::memory_order_relaxed)
               && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (g_rendezvous_count.load(std::memory_order_acquire) >= g_rendezvous_needed.load(std::memory_order_relaxed)
            && side == 0)
        {
            g_rendezvous_met.store(true, std::memory_order_release);
        }
    }

    struct FakeInstance
    {
        std::shared_ptr<m_tetris::TetrisContext> context_ptr;
        std::vector<double> theta;
        int moves = 0;

        bool apply_theta(double const* values, std::size_t count)
        {
            if (count != 3 || !tuning::theta_finite(values, count))
            {
                return false;
            }
            if (values[2] < 0.0)
            {
                return false;
            }
            theta.assign(values, values + count);
            moves = 0;
            return true;
        }

        tuning::MoveOutcome run_move(tuning::MoveRequest const& request, m_tetris::SearchBudget budget)
        {
            (void)request;
            (void)budget;
            rendezvous_point();
            ++moves;
            if (theta[1] == 9.0)
            {
                throw std::runtime_error("fake adapter move failure");
            }
            tuning::MoveOutcome outcome;
            int const die_after = static_cast<int>(theta[2]);
            outcome.dead = die_after > 0 && moves >= die_after;
            outcome.clear = static_cast<int>(std::min(4.0, std::max(0.0, theta[0])));
            outcome.spin = theta[1] == 1.0 ? tuning::Spin::Mini
                : theta[1] == 2.0 ? tuning::Spin::Full
                : tuning::Spin::None;
            return outcome;
        }

        m_tetris::TetrisContext* context()
        {
            return context_ptr.get();
        }
    };

    char const* const fake_names[] = { "plan_clear", "plan_spin", "plan_death" };
    double const fake_scales[] = { 1.0, 1.0, 1.0 };
    double const fake_defaults[] = { 2.0, 0.0, 0.0 };

    struct FakeAdapter
    {
        using engine_type = int;
        using instance_type = FakeInstance;

        static tuning::ParamSchema schema()
        {
            return tuning::ParamSchema{
                .adapter_id = "fake_engine3",
                .names = fake_names,
                .scales = fake_scales,
                .defaults = fake_defaults,
            };
        }

        static constexpr std::size_t param_count()
        {
            return 3;
        }

        static constexpr std::size_t next_length()
        {
            return 6;
        }

        static std::span<int const> combo_table()
        {
            static int const table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
            return table;
        }

        static constexpr std::uint64_t memory_limit_bytes()
        {
            return 1ull << 20;
        }

        static std::shared_ptr<m_tetris::TetrisContext> make_shared_context()
        {
            static std::shared_ptr<m_tetris::TetrisContext> const cached = []
            {
                m_tetris::TetrisEngine<rule_srs::TetrisRule, ai_easy::AI, search_simple::Search> engine;
                if (!engine.prepare(10, 40))
                {
                    return std::shared_ptr<m_tetris::TetrisContext>{};
                }
                return engine.context();
            }();
            return cached;
        }

        static FakeInstance make_instance(std::shared_ptr<m_tetris::TetrisContext> const& context)
        {
            FakeInstance instance;
            instance.context_ptr = context;
            return instance;
        }
    };

    using Backend = tuning::EngineMatchBackend<FakeAdapter>;

    std::vector<double> t3(double a, double b, double c)
    {
        return { a, b, c };
    }

    bool same_outcome(tuning::GameOutcome const& a, tuning::GameOutcome const& b)
    {
        return a.id == b.id && a.winner == b.winner && a.dead_a == b.dead_a && a.dead_b == b.dead_b
            && a.capped == b.capped && a.rounds == b.rounds && a.app_a == b.app_a && a.app_b == b.app_b
            && a.apl_a == b.apl_a && a.apl_b == b.apl_b && a.reason == b.reason;
    }

    std::string describe(tuning::GameOutcome const& o)
    {
        return "id=" + std::to_string(o.id) + " winner=" + std::to_string(o.winner)
            + " reason=" + std::to_string(static_cast<int>(o.reason)) + " rounds=" + std::to_string(o.rounds)
            + " capped=" + std::to_string(static_cast<int>(o.capped))
            + " dead_a=" + std::to_string(static_cast<int>(o.dead_a))
            + " dead_b=" + std::to_string(static_cast<int>(o.dead_b))
            + " app_a=" + std::to_string(o.app_a) + " app_b=" + std::to_string(o.app_b)
            + " apl_a=" + std::to_string(o.apl_a) + " apl_b=" + std::to_string(o.apl_b);
    }

    tuning::RunConfig fake_config(int threads, int max_rounds)
    {
        tuning::RunConfig config;
        config.threads = threads;
        config.iterations_per_move = 4;
        config.max_rounds = max_rounds;
        return config;
    }

    void test_fake_scenario_primitives()
    {
        auto const scenario = tuning::make_game_scenario(0xC0FFEE, 100, 6);
        std::size_t const needed_pieces = 100 * 2 + 6 * 2 + 4;
        std::size_t const bag_rounded = (needed_pieces + 6) / 7 * 7;
        check(scenario.pieces.size() == bag_rounded,
              "fake scenario builds whole seven bags covering the declared need");
        std::string const bag = "IJLOSTZ";
        bool bags_valid = scenario.pieces.size() % 7 == 0;
        for (std::size_t base = 0; base + 7 <= scenario.pieces.size(); base += 7)
        {
            int seen[7] = { 0 };
            for (std::size_t i = 0; i < 7; ++i)
            {
                std::size_t const slot = bag.find(scenario.pieces[base + i]);
                bags_valid = bags_valid && slot != std::string::npos && seen[slot] == 0;
                if (slot != std::string::npos)
                {
                    seen[slot] = 1;
                }
            }
        }
        check(bags_valid, "fake scenario pieces form complete seven bags");
        check(tuning::make_game_scenario(1, 100, 6).pieces.size()
                  == tuning::make_game_scenario(2, 100, 6).pieces.size(),
              "scenario size independent of seed");
        check(tuning::game_scenario_hole(scenario) >= 0 && tuning::game_scenario_hole(scenario) < 10,
              "scenario hole column stays on the board");
        check(tuning::combo_attack(FakeAdapter::combo_table(), 1) == 0
                  && tuning::combo_attack(FakeAdapter::combo_table(), 4) == 1
                  && tuning::combo_attack(FakeAdapter::combo_table(), 99) == 4,
              "combo attack clamps to the adapter table");
    }

    void test_fake_outcomes()
    {
        Backend backend;
        check(backend.validate(t3(1.0, 0.0, 5.0)), "fake backend accepts a valid theta");
        check(!backend.validate(t3(1.0, 0.5, std::nan(""))), "fake backend rejects nonfinite theta");
        check(!backend.validate(std::vector<double>{ 1.0, 2.0 }), "fake backend rejects short theta");
        check(backend.schema().size() == 3, "fake backend schema passes through");

        std::vector<tuning::BatchGame> games(6);
        games[0].id = 42;
        games[0].theta_a = t3(2.0, 0.0, 1.0);
        games[0].theta_b = t3(2.0, 0.0, 0.0);
        games[0].seed_a = 11;
        games[0].seed_b = 22;
        games[1].id = 7;
        games[1].theta_a = t3(2.0, 0.0, 0.0);
        games[1].theta_b = t3(2.0, 0.0, 0.0);
        games[1].seed_a = 33;
        games[1].seed_b = 44;
        games[2].id = 7;
        games[2].theta_a = t3(2.0, 0.0, 0.0);
        games[2].theta_b = t3(3.0, 0.0, 0.0);
        games[2].seed_a = 55;
        games[2].seed_b = 66;
        games[3].id = 0;
        games[3].theta_a = t3(1.0, 0.0, 3.0);
        games[3].theta_b = t3(1.0, 0.0, 3.0);
        games[3].seed_a = 77;
        games[3].seed_b = 88;
        games[4].id = 1234567890123ULL;
        games[4].theta_a = t3(1.0, 2.0, 0.0);
        games[4].theta_b = t3(0.0, 0.0, 0.0);
        games[4].seed_a = 99;
        games[4].seed_b = 100;
        games[5].id = 5;
        games[5].theta_a = t3(2.0, 0.0, 0.0);
        games[5].theta_b = t3(2.0, 0.0, 0.0);
        games[5].seed_a = 111;
        games[5].seed_b = 122;

        auto const results = backend.run_games(games, fake_config(2, 5));
        check(results.size() == games.size(), "fake batch returns one outcome per game");
        bool ids_ok = results.size() == games.size();
        for (std::size_t i = 0; i < games.size() && ids_ok; ++i)
        {
            ids_ok = results[i].id == games[i].id;
        }
        check(ids_ok, "fake batch preserves input order and ids including duplicates");

        using WR = tuning::WinReason;
        tuning::GameOutcome const& g0 = results[0];
        check(g0.winner == -1 && g0.reason == WR::BSurvivor && g0.rounds == 1 && !g0.capped
                  && g0.dead_a && !g0.dead_b && g0.app_a == 0.0 && g0.apl_a == 0.0
                  && g0.app_b == 7.0 && g0.apl_b == 7.0 / 2.0,
              "fake immediate death gives survivor win with exact telemetry");

        tuning::GameOutcome const& g1 = results[1];
        check(g1.winner == 0 && g1.reason == WR::CapDraw && g1.rounds == 5 && g1.capped && !g1.dead_a
                  && !g1.dead_b && g1.app_a == 39.0 / 5.0 && g1.app_b == 39.0 / 5.0
                  && g1.apl_a == 39.0 / 10.0 && g1.apl_b == 39.0 / 10.0,
              "fake capped symmetric game draws with exact telemetry");

        tuning::GameOutcome const& g2 = results[2];
        check(g2.winner == 1 && g2.reason == WR::ACapApl && g2.rounds == 5 && g2.capped
                  && g2.app_a == 39.0 / 5.0 && g2.apl_a == 39.0 / 10.0 && g2.app_b == 44.0 / 5.0
                  && g2.apl_b == 44.0 / 15.0,
              "fake capped asymmetric game resolves by apl");

        tuning::GameOutcome const& g3 = results[3];
        check(g3.winner == 0 && g3.reason == WR::BothDeadDraw && g3.rounds == 3 && !g3.capped
                  && g3.dead_a && g3.dead_b && g3.app_a == 6.0 && g3.app_b == 6.0 && g3.apl_a == 6.0
                  && g3.apl_b == 6.0,
              "fake simultaneous death draws on equal apl");

        tuning::GameOutcome const& g4 = results[4];
        check(g4.winner == 1 && g4.reason == WR::ACapApl && g4.rounds == 5 && g4.capped
                  && g4.app_a == 48.0 / 5.0 && g4.apl_a == 48.0 / 5.0 && g4.app_b == 24.0 / 5.0
                  && g4.apl_b == 0.0,
              "fake b2b spin chain and garbage side produce exact telemetry");

        std::vector<tuning::BatchGame> single_round(1);
        single_round[0].id = 5;
        single_round[0].theta_a = t3(2.0, 0.0, 0.0);
        single_round[0].theta_b = t3(2.0, 0.0, 0.0);
        single_round[0].seed_a = 111;
        single_round[0].seed_b = 122;
        auto const single = backend.run_games(single_round, fake_config(2, 1));
        check(single.size() == 1 && single[0].winner == 0 && single[0].reason == WR::CapDraw
                  && single[0].rounds == 1 && single[0].capped && single[0].app_a == 7.0
                  && single[0].apl_a == 7.0 / 2.0,
              "fake single round cap resolves immediately");
    }

    void test_fake_validation_failures()
    {
        Backend backend;
        tuning::RunConfig bad_threads = fake_config(0, 5);
        check(throws_any([&] { (void)backend.run_games({}, bad_threads); }),
              "fake backend rejects nonpositive threads");
        tuning::RunConfig bad_iters = fake_config(1, 5);
        bad_iters.iterations_per_move = 0;
        check(throws_any([&] { (void)backend.run_games({}, bad_iters); }),
              "fake backend rejects zero iteration budget");
        tuning::RunConfig bad_rounds = fake_config(1, 0);
        check(throws_any([&] { (void)backend.run_games({}, bad_rounds); }),
              "fake backend rejects zero max rounds");
        check(backend.run_games({}, fake_config(1, 5)).empty(), "fake backend accepts an empty batch");

        std::vector<tuning::BatchGame> games(1);
        games[0].id = 1;
        games[0].theta_a = t3(1.0, 0.0, 0.0);
        games[0].theta_b = t3(1.0, 0.0, 0.0);
        games[0].seed_a = 1;
        games[0].seed_b = 2;
        auto short_theta = games;
        short_theta[0].theta_a = std::vector<double>{ 1.0, 0.0 };
        check(throws_any([&] { (void)backend.run_games(short_theta, fake_config(1, 5)); }),
              "fake backend rejects a short theta before dispatch");
        auto nan_theta = games;
        nan_theta[0].theta_b = t3(1.0, std::nan(""), 0.0);
        check(throws_any([&] { (void)backend.run_games(nan_theta, fake_config(1, 5)); }),
              "fake backend rejects a nonfinite theta before dispatch");
        auto rejected_theta = games;
        rejected_theta[0].theta_a = t3(1.0, 0.0, -1.0);
        check(throws_any([&] { (void)backend.run_games(rejected_theta, fake_config(1, 5)); }),
              "fake backend surfaces an instance level theta rejection as a batch failure");
        auto throwing_theta = games;
        throwing_theta[0].theta_a = t3(5.0, 9.0, 0.0);
        check(throws_any([&] { (void)backend.run_games(throwing_theta, fake_config(1, 5)); }),
              "fake backend surfaces an adapter exception as a batch failure");
    }

    void test_fake_pair_overlap()
    {
        Backend backend;
        std::vector<tuning::BatchGame> games(1);
        games[0].id = 900;
        games[0].theta_a = t3(0.0, 0.0, 0.0);
        games[0].theta_b = t3(0.0, 0.0, 0.0);
        games[0].seed_a = 1;
        games[0].seed_b = 2;
        rendezvous_reset(2);
        auto const results = backend.run_games(games, fake_config(1, 1));
        rendezvous_disable();
        check(results.size() == 1 && results[0].winner == 0, "rendezvous game completes");
        check(g_rendezvous_met.load(std::memory_order_acquire),
              "both run_move calls of a round execute together through RunPair");
    }

    void test_fake_parallel_games_and_peaks()
    {
        Backend backend(FakeAdapter::make_shared_context());
        std::vector<tuning::BatchGame> games(2);
        for (int g = 0; g < 2; ++g)
        {
            games[static_cast<std::size_t>(g)].id = static_cast<std::uint64_t>(1000 + g);
            games[static_cast<std::size_t>(g)].theta_a = t3(2.0, 0.0, 0.0);
            games[static_cast<std::size_t>(g)].theta_b = t3(2.0, 0.0, 0.0);
            games[static_cast<std::size_t>(g)].seed_a = static_cast<std::uint64_t>(10 + g);
            games[static_cast<std::size_t>(g)].seed_b = static_cast<std::uint64_t>(20 + g);
        }
        tuning::RunConfig const config = fake_config(2, 30);

        rendezvous_reset(4);
        tournament_scheduler::ExecutorConfig executor_config;
        executor_config.game_workers = 2;
        tournament_scheduler::GameExecutor executor(executor_config);
        std::vector<tuning::GameOutcome> outcomes(2);
        std::vector<tournament_scheduler::GameJob> jobs;
        for (int g = 0; g < 2; ++g)
        {
            jobs.push_back(backend.make_game_job(games[static_cast<std::size_t>(g)], config,
                                                 FakeAdapter::make_shared_context(),
                                                 outcomes[static_cast<std::size_t>(g)]));
        }
        auto const raw = executor.run(std::move(jobs));
        rendezvous_disable();
        bool raw_ok = raw.size() == 2;
        for (int g = 0; g < 2; ++g)
        {
            raw_ok = raw_ok && raw[static_cast<std::size_t>(g)] == outcomes[static_cast<std::size_t>(g)].winner;
        }
        check(raw_ok, "custom executor preserves game body outcomes");
        check(g_rendezvous_met.load(std::memory_order_acquire),
              "two games run concurrently with both leaves inside run_move");
        auto const stats = executor.stats();
        check(stats.peak_active_games == 2, "peak active games reaches the worker count");
        check(stats.peak_concurrent_leaves == 4, "peak leaf concurrency reaches two per game");
        check(stats.peak_active_games <= executor.game_worker_count(),
              "active games never exceed the worker cap");
        check(stats.peak_concurrent_leaves <= executor.leaf_limit(),
              "leaf callbacks never exceed the leaf cap");
        check(stats.games_started == 2 && stats.games_completed == 2,
              "every scheduled game starts and completes");
        check(stats.turns_executed == 60, "each round executes exactly one paired turn per game");

        auto const via_backend = backend.run_games(games, config);
        bool matches = via_backend.size() == 2;
        for (int g = 0; g < 2; ++g)
        {
            matches = matches && same_outcome(via_backend[static_cast<std::size_t>(g)],
                                              outcomes[static_cast<std::size_t>(g)]);
        }
        check(matches, "backend batch and custom executor agree on identical games");

        tournament_scheduler::ExecutorConfig tight_config;
        tight_config.game_workers = 2;
        tight_config.leaf_limit = 2;
        tournament_scheduler::GameExecutor tight_executor(tight_config);
        std::vector<tuning::GameOutcome> tight_outcomes(2);
        std::vector<tournament_scheduler::GameJob> tight_jobs;
        for (int g = 0; g < 2; ++g)
        {
            tight_jobs.push_back(backend.make_game_job(games[static_cast<std::size_t>(g)], config,
                                                       FakeAdapter::make_shared_context(),
                                                       tight_outcomes[static_cast<std::size_t>(g)]));
        }
        auto const tight_raw = tight_executor.run(std::move(tight_jobs));
        bool tight_ok = tight_raw.size() == 2;
        for (int g = 0; g < 2; ++g)
        {
            tight_ok = tight_ok && same_outcome(tight_outcomes[static_cast<std::size_t>(g)],
                                                outcomes[static_cast<std::size_t>(g)]);
        }
        check(tight_ok, "a tight leaf limit still yields identical deterministic outcomes");
        check(tight_executor.stats().peak_concurrent_leaves <= 2,
              "tight leaf limit peak never exceeds the cap");
    }

    void test_fake_worker_count_determinism()
    {
        Backend backend;
        std::vector<tuning::BatchGame> games(6);
        games[0].id = 1;
        games[0].theta_a = t3(2.0, 0.0, 1.0);
        games[0].theta_b = t3(2.0, 0.0, 0.0);
        games[1].id = 2;
        games[1].theta_a = t3(2.0, 0.0, 0.0);
        games[1].theta_b = t3(3.0, 0.0, 0.0);
        games[2].id = 3;
        games[2].theta_a = t3(1.0, 2.0, 0.0);
        games[2].theta_b = t3(0.0, 0.0, 0.0);
        games[3].id = 4;
        games[3].theta_a = t3(1.0, 0.0, 3.0);
        games[3].theta_b = t3(1.0, 0.0, 3.0);
        games[4].id = 5;
        games[4].theta_a = t3(4.0, 2.0, 0.0);
        games[4].theta_b = t3(0.0, 0.0, 0.0);
        games[5].id = 6;
        games[5].theta_a = t3(0.0, 0.0, 0.0);
        games[5].theta_b = t3(2.0, 0.0, 0.0);
        for (int g = 0; g < 6; ++g)
        {
            games[static_cast<std::size_t>(g)].seed_a = static_cast<std::uint64_t>(100 + g);
            games[static_cast<std::size_t>(g)].seed_b = static_cast<std::uint64_t>(200 + g);
        }
        auto const single = backend.run_games(games, fake_config(1, 8));
        auto const triple = backend.run_games(games, fake_config(3, 8));
        auto const repeat = backend.run_games(games, fake_config(3, 8));
        bool same = single.size() == 6 && triple.size() == 6 && repeat.size() == 6;
        for (std::size_t i = 0; i < 6 && same; ++i)
        {
            same = same_outcome(single[i], triple[i]) && same_outcome(single[i], repeat[i]);
        }
        check(same, "fake outcomes are identical across worker counts and reruns");
    }

#ifndef TUNING_ENGINE_MATCH_TEST_SKIP_TOJ
    void test_toj_scenario_equivalence()
    {
        bool pieces_match = true;
        bool holes_match = true;
        for (std::uint64_t seed : { 0ULL, 1ULL, 555ULL, 0xDEADBEEFULL, 0x123456789ABCDEF0ULL })
        {
            auto mine = tuning::make_game_scenario(seed, 64, 6);
            auto theirs = tuner_toj::Tuner::make_scenario(seed, 64, 6);
            pieces_match = pieces_match && mine.pieces.size() == theirs.pieces.size();
            for (std::size_t i = 0; i < mine.pieces.size() && pieces_match; ++i)
            {
                pieces_match = mine.pieces[i] == theirs.pieces[i];
            }
            for (int round = 1; round <= 64 && holes_match; ++round)
            {
                tuning::begin_game_round(mine, mine, round);
                tuner_toj::Tuner::begin_round(theirs, theirs, round);
                for (int packet = 0; packet < 8; ++packet)
                {
                    holes_match = holes_match
                        && tuning::game_scenario_hole(mine) == tuner_toj::Tuner::scenario_hole(theirs);
                    ++mine.packet_index;
                    ++theirs.packet_index;
                }
            }
        }
        check(pieces_match, "generic seven bag scenario matches the tuner scenario exactly");
        check(holes_match, "generic garbage hole stream matches the tuner scenario exactly");
    }

    void test_toj_parity()
    {
        tuning_toj::TojMatchBackend reference;
        using TojEngineBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;
        auto const adapter_schema = tuning_toj::TojAdapter::schema();
        std::vector<double> const v_prod(adapter_schema.defaults.begin(), adapter_schema.defaults.end());
        TojEngineBackend mine(tuning_toj::TojAdapter::make_shared_context());
        check(mine.schema().adapter_id == tuning_toj::TojAdapter::kAdapterId,
              "toj backend reports the adapter schema");
        check(mine.validate(v_prod), "toj backend accepts the production defaults");

        std::vector<double> v_x = v_prod;
        v_x[0] += 0.37;
        v_x[9] -= 0.5;
        v_x[28] += 0.05;
        std::vector<double> v_y = v_prod;
        v_y[12] += 0.25;
        v_y[33] += 0.03;
        check(mine.validate(v_x) && mine.validate(v_y), "toj perturbed vectors validate");

        std::vector<tuning::BatchGame> games(6);
        games[0].id = 1;
        games[0].theta_a = v_prod;
        games[0].theta_b = v_prod;
        games[0].seed_a = 11;
        games[0].seed_b = 22;
        games[1].id = 2;
        games[1].theta_a = v_x;
        games[1].theta_b = v_prod;
        games[1].seed_a = 101;
        games[1].seed_b = 102;
        games[2].id = 3;
        games[2].theta_a = v_prod;
        games[2].theta_b = v_x;
        games[2].seed_a = 101;
        games[2].seed_b = 102;
        games[3].id = 4;
        games[3].theta_a = v_x;
        games[3].theta_b = v_y;
        games[3].seed_a = 7;
        games[3].seed_b = 8;
        games[4].id = 5;
        games[4].theta_a = v_y;
        games[4].theta_b = v_x;
        games[4].seed_a = 7;
        games[4].seed_b = 8;
        games[5].id = 6;
        games[5].theta_a = v_x;
        games[5].theta_b = v_y;
        games[5].seed_a = 0xDEADULL;
        games[5].seed_b = 0xBEEFULL;

        tuning::RunConfig config;
        config.threads = 3;
        config.iterations_per_move = 100;
        config.max_rounds = 300;
        auto const mine_out = mine.run_games(games, config);
        tuning::RunConfig ref_config = config;
        ref_config.threads = 2;
        auto const ref_out = reference.run_games(games, ref_config);
        bool parity = mine_out.size() == games.size() && ref_out.size() == games.size();
        for (std::size_t i = 0; i < games.size() && parity; ++i)
        {
            parity = same_outcome(mine_out[i], ref_out[i]);
            if (!parity)
            {
                std::printf("      mine: %s\n      ref:  %s\n", describe(mine_out[i]).c_str(),
                            describe(ref_out[i]).c_str());
            }
        }
        check(parity, "toj backend matches the reference backend on identical seeds vectors and budgets");

        tuning::RunConfig heavy;
        heavy.threads = 2;
        heavy.iterations_per_move = 300;
        heavy.max_rounds = 800;
        std::vector<tuning::BatchGame> subset(games.begin(), games.begin() + 2);
        auto const mine_heavy = mine.run_games(subset, heavy);
        auto const ref_heavy = reference.run_games(subset, heavy);
        bool heavy_parity = mine_heavy.size() == 2 && ref_heavy.size() == 2;
        for (std::size_t i = 0; i < 2 && heavy_parity; ++i)
        {
            heavy_parity = same_outcome(mine_heavy[i], ref_heavy[i]);
            if (!heavy_parity)
            {
                std::printf("      mine: %s\n      ref:  %s\n", describe(mine_heavy[i]).c_str(),
                            describe(ref_heavy[i]).c_str());
            }
        }
        check(heavy_parity, "toj parity holds at a larger iteration budget and round cap");

        tuning::RunConfig single_thread = config;
        single_thread.threads = 1;
        auto const mine_single = mine.run_games(games, single_thread);
        bool deterministic = mine_single.size() == games.size();
        for (std::size_t i = 0; i < games.size() && deterministic; ++i)
        {
            deterministic = same_outcome(mine_single[i], mine_out[i]);
        }
        check(deterministic, "toj outcomes are identical across worker counts");
    }
#endif
}

int main()
{
    test_fake_scenario_primitives();
    test_fake_outcomes();
    test_fake_validation_failures();
    test_fake_pair_overlap();
    test_fake_parallel_games_and_peaks();
    test_fake_worker_count_determinism();
#ifndef TUNING_ENGINE_MATCH_TEST_SKIP_TOJ
    test_toj_scenario_equivalence();
    test_toj_parity();
#endif
    if (failures == 0)
    {
        std::printf("ALL TUNING ENGINE MATCH TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TUNING ENGINE MATCH TEST(S) FAILED\n", failures);
    return 1;
}
