// Usage:
//   spsa [num_iters] [eval_matches] [search_ms] [seed] [threads]

#include <ctime>
#include <cstring>
#include <fstream>
#include <thread>
#include <random>
#include <iostream>
#include <chrono>
#include <deque>
#include <atomic>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <functional>

#include "tetris_core.h"
#include "search_tspin.h"
#include "ai_zzz.h"
#include "rule_toj.h"
#include "param.h"

static int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5 };
static int const combo_table_max = 13;

static double const param_step[] = {
    10.0, // base
    10.0, // roof
    10.0, // col_trans
    10.0, // row_trans
    10.0, // hole_count
    10.0, // hole_line
    10.0, // clear_width
    10.0, // wide_2
    10.0, // wide_3
    10.0, // wide_4
    10.0, // safe
    50.0, // b2b
    50.0, // attack
     0.1, // hold_t
     0.1, // hold_i
     5.0, // waste_t
     5.0, // waste_i
    10.0, // clear_1
    10.0, // clear_2
    10.0, // clear_3
    10.0, // clear_4
     0.1, // t2_slot
     0.1, // t3_slot
    10.0, // tspin_mini
    10.0, // tspin_1
    10.0, // tspin_2
    10.0, // tspin_3
    10.0, // combo
     0.1, // ratio
};

size_t const NUM_PARAMS = sizeof(param_step) / sizeof(param_step[0]);

static double const param_rates[NUM_PARAMS] = {
    0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1,
    0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1,
    0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1,
};

static char const *const param_names[NUM_PARAMS] = {
    "base", "roof", "col_trans", "row_trans", "hole_count", "hole_line",
    "clear_width", "wide_2", "wide_3", "wide_4", "safe", "b2b", "attack",
    "hold_t", "hold_i", "waste_t", "waste_i", "clear_1", "clear_2",
    "clear_3", "clear_4", "t2_slot", "t3_slot", "tspin_mini", "tspin_1",
    "tspin_2", "tspin_3", "combo", "ratio",
};

struct SpsaSchedule
{
    double A;      // a_k = A / (k+1+A)^alpha
    double alpha;  // typically 0.602
    double C;      // c_k = C / (k+1)^gamma
    double gamma;  // typically 0.101

    double a_k(int k) const { return A / std::pow(k + 1.0 + A, alpha); }
    double c_k(int k) const { return C / std::pow(k + 1.0, gamma); }
};

static SpsaSchedule const default_schedule = { 500.0, 0.602, 1.0, 0.101 };

using SpsaEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

static void param_to_array(ai_zzz::TOJ::Param const &p, double *out)
{
    out[0] = p.base;      out[1] = p.roof;
    out[2] = p.col_trans; out[3] = p.row_trans;
    out[4] = p.hole_count; out[5] = p.hole_line;
    out[6] = p.clear_width; out[7] = p.wide_2;
    out[8] = p.wide_3;    out[9] = p.wide_4;
    out[10] = p.safe;     out[11] = p.b2b;
    out[12] = p.attack;   out[13] = p.hold_t;
    out[14] = p.hold_i;   out[15] = p.waste_t;
    out[16] = p.waste_i;  out[17] = p.clear_1;
    out[18] = p.clear_2;  out[19] = p.clear_3;
    out[20] = p.clear_4;  out[21] = p.t2_slot;
    out[22] = p.t3_slot;  out[23] = p.tspin_mini;
    out[24] = p.tspin_1;  out[25] = p.tspin_2;
    out[26] = p.tspin_3;  out[27] = p.combo;
    out[28] = p.ratio;
}

