#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <print>
#include <random>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "ai_zzz.h"
#include "rule_toj.h"
#include "search_tspin.h"
#include "tetris_core.h"
#include "tuner_toj.h"

namespace tuner_match
{
    inline constexpr size_t NUM_PARAMS = 29;
    inline constexpr int next_length = tuner_toj::Tuner::NEXT_LENGTH;

    inline constexpr double const (&param_scale)[NUM_PARAMS] = tuner_toj::Tuner::param_scale;
    inline char const *const (&param_names)[NUM_PARAMS] = tuner_toj::Tuner::param_names;
    inline constexpr int const (&combo_table)[tuner_toj::Tuner::combo_table_max] = tuner_toj::Tuner::combo_table;
    inline constexpr int combo_table_max = tuner_toj::Tuner::combo_table_max;

    using Scenario = tuner_toj::Tuner::Scenario;
    inline void begin_round(Scenario &s1, Scenario &s2, int round)
    {
        tuner_toj::Tuner::begin_round(s1, s2, round);
    }
    inline int scenario_hole(Scenario const &s)
    {
        return tuner_toj::Tuner::scenario_hole(s);
    }
    inline Scenario make_scenario(uint64_t seed, size_t max_rounds, size_t next_len)
    {
        return tuner_toj::Tuner::make_scenario(seed, max_rounds, next_len);
    }
    inline uint64_t splitmix64(uint64_t x)
    {
        return tuner_toj::Tuner::splitmix64(x);
    }
    inline int rademacher(uint64_t seed, int k, int j, int i)
    {
        return tuner_toj::Tuner::rademacher(seed, k, j, i);
    }

    using TunerEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

    inline void param_to_array(ai_zzz::TOJ::Param const &p, double *out)
    {
        static_assert(NUM_PARAMS == ai_zzz::TOJ::NUM_PARAMS,
                      "parameter count mismatch between tuner_match and ai_zzz::TOJ");
        ai_zzz::TOJ::theta_from_param(p, out);
    }

    inline void array_to_param(double const *in, ai_zzz::TOJ::Param &p)
    {
        ai_zzz::TOJ::theta_to_param(in, p);
    }

