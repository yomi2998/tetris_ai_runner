#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <print>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "tetris_core.h"
#include "rule_toj.h"
#include "search_tspin.h"
#include "ai_zzz.h"

using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;
using Budget = m_tetris::SearchBudget;

namespace
{
    constexpr size_t next_length = 6;
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    constexpr uint64_t kDefaultLimit = 128ull * kMiB;
    constexpr uint64_t kLowLimit = 32ull * kMiB;
    constexpr uint64_t kStepSlack = 4ull * kMiB;
    constexpr size_t kHugeIterations = size_t(1) << 26;

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    constexpr int combo_table_max = 10;

    struct Bag
    {
        std::mt19937 rng;
        std::deque<char> pieces;

        explicit Bag(unsigned seed) : rng(seed)
        {
        }
        void fill(size_t n)
        {
            std::string bag = "IJLOSTZ";
            while (pieces.size() < n)
            {
                std::shuffle(bag.begin(), bag.end(), rng);
                for (char c : bag)
                {
                    pieces.push_back(c);
                }
            }
        }
        char pop()
        {
            char c = pieces.front();
            pieces.pop_front();
            return c;
        }
    };

    Engine make_engine()
    {
        Engine engine;
        engine.prepare(10, 40);
        engine.search_config()->allow_rotate_move = false;
        engine.search_config()->allow_180 = true;
        engine.search_config()->allow_d = true;
        engine.search_config()->is_20g = false;
        engine.search_config()->last_rotate = false;
        engine.ai_config()->table = combo_table;
        engine.ai_config()->table_max = combo_table_max;
        double theta[ai_zzz::TOJ::NUM_PARAMS];
        ai_zzz::TOJ::production_default_theta(theta);
        ai_zzz::TOJ::theta_to_param(theta, engine.ai_config()->param);
        return engine;
    }

    struct Bot
    {
        Engine &engine;
        m_tetris::TetrisMap map;
        std::vector<char> next;
        char hold = ' ';
        int last_clear = 0;
        int combo = 0;
        bool b2b = false;
        bool dead = false;

        Bot(Engine &e, Bag &bag) : engine(e), map(10, 40)
        {
            bag.fill(16);
            next.push_back(bag.pop());
            for (size_t i = 0; i < next_length; ++i)
            {
                next.push_back(bag.pop());
            }
        }
        void prepare(Bag &bag)
        {
            next.erase(next.begin());
            while (next.size() <= next_length)
            {
                bag.fill(8);
                next.push_back(bag.pop());
            }
        }
        void init_status()
        {
            engine.ai_config()->safe = engine.ai()->get_safe(map, next.front());
            engine.status()->death = 0;
            engine.status()->combo = combo;
            engine.status()->under_attack = 0;
            engine.status()->map_rise = 0;
            engine.status()->combo_debt = 0;
            engine.status()->just_attacked = 0;
            engine.status()->since_attack = 0;
            engine.status()->b2b = b2b;
            engine.status()->acc_value = 0;
            engine.status()->like = 0;
            engine.status()->value = 0;
            ai_zzz::TOJ::Status::init_t_value(map, engine.status()->t2_value, engine.status()->t3_value);
        }
        bool move(Bag &bag, Budget budget, bool with_hold)
        {
            prepare(bag);
            init_status();
            char current = next.front();
            bool is_hold = hold != ' ' && current == hold;
            auto result = with_hold
                ? engine.run_hold(map, engine.spawn_node(current, last_clear, is_hold, map), hold, true,
                                  next.data() + 1, next_length, budget)
                : engine.run(map, engine.spawn_node(current, last_clear, is_hold, map),
                             next.data() + 1, next_length, budget);
            if (result.target == nullptr || result.target->row >= 20)
            {
                dead = true;
                return false;
            }
            if (result.change_hold)
            {
                if (hold == ' ')
                {
                    next.erase(next.begin());
                }
                hold = current;
            }
            int clear = result.target->attach(engine.context().get(), map);
            last_clear = clear;
            combo = clear > 0 ? combo + 1 : 0;
            b2b = clear >= 4;
            return true;
        }
    };

    struct Record
    {
        bool moved = false;
        double ms = 0.0;
        uint64_t usage = 0;
    };

    Record play_one(Engine &engine, Bot &bot, Bag &bag, Budget budget, bool with_hold)
    {
        Record rec;
        auto t0 = std::chrono::steady_clock::now();
        rec.moved = bot.move(bag, budget, with_hold);
        auto t1 = std::chrono::steady_clock::now();
        rec.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        rec.usage = engine.memory_usage();
        return rec;
    }

    struct SelfCheck
    {
        int failures = 0;
        void check(bool cond, std::string const &name)
        {
            std::println("{}: {}", cond ? "PASS" : "FAIL", name);
            if (!cond)
            {
                ++failures;
            }
        }
    };
}