static void array_to_param(double const *in, ai_zzz::TOJ::Param &p)
{
    p.base = in[0];       p.roof = in[1];
    p.col_trans = in[2];  p.row_trans = in[3];
    p.hole_count = in[4]; p.hole_line = in[5];
    p.clear_width = in[6]; p.wide_2 = in[7];
    p.wide_3 = in[8];     p.wide_4 = in[9];
    p.safe = in[10];      p.b2b = in[11];
    p.attack = in[12];    p.hold_t = in[13];
    p.hold_i = in[14];    p.waste_t = in[15];
    p.waste_i = in[16];   p.clear_1 = in[17];
    p.clear_2 = in[18];   p.clear_3 = in[19];
    p.clear_4 = in[20];   p.t2_slot = in[21];
    p.t3_slot = in[22];   p.tspin_mini = in[23];
    p.tspin_1 = in[24];   p.tspin_2 = in[25];
    p.tspin_3 = in[26];   p.combo = in[27];
    p.ratio = in[28];
}

static void load_params(double *out, std::string const &tag = "")
{
    if (param::read(out, NUM_PARAMS, tag))
    {
        std::printf("[SPSA] Loaded best parameters from %s\n", param::filename(tag).c_str());
        return;
    }
    double const dflt[NUM_PARAMS] = {
        10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
        0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
        8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
        -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
        -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
    };
    std::memcpy(out, dflt, sizeof(dflt[0]) * NUM_PARAMS);
}

struct BotInstance
{
    SpsaEngine ai;
    m_tetris::TetrisMap map;
    std::mt19937 r_next;
    std::mt19937 r_garbage;

    int next_length = 5;
    int search_ms = 20;
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

    explicit BotInstance(SpsaEngine &global_ai) : ai(global_ai.context())
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
        r_next.seed(std::random_device{}());
        r_garbage.seed(r_next());
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
    }

    void init_status()
    {
        ai.ai_config()->safe = ai.ai()->get_safe(map, next.front());
        ai.status()->death = 0;
        ai.status()->combo = combo;
        ai.status()->under_attack = 0;
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
        while (next.size() <= (size_t)next_length)
        {
            for (size_t i = 0; i < ai.context()->type_max(); ++i)
            {
                next.push_back(ai.context()->convert(i));
            }
            std::shuffle(next.end() - ai.context()->type_max(), next.end(), r_next);
        }
    }

    void run()
    {
        init_status();

        char current = next.front();
        auto result = ai.run_hold(map, ai.context()->generate(current), hold, true,
                                  next.data() + 1, next_length, search_ms);
        if (result.target == nullptr || result.target->low >= 20)
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
            uint32_t hole = 1u << std::uniform_int_distribution<int>(0, map.width - 1)(r_garbage);
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

static void render_view(BotInstance &b1, BotInstance &b2, char const *name1, char const *name2)
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

    std::printf("\x1b[H\x1b[2J");
    int up1 = std::accumulate(b1.recv_attack.begin(), b1.recv_attack.end(), 0);
    int up2 = std::accumulate(b2.recv_attack.begin(), b2.recv_attack.end(), 0);
    std::printf(
        "HOLD=%c NEXT=%s COMBO=%d B2B=%d UP=%d P=%d L=%d A=%d APL=%.2f APP=%.2f %s\n"
        "HOLD=%c NEXT=%s COMBO=%d B2B=%d UP=%d P=%d L=%d A=%d APL=%.2f APP=%.2f %s\n",
        b1.hold, std::string(b1.next.begin() + 1, b1.next.begin() + 1 + b1.next_length).c_str(),
        b1.combo, b1.b2b, up1, b1.total_block, b1.total_clear, b1.total_attack,
        b1.total_clear ? (double)b1.total_attack / b1.total_clear : 0.0,
        b1.total_block ? (double)b1.total_attack / b1.total_block : 0.0, name1,
        b2.hold, std::string(b2.next.begin() + 1, b2.next.begin() + 1 + b2.next_length).c_str(),
        b2.combo, b2.b2b, up2, b2.total_block, b2.total_clear, b2.total_attack,
        b2.total_clear ? (double)b2.total_attack / b2.total_clear : 0.0,
        b2.total_block ? (double)b2.total_attack / b2.total_block : 0.0, name2);
    for (int y = 21; y >= 0; --y)
    {
        for (int x = 0; x < 10; ++x)
        {
            std::printf("%s", m1.full(x, y) ? "[]" : "  ");
        }
        std::printf("  ");
        for (int x = 0; x < 10; ++x)
        {
            std::printf("%s", m2.full(x, y) ? "[]" : "  ");
        }
        std::printf("\n");
    }
    std::fflush(stdout);
}