    inline void production_default_theta(double *out)
    {
        static_assert(NUM_PARAMS == ai_zzz::TOJ::NUM_PARAMS,
                      "parameter count mismatch between tuner_match and ai_zzz::TOJ");
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            out[i] = ai_zzz::TOJ::kProductionDefaultTheta[i];
        }
    }

    inline bool read_theta_strict(std::string const &path, double *out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f.good())
        {
            return false;
        }
        std::streamoff const size = f.tellg();
        if (size != static_cast<std::streamoff>(NUM_PARAMS * sizeof(double)))
        {
            return false;
        }
        f.seekg(0, std::ios::beg);
        f.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(size));
        if (!f.good())
        {
            return false;
        }
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            if (!std::isfinite(out[i]))
            {
                return false;
            }
        }
        return true;
    }

    struct BotInstance
    {
        TunerEngine ai;
        m_tetris::TetrisMap map;
        Scenario *scenario = nullptr;

        m_tetris::SearchBudget search_budget{ 20 };
        int last_clear = 0;
        std::vector<char> next;
        std::deque<int> recv_attack;
        int send_attack = 0;
        int combo = 0;
        int b2b = 0;
        char hold = ' ';
        bool dead = false;

        int total_block = 0;
        int total_clear = 0;
        int total_attack = 0;
        int total_receive = 0;

        BotInstance()
        {
            ai.prepare(10, 40);
            ai.memory_limit(256ull << 20);
            ai.search_config()->allow_rotate_move = false;
            ai.search_config()->allow_180 = true;
            ai.search_config()->allow_d = true;
            ai.search_config()->is_20g = false;
            ai.search_config()->last_rotate = false;
            ai.ai_config()->table = combo_table;
            ai.ai_config()->table_max = combo_table_max;
        }

        void init(double const *params)
        {
            map = m_tetris::TetrisMap(10, 40);
            array_to_param(params, ai.ai_config()->param);
            next.clear();
            recv_attack.clear();
            send_attack = 0;
            combo = 0;
            b2b = 0;
            hold = ' ';
            dead = false;
            total_block = 0;
            total_clear = 0;
            total_attack = 0;
            total_receive = 0;
            last_clear = 0;
        }

        void init_status()
        {
            ai.ai_config()->safe = ai.ai()->get_safe(map, next.front());
            ai.status()->death = 0;
            ai.status()->combo = combo;
            ai.status()->under_attack = static_cast<int>(std::accumulate(recv_attack.begin(), recv_attack.end(), 0));
            ai.status()->map_rise = 0;
            ai.status()->b2b = !!b2b;
            ai.status()->acc_value = 0;
            ai.status()->like = 0;
            ai.status()->value = 0;
            ai_zzz::TOJ::Status::init_t_value(map, ai.status()->t2_value, ai.status()->t3_value);
        }

        void prepare()
        {
            if (!next.empty())
            {
                next.erase(next.begin());
            }
            while (next.size() <= static_cast<size_t>(next_length))
            {
                assert(!scenario->pieces.empty());
                next.push_back(scenario->pieces.front());
                scenario->pieces.pop_front();
            }
        }

        void run()
        {
            init_status();

            char current = next.front();
            bool is_hold_piece = hold != ' ' && current == hold;
            auto result = ai.run_hold(map, ai.spawn_node(current, last_clear, is_hold_piece, map), hold, true,
                                      next.data() + 1, next_length, search_budget);
            if (result.target == nullptr || result.target->row >= 20)
            {
                dead = true;
                return;
            }
            if (result.change_hold)
            {
                if (hold == ' ')
                {
                    next.erase(next.begin());
                }
                hold = current;
            }

            int clear = result.target->attach(ai.context().get(), map);
            total_clear += clear;
            last_clear = clear;

            auto get_combo_attack = [&](int c)
            {
                return combo_table[std::min(combo_table_max - 1, c)];
            };
            int attack = 0;
            switch (clear)
            {
            case 0:
                combo = 0;
                break;
            case 1:
                if (result.target.type == ai_zzz::TOJ::TSpinType::TSpinMini)
                {
                    attack += 1 + b2b;
                    b2b = 1;
                }
                else if (result.target.type == ai_zzz::TOJ::TSpinType::TSpin)
                {
                    attack += 2 + b2b;
                    b2b = 1;
                }
                else
                {
                    b2b = 0;
                }
                attack += get_combo_attack(++combo);
                break;
            case 2:
                if (result.target.type != ai_zzz::TOJ::TSpinType::None)
                {
                    attack += 4 + b2b;
                    b2b = 1;
                }
                else
                {
                    attack += 1;
                    b2b = 0;
                }
                attack += get_combo_attack(++combo);
                break;
            case 3:
                if (result.target.type != ai_zzz::TOJ::TSpinType::None)
                {
                    attack += 6 + b2b * 2;
                    b2b = 1;
                }
                else
                {
                    attack += 2;
                    b2b = 0;
                }
                attack += get_combo_attack(++combo);
                break;
            case 4:
                attack += get_combo_attack(++combo) + 4 + b2b;
                b2b = 1;
                break;
            }
            if (map.count == 0)
            {
                attack += 6;
            }

            ++total_block;
            total_attack += attack;
            send_attack = attack;

            while (!recv_attack.empty())
            {
                if (send_attack > 0)
                {
                    if (recv_attack.front() <= send_attack)
                    {
                        send_attack -= recv_attack.front();
                        recv_attack.pop_front();
                        continue;
                    }
                    recv_attack.front() -= send_attack;
                    send_attack = 0;
                }
                if (send_attack > 0 || combo > 0)
                {
                    break;
                }
                int line = recv_attack.front();
                total_receive += line;
                recv_attack.pop_front();

                for (int y = map.height - 1; y >= line; --y)
                {
                    map.row[y] = map.row[y - line];
                }
                uint32_t hole = 1u << scenario_hole(*scenario);
                ++scenario->packet_index;
                uint32_t garbage_row = ai.context()->full() & ~hole;
                for (int y = 0; y < line; ++y)
                {
                    map.row[y] = garbage_row;
                }
                map.count = 0;
                map.roof = 0;
                for (int my = 0; my < map.height; ++my)
                {
                    for (int mx = 0; mx < map.width; ++mx)
                    {
                        if (map.full(mx, my))
                        {
                            map.top[mx] = map.roof = my + 1;
                            ++map.count;
                        }
                    }
                }
            }
        }

        void under_attack(int line)
        {
            if (line > 0)
            {
                recv_attack.emplace_back(line);
            }
        }
    };

    inline void render_view(BotInstance &b1, BotInstance &b2, char const *name1, char const *name2)
    {
        m_tetris::TetrisMap m1 = b1.map;
        m_tetris::TetrisMap m2 = b2.map;
        if (!b1.next.empty())
        {
            auto n1 = b1.ai.context()->generate(b1.next.front());
            if (n1)
            {
                m_tetris::TetrisMap tmp = b1.map;
                n1->attach(b1.ai.context().get(), tmp);
                m1 = tmp;
            }
        }
        if (!b2.next.empty())
        {
            auto n2 = b2.ai.context()->generate(b2.next.front());
            if (n2)
            {
                m_tetris::TetrisMap tmp = b2.map;
                n2->attach(b2.ai.context().get(), tmp);
                m2 = tmp;
            }
        }

        std::print("\x1b[H\x1b[2J");
        int up1 = std::accumulate(b1.recv_attack.begin(), b1.recv_attack.end(), 0);
        int up2 = std::accumulate(b2.recv_attack.begin(), b2.recv_attack.end(), 0);
        std::println(
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {}\n"
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {}",
            b1.hold, std::string(b1.next.begin() + 1, b1.next.begin() + 1 + next_length),
            b1.combo, b1.b2b, up1, b1.total_block, b1.total_clear, b1.total_attack,
            b1.total_clear ? static_cast<double>(b1.total_attack) / b1.total_clear : 0.0,
            b1.total_block ? static_cast<double>(b1.total_attack) / b1.total_block : 0.0, name1,
            b2.hold, std::string(b2.next.begin() + 1, b2.next.begin() + 1 + next_length),
            b2.combo, b2.b2b, up2, b2.total_block, b2.total_clear, b2.total_attack,
            b2.total_clear ? static_cast<double>(b2.total_attack) / b2.total_clear : 0.0,
            b2.total_block ? static_cast<double>(b2.total_attack) / b2.total_block : 0.0, name2);
        for (int y = 21; y >= 0; --y)
        {
            for (int x = 0; x < 10; ++x)
            {
                std::print("{}", m1.full(x, y) ? "[]" : "  ");
            }
            std::print("  ");
            for (int x = 0; x < 10; ++x)
            {
                std::print("{}", m2.full(x, y) ? "[]" : "  ");
            }
            std::println("");
        }
        std::fflush(stdout);
    }

    struct MatchResult
    {
        enum WinnerReason
        {
            P1_SURVIVOR = 1,
            P2_SURVIVOR = 2,
            P1_CAP_APL = 3,
            P2_CAP_APL = 4,
            P1_BOTH_DEAD_APL = 5,
            P2_BOTH_DEAD_APL = 6,
            BOTH_DEAD_DRAW = 7,
            CAP_DRAW = 8,
        };
        int winner;
        bool dead1;
        bool dead2;
        bool capped;
        int winner_reason;
        int rounds;
        double app1, app2;
        double apl1, apl2;
    };

    inline void run_round(BotInstance &b1, BotInstance &b2)
    {
        b1.run();
        b2.run();
    }

    template<class F1, class F2>
    inline void run_pair_parallel(F1 &&f1, F2 &&f2, std::counting_semaphore<> &permits)
    {
        if (permits.try_acquire())
        {
            std::thread helper(std::forward<F2>(f2));
            f1();
            helper.join();
            permits.release();
        }
        else
        {
            f1();
            f2();
        }
    }

    inline void run_round_parallel(BotInstance &b1, BotInstance &b2, std::counting_semaphore<> &permits)
    {
        run_pair_parallel([&b1] { b1.run(); }, [&b2] { b2.run(); }, permits);
    }

    inline MatchResult play_match(BotInstance &b1, BotInstance &b2, int max_rounds = 1000,
                                  std::function<void()> view_cb = nullptr,
                                  std::counting_semaphore<> *bot_permits = nullptr)
    {
        int played_rounds = 0;
        if (bot_permits != nullptr)
        {
            bot_permits->acquire();
        }
        for (int round = 1; round <= max_rounds; ++round)
        {
            begin_round(*b1.scenario, *b2.scenario, round);
            b1.prepare();
            b2.prepare();
            if (view_cb)
            {
                view_cb();
            }
            if (bot_permits != nullptr)
            {
                run_round_parallel(b1, b2, *bot_permits);
            }
            else
            {
                run_round(b1, b2);
            }
            ++played_rounds;
            if (b1.dead || b2.dead)
            {
                break;
            }

            int min_attack = std::min(b1.send_attack, b2.send_attack);
            b1.send_attack -= min_attack;
            b2.send_attack -= min_attack;
            b1.under_attack(b2.send_attack);
            b2.under_attack(b1.send_attack);
        }
        if (bot_permits != nullptr)
        {
            bot_permits->release();
        }

        MatchResult r;
        r.dead1 = b1.dead;
        r.dead2 = b2.dead;
        r.capped = !b1.dead && !b2.dead;
        r.rounds = played_rounds;
        r.app1 = b1.total_block > 0 ? static_cast<double>(b1.total_attack) / b1.total_block : 0.0;
        r.app2 = b2.total_block > 0 ? static_cast<double>(b2.total_attack) / b2.total_block : 0.0;
        r.apl1 = b1.total_clear > 0 ? static_cast<double>(b1.total_attack) / b1.total_clear : 0.0;
        r.apl2 = b2.total_clear > 0 ? static_cast<double>(b2.total_attack) / b2.total_clear : 0.0;
        if (b1.dead && !b2.dead)
        {
            r.winner = -1;
            r.winner_reason = MatchResult::P2_SURVIVOR;
        }
        else if (b2.dead && !b1.dead)
        {
            r.winner = +1;
            r.winner_reason = MatchResult::P1_SURVIVOR;
        }
        else if (r.apl1 > r.apl2)
        {
            r.winner = +1;
            r.winner_reason = r.capped ? MatchResult::P1_CAP_APL : MatchResult::P1_BOTH_DEAD_APL;
        }
        else if (r.apl2 > r.apl1)
        {
            r.winner = -1;
            r.winner_reason = r.capped ? MatchResult::P2_CAP_APL : MatchResult::P2_BOTH_DEAD_APL;
        }
        else
        {
            r.winner = 0;
            r.winner_reason = r.capped ? MatchResult::CAP_DRAW : MatchResult::BOTH_DEAD_DRAW;
        }
        return r;
    }

    struct MatchJob
    {
        double p1[NUM_PARAMS];
        double p2[NUM_PARAMS];
        uint64_t scenario_seed_p1;
        uint64_t scenario_seed_p2;
        size_t job_id;
        int budget_iters_p1 = -1;
        int budget_iters_p2 = -1;
        int budget_ms_p1 = -1;
        int budget_ms_p2 = -1;
    };

    struct MatchOutcome
    {
        int winner;
        bool dead1;
        bool dead2;
        bool capped;
        int winner_reason;
        int rounds;
        double app1, app2;
        double apl1, apl2;
    };

    inline double paired_reward(MatchOutcome const &a, MatchOutcome const &b)
    {
        return 0.5 * (a.winner - b.winner);
    }

    inline std::vector<MatchOutcome> run_batch(std::vector<MatchJob> const &jobs, int threads,
                                               int default_iters, int default_ms, int max_rounds,
                                               std::atomic<bool> &view, std::mutex &view_mutex,
                                               std::atomic<uint32_t> &view_index)
    {
        std::vector<MatchOutcome> out(jobs.size());
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
                Scenario scenario_p1 = make_scenario(jobs[idx].scenario_seed_p1, static_cast<size_t>(max_rounds), static_cast<size_t>(next_length));
                Scenario scenario_p2 = make_scenario(jobs[idx].scenario_seed_p2, static_cast<size_t>(max_rounds), static_cast<size_t>(next_length));
                BotInstance b1, b2;
                b1.scenario = &scenario_p1;
                b2.scenario = &scenario_p2;
                auto budget_of = [&](int iters, int ms)
                {
                    if (iters > 0)
                    {
                        return m_tetris::SearchBudget::by_iterations(static_cast<size_t>(iters));
                    }
                    if (ms > 0)
                    {
                        return m_tetris::SearchBudget{ static_cast<time_t>(ms) };
                    }
                    if (default_iters > 0)
                    {
                        return m_tetris::SearchBudget::by_iterations(static_cast<size_t>(default_iters));
                    }
                    return m_tetris::SearchBudget{ static_cast<time_t>(default_ms) };
                };
                b1.search_budget = budget_of(jobs[idx].budget_iters_p1, jobs[idx].budget_ms_p1);
                b2.search_budget = budget_of(jobs[idx].budget_iters_p2, jobs[idx].budget_ms_p2);
                b1.init(jobs[idx].p1);
                b2.init(jobs[idx].p2);
                uint32_t claim = static_cast<uint32_t>(idx) + 1;
                std::function<void()> view_cb = [&, idx, claim]() mutable
                {
                    if (view.load(std::memory_order_relaxed)
                        && view_index.load(std::memory_order_relaxed) == 0)
                    {
                        std::lock_guard<std::mutex> lock(view_mutex);
                        if (view.load(std::memory_order_relaxed)
                            && view_index.load(std::memory_order_relaxed) == 0)
                        {
                            view_index.store(claim, std::memory_order_relaxed);
                        }
                    }
                    if (view_index.load(std::memory_order_relaxed) != claim)
                    {
                        return;
                    }
                    render_view(b1, b2, "PLUS", "MINUS");
                };
                MatchResult r = play_match(b1, b2, max_rounds, view_cb, permits.get());
                {
                    std::lock_guard<std::mutex> lock(view_mutex);
                    if (view_index.load(std::memory_order_relaxed) == claim)
                    {
                        view_index.store(0, std::memory_order_relaxed);
                    }
                }
                out[idx].winner = r.winner;
                out[idx].dead1 = r.dead1;
                out[idx].dead2 = r.dead2;
                out[idx].capped = r.capped;
                out[idx].winner_reason = r.winner_reason;
                out[idx].rounds = r.rounds;
                out[idx].app1 = r.app1;
                out[idx].app2 = r.app2;
                out[idx].apl1 = r.apl1;
                out[idx].apl2 = r.apl2;
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
}