int main()
{
    SelfCheck sc;

    {
        Engine engine = make_engine();
        sc.check(engine.memory_limit() == kDefaultLimit, "default memory limit is 128 MiB");
        engine.memory_limit(1);
        sc.check(engine.memory_limit() == kLowLimit, "memory limit clamps to a 32 MiB floor");
    }

    {
        Engine engine = make_engine();
        Bag bag(12345);
        Bot bot(engine, bag);
        bool all_moved = true;
        bool under_limit = true;
        for (int i = 0; i < 3; ++i)
        {
            Record rec = play_one(engine, bot, bag, Budget::by_iterations(64), true);
            all_moved = all_moved && rec.moved;
            under_limit = under_limit && rec.usage < kDefaultLimit && rec.ms < 5000.0;
        }
        sc.check(all_moved, "run_hold with an iteration budget under the default limit returns moves");
        sc.check(under_limit, "run_hold under the default limit keeps memory under the default limit");
    }

    {
        Engine engine = make_engine();
        engine.memory_limit(kLowLimit);
        Bag bag(12345);
        Bot bot(engine, bag);
        bool all_moved = true;
        bool bounded = true;
        bool cap_reached = false;
        for (int i = 0; i < 5; ++i)
        {
            Record rec = play_one(engine, bot, bag, Budget::by_iterations(kHugeIterations), true);
            all_moved = all_moved && rec.moved;
            bounded = bounded && rec.ms < 20000.0 && rec.usage < kLowLimit + kStepSlack;
            cap_reached = cap_reached || rec.usage >= kLowLimit;
        }
        sc.check(all_moved, "run_hold with a huge iteration budget and a 32 MiB cap still returns moves");
        sc.check(bounded, "run_hold stops searching at the 32 MiB cap under a huge iteration budget");
        sc.check(cap_reached, "run_hold iteration-budget game actually reached the 32 MiB cap");
    }

    {
        Engine engine = make_engine();
        engine.memory_limit(kLowLimit);
        Bag bag(12345);
        Bot bot(engine, bag);
        Record rec = play_one(engine, bot, bag, Budget(time_t(60000)), true);
        sc.check(rec.moved, "run_hold with a huge time budget and a 32 MiB cap still returns a move");
        sc.check(rec.ms < 20000.0 && rec.usage < kLowLimit + kStepSlack,
                 "run_hold stops searching at the 32 MiB cap under a huge time budget");
        sc.check(rec.usage >= kLowLimit, "run_hold time-budget game actually reached the 32 MiB cap");
    }

    {
        Engine engine = make_engine();
        Bag bag(12345);
        Bot bot(engine, bag);
        Record rec = play_one(engine, bot, bag, Budget(time_t(40)), true);
        sc.check(rec.moved && rec.usage < kDefaultLimit,
                 "run_hold with a small time budget under the default limit returns a move");
    }

    {
        Engine engine = make_engine();
        engine.memory_limit(kLowLimit);
        Bag bag(12345);
        Bot bot(engine, bag);
        Record rec = play_one(engine, bot, bag, Budget::by_iterations(kHugeIterations), false);
        sc.check(rec.moved, "run with a huge iteration budget and a 32 MiB cap still returns a move");
        sc.check(rec.ms < 20000.0 && rec.usage < kLowLimit + kStepSlack && rec.usage >= kLowLimit,
                 "run stops searching at the 32 MiB cap under a huge iteration budget");
    }

    {
        Engine engine = make_engine();
        Bag bag(12345);
        Bot bot(engine, bag);
        Record rec = play_one(engine, bot, bag, Budget::by_iterations(1000), false);
        sc.check(rec.moved && rec.usage > kLowLimit + 32ull * kMiB,
                 "the same run workload grows far past 32 MiB when the default limit applies");
    }

    {
        auto run_capped = []()
        {
            Engine engine = make_engine();
            engine.memory_limit(kLowLimit);
            Bag bag(12345);
            Bot bot(engine, bag);
            std::vector<std::tuple<char, int, int, int>> landings;
            for (int i = 0; i < 5; ++i)
            {
                bot.prepare(bag);
                bot.init_status();
                char current = bot.next.front();
                bool is_hold = bot.hold != ' ' && current == bot.hold;
                auto result = engine.run_hold(bot.map, engine.spawn_node(current, bot.last_clear, is_hold, bot.map),
                                              bot.hold, true, bot.next.data() + 1, next_length,
                                              Budget::by_iterations(kHugeIterations));
                if (result.target == nullptr || result.target->row >= 20)
                {
                    break;
                }
                landings.emplace_back(result.target->status.t, result.target->status.x,
                                      result.target->status.y, result.target->status.r);
                if (result.change_hold)
                {
                    if (bot.hold == ' ')
                    {
                        bot.next.erase(bot.next.begin());
                    }
                    bot.hold = current;
                }
                int clear = result.target->attach(engine.context().get(), bot.map);
                bot.last_clear = clear;
                bot.combo = clear > 0 ? bot.combo + 1 : 0;
                bot.b2b = clear >= 4;
            }
            return landings;
        };
        std::vector<std::tuple<char, int, int, int>> a = run_capped();
        std::vector<std::tuple<char, int, int, int>> b = run_capped();
        sc.check(a.size() >= 3 && a == b, "capped run_hold play is deterministic across repeated fixtures");
    }

    std::println("");
    if (sc.failures == 0)
    {
        std::println("ALL MEMORY BUDGET CHECKS PASSED");
        return 0;
    }
    std::println("{} MEMORY BUDGET CHECK(S) FAILED", sc.failures);
    return 1;
}