static std::pair<double, double> play_match(BotInstance &b1, BotInstance &b2, int max_rounds = 1000,
                                            std::function<void()> view_cb = nullptr,
                                            std::function<bool()> cancelled = nullptr)
{
    for (int round = 1; round <= max_rounds; ++round)
    {
        if (cancelled && cancelled())
        {
            break;
        }
        b1.prepare();
        b2.prepare();
        if (view_cb)
        {
            view_cb();
        }
        b1.run();
        b2.run();
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

    double apl1 = b1.total_block > 0 ? (double)b1.total_attack / b1.total_block : 0.0;
    double apl2 = b2.total_block > 0 ? (double)b2.total_attack / b2.total_block : 0.0;
    return { apl1, apl2 };
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

    int num_iters = 10000;
    int eval_matches = 1;
    int search_ms = 20;
    unsigned seed = 0;
    int threads = 1; // 0 = auto (hardware concurrency)

    if (argc > 1) num_iters = std::stoi(argv[1]);
    if (argc > 2) eval_matches = std::stoi(argv[2]);
    if (argc > 3) search_ms = std::stoi(argv[3]);
    if (argc > 4) seed = (unsigned)std::stoul(argv[4]);
    if (argc > 5) threads = std::stoi(argv[5]);
    if (seed == 0) seed = (unsigned)std::time(nullptr);
    if (threads == 0)
    {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }

    std::string data_file = param::tag_filename("spsa_data.bin", "");
    std::mt19937 rng(seed);

    SpsaEngine global_ai;
    global_ai.prepare(10, 40);

    double theta[NUM_PARAMS];
    load_params(theta);

    int resume_k = 0;
    {
        std::ifstream ifs(data_file, std::ios::binary);
        if (ifs.good())
        {
            ifs.read(reinterpret_cast<char *>(&resume_k), sizeof(resume_k));
            ifs.read(reinterpret_cast<char *>(theta), sizeof(theta));
            ifs.close();
            if (resume_k >= num_iters)
            {
                std::printf("[SPSA] Warning: resume point %d >= num_iters %d; nothing to do.\n"
                            "           Delete %s for a fresh start.\n", resume_k, num_iters, data_file.c_str());
            }
            std::printf("[SPSA] Resumed from iteration %d (overrides best_io_param)\n", resume_k);
        }
        else
        {
            std::printf("[SPSA] Starting fresh\n");
        }
    }

    SpsaSchedule sched = default_schedule;
    std::printf("[SPSA] %d iters, %d matches/eval, 1000 rounds/match, %dms search, seed %u, threads=%d\n",
                num_iters, eval_matches, search_ms, seed, threads);
    std::fflush(stdout);

    std::atomic<bool> view{ false };
    std::atomic<uint32_t> view_index{ 0 };

    // Stdin listener
    std::thread stdin_thread([&]()
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line == "view")
            {
                view = true;
                std::printf("\033[2J");
            }
            else if (line.empty())
            {
                view = false;
                view_index = 0;
            }
        }
    });
    stdin_thread.detach();

    auto start_time = std::chrono::steady_clock::now();

    for (int k = resume_k; k < num_iters; ++k)
    {
        double ck = sched.c_k(k);
        double ak = sched.a_k(k);

        double delta[NUM_PARAMS];
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            delta[i] = (rng() & 1) ? 1.0 : -1.0;
        }

        double theta_plus[NUM_PARAMS], theta_minus[NUM_PARAMS];
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            double step = ck * param_step[i] * delta[i];
            theta_plus[i] = theta[i] + step;
            theta_minus[i] = theta[i] - step;
        }

        int m = std::max(1, eval_matches);
        int win_threshold = (m + 1) / 2;
        std::atomic<int> wins_plus{ 0 }, wins_minus{ 0 }, matches_played{ 0 };
        std::atomic<bool> decided{ false };

        auto play_one = [&](int index)
        {
            if (decided.load(std::memory_order_relaxed))
            {
                return;
            }
            BotInstance plus(global_ai), minus(global_ai);
            plus.search_ms = minus.search_ms = search_ms;
            plus.init(theta_plus);
            minus.init(theta_minus);
            std::function<void()> view_cb = [&, index]()
            {
                if (!view.load(std::memory_order_relaxed))
                {
                    uint32_t mine = (uint32_t)index;
                    view_index.compare_exchange_strong(mine, 0);
                    return;
                }
                uint32_t zero = 0;
                if (view_index.compare_exchange_strong(zero, (uint32_t)index))
                {
                    // claimed the view
                }
                if (view_index.load(std::memory_order_relaxed) != (uint32_t)index)
                {
                    return;
                }
                render_view(plus, minus, "PLUS", "MINUS");
            };
            auto [apl_plus, apl_minus] = play_match(plus, minus, 1000, view_cb,
                [&]() { return decided.load(std::memory_order_relaxed); });
            if (decided.load(std::memory_order_relaxed))
            {
                return;
            }
            ++matches_played;
            if (apl_plus > apl_minus)
            {
                if (++wins_plus >= win_threshold)
                {
                    decided.store(true, std::memory_order_relaxed);
                }
            }
            else if (apl_minus > apl_plus)
            {
                if (++wins_minus >= win_threshold)
                {
                    decided.store(true, std::memory_order_relaxed);
                }
            }
        };

        if (threads > 1 && m > 1)
        {
            size_t nthreads = std::min<size_t>(m, (size_t)std::max(1, threads));
            std::vector<std::thread> pool;
            pool.reserve(nthreads);
            for (size_t t = 0; t < nthreads; ++t)
            {
                pool.emplace_back([&, t]()
                {
                    for (int i = (int)t; i < m && !decided.load(std::memory_order_relaxed); i += (int)nthreads)
                    {
                        play_one(i);
                    }
                });
            }
            for (auto &th : pool)
            {
                th.join();
            }
        }
        else
        {
            for (int i = 0; i < m && !decided.load(std::memory_order_relaxed); ++i)
            {
                play_one(i);
            }
        }
        double score_diff = matches_played.load() > 0
            ? (double)(wins_plus.load() - wins_minus.load()) / matches_played.load() : 0.0;

        double grad[NUM_PARAMS];
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            double denom = 2.0 * ck * param_step[i] * delta[i];
            if (std::fabs(denom) < 1e-15)
            {
                denom = 1e-15;
            }
            grad[i] = score_diff / denom;
        }

        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            theta[i] += ak * param_rates[i] * param_step[i] * param_step[i] * grad[i];
        }

        {
            std::ofstream ofs(data_file, std::ios::binary);
            int sk = k + 1;
            ofs.write((char const *)&sk, sizeof(sk));
            ofs.write((char const *)theta, sizeof(theta));
            ofs.close();
        }

        param::write(theta, NUM_PARAMS, "");
        if ((k + 1) % 10 == 0 || k == resume_k)
        {
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - start_time).count();
            std::printf("[SPSA] iter %5d | score=%+.3f | ak=%.6f ck=%.6f | %.1fs\n",
                        k, score_diff, ak, ck, sec);
        }
        std::fflush(stdout);
    }

    param::write(theta, NUM_PARAMS, "");
    {
        std::ofstream ofs(data_file, std::ios::binary);
        int sk = num_iters;
        ofs.write((char const *)&sk, sizeof(sk));
        ofs.write((char const *)theta, sizeof(theta));
        ofs.close();
    }

    std::printf("\n[SPSA] Done. %d iterations completed\n", num_iters);
    std::printf("[SPSA] Params saved to %s\n", param::filename("").c_str());
    std::printf("[SPSA] theta:\n");
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        std::printf("  %-14s %+.6f\n", param_names[i], theta[i]);
    }
    return 0;
}
