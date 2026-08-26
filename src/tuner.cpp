
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
#include <mutex>
#include <numbers>
#include <print>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "tetris_core.h"
#include "search_tspin.h"
#include "ai_zzz.h"
#include "rule_toj.h"
#include "param.h"

static int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
static int const combo_table_max = 10;

size_t const NUM_PARAMS = 29;
static int iters_per_move = 0;
static int const next_length = 6;

static double const param_scale[NUM_PARAMS] = {
    0.17, 2.8, 0.31, 0.97, 6.3, 6.8, 0.43, 0.18, 7.3, 8.15,
    0.037, 2.64, 1.8, 0.00085, 0.0012, 1.4, 0.31, 0.24, 0.99, 0.48,
    0.70, 0.0092, 0.058, 1.3, 0.22, 0.26, 0.59, 0.94, 0.68,
};

static uint64_t splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

static int rademacher(uint64_t seed, int k, int j, int i)
{
    uint64_t h = splitmix64(seed ^ splitmix64(static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ULL
                                              + static_cast<uint64_t>(j) * 0xBF58476D1CE4E5B9ULL
                                              + static_cast<uint64_t>(i)));
    return static_cast<int>(h & 1) ? 1 : -1;
}

struct Scenario
{
    std::deque<char> pieces;
    uint64_t pair_seed = 0;
    int round = 0;
    int packet_index = 0;
};

static int scenario_hole(Scenario const &s)
{
    uint64_t h = splitmix64(s.pair_seed
        ^ splitmix64(static_cast<uint64_t>(s.round) * 0x9E3779B97F4A7C15ULL
                     + static_cast<uint64_t>(s.packet_index) * 0x94D049BB133111EBULL));
    return static_cast<int>(h % 10);
}

static Scenario make_scenario(uint64_t seed, size_t max_rounds, size_t next_length)
{
    std::mt19937 rng(static_cast<unsigned>(splitmix64(seed)));
    std::string bag = "IJLOSTZ";
    Scenario s;

    size_t pieces_needed = max_rounds * 2 + next_length * 2 + 4;
    while (s.pieces.size() < pieces_needed)
    {
        std::shuffle(bag.begin(), bag.end(), rng);
        for (char c : bag)
        {
            s.pieces.push_back(c);
        }
    }

    s.pair_seed = seed;
    return s;
}

static char const *const param_names[NUM_PARAMS] = {
    "base", "roof", "col_trans", "row_trans", "hole_count", "hole_line",
    "clear_width", "wide_2", "wide_3", "wide_4", "safe", "b2b", "attack",
    "hold_t", "hold_i", "waste_t", "waste_i", "clear_1", "clear_2",
    "clear_3", "clear_4", "t2_slot", "t3_slot", "tspin_mini", "tspin_1",
    "tspin_2", "tspin_3", "combo", "ratio",
};

static double const ES_SIGMA0 = 0.30;
static double const ES_SIGMA_FLOOR = 0.08;
static double const ES_SIGMA_TAU = 2000.0;
static double const ES_LR0 = 0.025;
static double const ES_LR1 = 0.004;
static double const ES_LR_HORIZON = 5000.0;
static double const ES_BETA1 = 0.9;
static double const ES_BETA2 = 0.999;
static double const ES_EPS = 1e-8;
static double const ES_DX_MAX = 0.04;
static double const ES_DX_L2_MAX = 0.10;
static uint64_t const CHALLENGE_SEED_BASE = 0x123456789ABCDEF0ULL;
static int const CHALLENGE_EVERY = 50;
static int const CHALLENGE_PAIRS = 4;
static int const CHALLENGE_MAX_PAIRS = 32;
static int const CHALLENGE_PROMOTE_LEVEL = 95;
static int const STALL_WINDOW = 200;
static int const MAX_RESTARTS = 4;
static double const RESTART_SIGMA = 0.20;

using TunerEngine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ai_zzz::TOJ, search_tspin::Search>;

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

static bool load_params(double *out, std::string const &tag = "")
{
    if (param::read(out, NUM_PARAMS, tag))
    {
        std::println("[TUNER] Loaded best parameters from {}", param::filename(tag));
        return true;
    }
    return false;
}

static void default_params(double *out)
{
    ai_zzz::TOJ::Param p;
    param_to_array(p, out);
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

    explicit BotInstance()
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
        auto result = ai.run_hold(map, ai.spawn_node(current, last_clear, is_hold_piece), hold, true,
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

static MatchResult play_match(BotInstance &b1, BotInstance &b2, int max_rounds = 1000,
                              std::function<void()> view_cb = nullptr)
{
    int played_rounds = 0;
    for (int round = 1; round <= max_rounds; ++round)
    {
        b1.scenario->round = round;
        b2.scenario->round = round;
        b1.prepare();
        b2.prepare();
        if (view_cb)
        {
            view_cb();
        }
        b1.run();
        b2.run();
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

static double paired_reward(MatchOutcome const &a, MatchOutcome const &b)
{
    return 0.5 * (a.winner - b.winner);
}

static std::vector<MatchOutcome> run_batch(std::vector<MatchJob> const &jobs, int threads, int search_ms, int max_rounds,
                                           TunerEngine &global_ai, std::atomic<bool> &view,
                                           std::mutex &view_mutex, std::atomic<uint32_t> &view_index)
{
    std::vector<MatchOutcome> out(jobs.size());
    std::atomic<size_t> next_job{ 0 };
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
            if (jobs[idx].budget_iters_p1 > 0)
            {
                b1.search_budget = m_tetris::SearchBudget::by_iterations(static_cast<size_t>(jobs[idx].budget_iters_p1));
            }
            else if (jobs[idx].budget_ms_p1 > 0)
            {
                b1.search_budget = m_tetris::SearchBudget{ static_cast<time_t>(jobs[idx].budget_ms_p1) };
            }
            else if (iters_per_move > 0)
            {
                b1.search_budget = m_tetris::SearchBudget::by_iterations(static_cast<size_t>(iters_per_move));
            }
            else
            {
                b1.search_budget = m_tetris::SearchBudget{ static_cast<time_t>(search_ms) };
            }
            if (jobs[idx].budget_iters_p2 > 0)
            {
                b2.search_budget = m_tetris::SearchBudget::by_iterations(static_cast<size_t>(jobs[idx].budget_iters_p2));
            }
            else if (jobs[idx].budget_ms_p2 > 0)
            {
                b2.search_budget = m_tetris::SearchBudget{ static_cast<time_t>(jobs[idx].budget_ms_p2) };
            }
            else if (iters_per_move > 0)
            {
                b2.search_budget = m_tetris::SearchBudget::by_iterations(static_cast<size_t>(iters_per_move));
            }
            else
            {
                b2.search_budget = m_tetris::SearchBudget{ static_cast<time_t>(search_ms) };
            }
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
            MatchResult r = play_match(b1, b2, max_rounds, view_cb);
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
    size_t nthreads = std::min<size_t>(std::max(1, threads), jobs.size());
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (size_t t = 0; t < nthreads; ++t)
    {
        pool.emplace_back(worker);
    }
    for (auto &th : pool)
    {
        th.join();
    }
    return out;
}

static void adam_update(double const *grad, int q, double es_sigma, int k,
                        double *es_m1, double *es_m2, double *dx, double &grad_norm)
{
    double g[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        g[i] = grad[i] / (2.0 * q * es_sigma);
        grad_norm += g[i] * g[i];
    }
    grad_norm = std::sqrt(grad_norm);
    double const ptk = static_cast<double>(k);
    double t = std::min(1.0, ptk / ES_LR_HORIZON);
    double lr = ES_LR1 + 0.5 * (ES_LR0 - ES_LR1) * (1.0 + std::cos(std::numbers::pi * t));
    double beta1t = std::pow(ES_BETA1, k + 1);
    double beta2t = std::pow(ES_BETA2, k + 1);
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        es_m1[i] = ES_BETA1 * es_m1[i] + (1 - ES_BETA1) * g[i];
        es_m2[i] = ES_BETA2 * es_m2[i] + (1 - ES_BETA2) * g[i] * g[i];
        double mh = es_m1[i] / (1 - beta1t);
        double vh = es_m2[i] / (1 - beta2t);
        dx[i] = lr * mh / (std::sqrt(vh) + ES_EPS);
        dx[i] = std::max(-ES_DX_MAX, std::min(ES_DX_MAX, dx[i]));
    }
    double l2 = 0;
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        l2 += dx[i] * dx[i];
    }
    l2 = std::sqrt(l2);
    if (l2 > ES_DX_L2_MAX)
    {
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            dx[i] *= ES_DX_L2_MAX / l2;
        }
    }
}

static double run_challenge(double const *candidate, double const *incumbent, int threads,
                           int search_ms, int max_rounds, TunerEngine &global_ai,
                           std::atomic<bool> &view, std::mutex &view_mutex,
                           std::atomic<uint32_t> &view_index,
                           uint64_t challenge_seed, int pairs)
{
    std::vector<MatchJob> jobs;
    jobs.reserve(2 * pairs);
    for (int j = 0; j < pairs; ++j)
    {
        uint64_t s = splitmix64(challenge_seed
            ^ splitmix64(static_cast<uint64_t>(j) * 0x94D049BB133111EBULL));
        uint64_t sa = splitmix64(s);
        uint64_t sb = splitmix64(s ^ 0x9E3779B97F4A7C15ULL);
        MatchJob a{}, b{};
        std::memcpy(a.p1, candidate, NUM_PARAMS * sizeof(double));
        std::memcpy(a.p2, incumbent, NUM_PARAMS * sizeof(double));
        std::memcpy(b.p1, incumbent, NUM_PARAMS * sizeof(double));
        std::memcpy(b.p2, candidate, NUM_PARAMS * sizeof(double));
        a.scenario_seed_p1 = sa;
        a.scenario_seed_p2 = sb;
        b.scenario_seed_p1 = sa;
        b.scenario_seed_p2 = sb;
        a.job_id = static_cast<size_t>(2) * j;
        b.job_id = static_cast<size_t>(2) * j + 1;
        jobs.push_back(a);
        jobs.push_back(b);
    }
    auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_mutex, view_index);
    double score = 0;
    for (int j = 0; j < pairs; ++j)
    {
        score += paired_reward(out[2 * j], out[2 * j + 1]);
    }
    return score / pairs;
}

static uint64_t checkpoint_checksum(void const *data, size_t bytes)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    char const *p = static_cast<char const *>(data);
    for (size_t i = 0; i < bytes; ++i)
    {
        h ^= (unsigned char)p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static void fsync_path(std::string const &path)
{
#if defined(_WIN32)
    int fd = ::_open(path.c_str(), _O_RDONLY);
    if (fd >= 0)
    {
        ::_commit(fd);
        ::_close(fd);
    }
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

static void fsync_directory(std::string const &path)
{
#if !defined(_WIN32)
    std::string dir = path;
    size_t const pos = dir.find_last_of("/\\");
    if (pos != std::string::npos)
    {
        dir = dir.substr(0, pos);
    }
    if (dir.empty())
    {
        dir = ".";
    }
    int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd >= 0)
    {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

static bool durable_write_doubles(std::string const &path, double const *data, size_t n)
{
    std::string const tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary);
        if (!ofs.good())
        {
            return false;
        }
        ofs.write(reinterpret_cast<char const *>(data), static_cast<std::streamsize>(n * sizeof(double)));
        ofs.flush();
        if (!ofs.good())
        {
            std::remove(tmp.c_str());
            return false;
        }
    }
    fsync_path(tmp);
    {
        std::ifstream src(path, std::ios::binary);
        if (src.good())
        {
            std::ofstream dst(path + ".bak", std::ios::binary | std::ios::trunc);
            if (dst.good())
            {
                dst << src.rdbuf();
                dst.flush();
            }
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    fsync_directory(path);
    return true;
}

struct CheckpointConfig
{
    int max_rounds;
    int eval_matches;
    int search_ms;
    unsigned seed;
    int iters_per_move;
    double param_scale[NUM_PARAMS];
    int combo_table[combo_table_max];
    int next_length;
};

static bool checkpoint_config_equal(CheckpointConfig const &a, CheckpointConfig const &b)
{
    if (a.max_rounds != b.max_rounds || a.eval_matches != b.eval_matches
        || a.search_ms != b.search_ms || a.seed != b.seed
        || a.iters_per_move != b.iters_per_move
        || a.next_length != b.next_length)
    {
        return false;
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        if (a.param_scale[i] != b.param_scale[i])
        {
            return false;
        }
    }
    for (size_t i = 0; i < static_cast<size_t>(combo_table_max); ++i)
    {
        if (a.combo_table[i] != b.combo_table[i])
        {
            return false;
        }
    }
    return true;
}

enum CheckpointLoadResult
{
    CHECKPOINT_OK = 1,
    CHECKPOINT_MISSING = 0,
    CHECKPOINT_CORRUPT = -1,
};

static int load_state(std::string const &data_file, int &resume_k, double *theta,
                      double &es_sigma, double *es_m1, double *es_m2,
                      CheckpointConfig &saved_cfg)
{
    std::ifstream ifs(data_file, std::ios::binary);
    if (!ifs.good())
    {
        return CHECKPOINT_MISSING;
    }

    int ver = 0;
    ifs.read(reinterpret_cast<char *>(&ver), sizeof(ver));
    if (ver != 1)
    {
        return CHECKPOINT_CORRUPT;
    }
    ifs.read(reinterpret_cast<char *>(&resume_k), sizeof(resume_k));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.max_rounds), sizeof(saved_cfg.max_rounds));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.eval_matches), sizeof(saved_cfg.eval_matches));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.search_ms), sizeof(saved_cfg.search_ms));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.seed), sizeof(saved_cfg.seed));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.iters_per_move), sizeof(saved_cfg.iters_per_move));
    ifs.read(reinterpret_cast<char *>(saved_cfg.param_scale), sizeof(saved_cfg.param_scale));
    ifs.read(reinterpret_cast<char *>(saved_cfg.combo_table), sizeof(saved_cfg.combo_table));
    ifs.read(reinterpret_cast<char *>(&saved_cfg.next_length), sizeof(saved_cfg.next_length));
    ifs.read(reinterpret_cast<char *>(theta), NUM_PARAMS * sizeof(double));
    ifs.read(reinterpret_cast<char *>(&es_sigma), sizeof(es_sigma));
    ifs.read(reinterpret_cast<char *>(es_m1), NUM_PARAMS * sizeof(double));
    ifs.read(reinterpret_cast<char *>(es_m2), NUM_PARAMS * sizeof(double));
    uint64_t stored_checksum = 0;
    ifs.read(reinterpret_cast<char *>(&stored_checksum), sizeof(stored_checksum));
    if (!ifs.good())
    {
        return CHECKPOINT_CORRUPT;
    }
    char extra = 0;
    ifs.read(&extra, 1);
    if (ifs.gcount() != 0)
    {
        return CHECKPOINT_CORRUPT;
    }
    size_t const payload_bytes = sizeof(resume_k) + sizeof(saved_cfg.max_rounds)
        + sizeof(saved_cfg.eval_matches) + sizeof(saved_cfg.search_ms) + sizeof(saved_cfg.seed)
        + sizeof(saved_cfg.iters_per_move)
        + sizeof(saved_cfg.param_scale) + sizeof(saved_cfg.combo_table)
        + sizeof(saved_cfg.next_length) + NUM_PARAMS * sizeof(double) * 3 + sizeof(es_sigma);
    std::vector<char> payload(payload_bytes);
    char *p = payload.data();
    std::memcpy(p, &resume_k, sizeof(resume_k)); p += sizeof(resume_k);
    std::memcpy(p, &saved_cfg.max_rounds, sizeof(saved_cfg.max_rounds)); p += sizeof(saved_cfg.max_rounds);
    std::memcpy(p, &saved_cfg.eval_matches, sizeof(saved_cfg.eval_matches)); p += sizeof(saved_cfg.eval_matches);
    std::memcpy(p, &saved_cfg.search_ms, sizeof(saved_cfg.search_ms)); p += sizeof(saved_cfg.search_ms);
    std::memcpy(p, &saved_cfg.seed, sizeof(saved_cfg.seed)); p += sizeof(saved_cfg.seed);
    std::memcpy(p, &saved_cfg.iters_per_move, sizeof(saved_cfg.iters_per_move)); p += sizeof(saved_cfg.iters_per_move);
    std::memcpy(p, saved_cfg.param_scale, sizeof(saved_cfg.param_scale)); p += sizeof(saved_cfg.param_scale);
    std::memcpy(p, saved_cfg.combo_table, sizeof(saved_cfg.combo_table)); p += sizeof(saved_cfg.combo_table);
    std::memcpy(p, &saved_cfg.next_length, sizeof(saved_cfg.next_length)); p += sizeof(saved_cfg.next_length);
    std::memcpy(p, theta, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    std::memcpy(p, &es_sigma, sizeof(es_sigma)); p += sizeof(es_sigma);
    std::memcpy(p, es_m1, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    std::memcpy(p, es_m2, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    uint64_t computed = checkpoint_checksum(payload.data(), payload.size());
    if (computed != stored_checksum)
    {
        return CHECKPOINT_CORRUPT;
    }
    if (resume_k < 0 || es_sigma <= 0.0 || !std::isfinite(es_sigma))
    {
        return CHECKPOINT_CORRUPT;
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        if (!std::isfinite(theta[i]) || !std::isfinite(es_m1[i]) || !std::isfinite(es_m2[i]))
        {
            return CHECKPOINT_CORRUPT;
        }
    }
    return CHECKPOINT_OK;
}

static bool save_state(std::string const &data_file, int k, double const *theta,
                       double es_sigma, double const *es_m1, double const *es_m2,
                       CheckpointConfig const &cfg)
{
    size_t const payload_bytes = sizeof(k) + sizeof(cfg.max_rounds) + sizeof(cfg.eval_matches)
        + sizeof(cfg.search_ms) + sizeof(cfg.seed) + sizeof(cfg.iters_per_move)
        + sizeof(cfg.param_scale) + sizeof(cfg.combo_table)
        + sizeof(cfg.next_length) + NUM_PARAMS * sizeof(double) * 3 + sizeof(es_sigma);
    std::vector<char> payload(payload_bytes);
    char *p = payload.data();
    std::memcpy(p, &k, sizeof(k)); p += sizeof(k);
    std::memcpy(p, &cfg.max_rounds, sizeof(cfg.max_rounds)); p += sizeof(cfg.max_rounds);
    std::memcpy(p, &cfg.eval_matches, sizeof(cfg.eval_matches)); p += sizeof(cfg.eval_matches);
    std::memcpy(p, &cfg.search_ms, sizeof(cfg.search_ms)); p += sizeof(cfg.search_ms);
    std::memcpy(p, &cfg.seed, sizeof(cfg.seed)); p += sizeof(cfg.seed);
    std::memcpy(p, &cfg.iters_per_move, sizeof(cfg.iters_per_move)); p += sizeof(cfg.iters_per_move);
    std::memcpy(p, cfg.param_scale, sizeof(cfg.param_scale)); p += sizeof(cfg.param_scale);
    std::memcpy(p, cfg.combo_table, sizeof(cfg.combo_table)); p += sizeof(cfg.combo_table);
    std::memcpy(p, &cfg.next_length, sizeof(cfg.next_length)); p += sizeof(cfg.next_length);
    std::memcpy(p, theta, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    std::memcpy(p, &es_sigma, sizeof(es_sigma)); p += sizeof(es_sigma);
    std::memcpy(p, es_m1, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    std::memcpy(p, es_m2, NUM_PARAMS * sizeof(double)); p += NUM_PARAMS * sizeof(double);
    uint64_t checksum = checkpoint_checksum(payload.data(), payload.size());

    std::string const tmp = data_file + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary);
        int ver = 1;
        ofs.write(reinterpret_cast<char const *>(&ver), sizeof(ver));
        ofs.write(payload.data(), (std::streamsize)payload.size());
        ofs.write(reinterpret_cast<char const *>(&checksum), sizeof(checksum));
        ofs.flush();
        if (!ofs.good())
        {
            std::remove(tmp.c_str());
            return false;
        }
    }
    fsync_path(tmp);
    {
        std::ifstream src(data_file, std::ios::binary);
        if (src.good())
        {
            std::ofstream dst(data_file + ".bak", std::ios::binary | std::ios::trunc);
            if (dst.good())
            {
                dst << src.rdbuf();
                dst.flush();
            }
        }
    }
    if (std::rename(tmp.c_str(), data_file.c_str()) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    fsync_directory(data_file);
    return true;
}

static int run_bench(int argc, char *argv[])
{
    std::vector<char const *> positional;
    bool want_default = false;
    positional.push_back(argv[0]);
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--default") == 0)
        {
            want_default = true;
        }
        else
        {
            positional.push_back(argv[i]);
        }
    }
    int const n = static_cast<int>(positional.size());

    int pairs = 64;
    int iters_arm = 20;
    int search_ms_arm = 20;
    unsigned seed = 555;
    int threads = 0;
    int max_rounds = 3600;
    if (n > 2) pairs = std::stoi(positional[2]);
    if (n > 3) iters_arm = std::stoi(positional[3]);
    if (n > 4) search_ms_arm = std::stoi(positional[4]);
    if (n > 5) seed = static_cast<unsigned>(std::stoul(positional[5]));
    if (n > 6) threads = std::stoi(positional[6]);
    if (n > 7) max_rounds = std::stoi(positional[7]);
    if (pairs <= 0 || iters_arm <= 0 || search_ms_arm <= 0 || max_rounds <= 0)
    {
        std::println(stderr, "[BENCH] invalid arguments");
        return 1;
    }
    if (seed == 0) seed = static_cast<unsigned>(std::time(nullptr));
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());

    TunerEngine global_ai;
    global_ai.prepare(10, 40);
    double theta[NUM_PARAMS];
    if (!load_params(theta))
    {
        if (want_default)
        {
            default_params(theta);
            std::println(stderr, "[BENCH] warning: {} missing or invalid; using built-in defaults", param::filename(""));
        }
        else
        {
            std::println(stderr, "[BENCH] error: {} missing or invalid; provide a valid param file or pass --default.", param::filename(""));
            return 1;
        }
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        theta[i] = std::isfinite(theta[i]) ? theta[i] : 0.0;
    }

    std::vector<MatchJob> jobs;
    jobs.reserve(static_cast<size_t>(2) * pairs);
    for (int j = 0; j < pairs; ++j)
    {
        uint64_t s = splitmix64(seed
            ^ splitmix64(static_cast<uint64_t>(j) * 0x94D049BB133111EBULL));
        uint64_t sa = splitmix64(s);
        uint64_t sb = splitmix64(s ^ 0x9E3779B97F4A7C15ULL);
        MatchJob a{}, b{};
        std::memcpy(a.p1, theta, sizeof(a.p1));
        std::memcpy(a.p2, theta, sizeof(a.p2));
        a.budget_iters_p1 = iters_arm;
        a.budget_ms_p2 = search_ms_arm;
        a.scenario_seed_p1 = sa;
        a.scenario_seed_p2 = sb;
        a.job_id = static_cast<size_t>(2) * j;
        std::memcpy(b.p1, theta, sizeof(b.p1));
        std::memcpy(b.p2, theta, sizeof(b.p2));
        b.budget_ms_p1 = search_ms_arm;
        b.budget_iters_p2 = iters_arm;
        b.scenario_seed_p1 = sa;
        b.scenario_seed_p2 = sb;
        b.job_id = static_cast<size_t>(2) * j + 1;
        jobs.push_back(a);
        jobs.push_back(b);
    }

    std::println("[BENCH] {} iters vs {} ms, {} seat-swapped pairs over {} rounds, seed {}, threads={}",
                 iters_arm, search_ms_arm, pairs, max_rounds, seed, threads);
    std::fflush(stdout);

    std::atomic<bool> view{ false };
    std::atomic<uint32_t> view_index{ 0 };
    std::mutex view_mutex;
    auto out = run_batch(jobs, threads, search_ms_arm, max_rounds, global_ai, view, view_mutex, view_index);

    double wins_iters = 0;
    double app_iters = 0, app_ms = 0;
    double apl_iters = 0, apl_ms = 0;
    double rounds_sum = 0;
    int deaths_iters = 0, deaths_ms = 0;
    for (int j = 0; j < pairs; ++j)
    {
        MatchOutcome const &ga = out[2 * j];
        MatchOutcome const &gb = out[2 * j + 1];
        if (ga.winner > 0) wins_iters += 1.0;
        else if (ga.winner == 0) wins_iters += 0.5;
        if (gb.winner < 0) wins_iters += 1.0;
        else if (gb.winner == 0) wins_iters += 0.5;
        app_iters += ga.app1 + gb.app2;
        app_ms += ga.app2 + gb.app1;
        apl_iters += ga.apl1 + gb.apl2;
        apl_ms += ga.apl2 + gb.apl1;
        rounds_sum += ga.rounds + gb.rounds;
        deaths_iters += ga.dead1 + gb.dead2;
        deaths_ms += ga.dead2 + gb.dead1;
    }
    int games = 2 * pairs;
    double phat = wins_iters / static_cast<double>(games);
    double z = 1.96;
    double denom = 1.0 + z * z / games;
    double mid = phat + z * z / (2.0 * games);
    double margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / games + z * z / (4.0 * games * games));
    double lo = (mid - margin) / denom;
    double hi = (mid + margin) / denom;

    std::println("[BENCH] iters-arm win-equiv: {:.1f}/{:.0f} = {:.1f}% (95% CI [{:.1f}%, {:.1f}%])",
                 wins_iters, static_cast<double>(games), 100.0 * phat,
                 100.0 * std::max(0.0, lo), 100.0 * std::min(1.0, hi));
    std::println("[BENCH] APP: iters={:.3f}  ms={:.3f} | APL: iters={:.3f}  ms={:.3f} | R={:.1f} | deaths: iters={} ms={}",
                 app_iters / games, app_ms / games, apl_iters / games, apl_ms / games,
                 rounds_sum / games, deaths_iters, deaths_ms);
    return 0;
}

static int run_vs(int argc, char *argv[])
{
    std::vector<char const *> positional;
    positional.push_back(argv[0]);
    for (int i = 1; i < argc; ++i)
    {
        positional.push_back(argv[i]);
    }
    int const n = static_cast<int>(positional.size());

    std::string path_a, path_b;
    int pairs = 32;
    int search_ms = 20;
    int ipm = 0;
    unsigned seed = 555;
    int threads = 0;
    int max_rounds = 3600;
    if (n > 2) path_a = positional[2];
    if (n > 3) path_b = positional[3];
    if (n > 4) pairs = std::stoi(positional[4]);
    if (n > 5) search_ms = std::stoi(positional[5]);
    if (n > 6) ipm = std::stoi(positional[6]);
    if (n > 7) seed = static_cast<unsigned>(std::stoul(positional[7]));
    if (n > 8) threads = std::stoi(positional[8]);
    if (n > 9) max_rounds = std::stoi(positional[9]);
    if (path_a.empty() || path_b.empty() || pairs <= 0 || search_ms < 0 || ipm < 0 || max_rounds <= 0
        || (ipm <= 0 && search_ms <= 0))
    {
        std::println(stderr, "[VS] usage: tuner vs <path_a> <path_b> <pairs> <search_ms> <iters_per_move> <seed> <threads> <max_rounds>");
        return 1;
    }
    if (seed == 0) seed = static_cast<unsigned>(std::time(nullptr));
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());

    double pa[NUM_PARAMS], pb[NUM_PARAMS];
    std::ifstream fa(path_a, std::ios::binary);
    std::ifstream fb(path_b, std::ios::binary);
    if (!fa.good() || !fb.good())
    {
        std::println(stderr, "[VS] error: could not open both param files");
        return 1;
    }
    fa.read(reinterpret_cast<char *>(pa), NUM_PARAMS * sizeof(double));
    fb.read(reinterpret_cast<char *>(pb), NUM_PARAMS * sizeof(double));
    if (fa.gcount() != static_cast<std::streamsize>(NUM_PARAMS * sizeof(double))
        || fb.gcount() != static_cast<std::streamsize>(NUM_PARAMS * sizeof(double)))
    {
        std::println(stderr, "[VS] error: param files must contain {} doubles", NUM_PARAMS);
        return 1;
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        if (!std::isfinite(pa[i]) || !std::isfinite(pb[i]))
        {
            std::println(stderr, "[VS] error: non-finite param values");
            return 1;
        }
    }

    TunerEngine global_ai;
    global_ai.prepare(10, 40);

    std::vector<MatchJob> jobs;
    jobs.reserve(static_cast<size_t>(2) * pairs);
    for (int j = 0; j < pairs; ++j)
    {
        uint64_t s = splitmix64(seed
            ^ splitmix64(static_cast<uint64_t>(j) * 0x94D049BB133111EBULL));
        uint64_t sa = splitmix64(s);
        uint64_t sb = splitmix64(s ^ 0x9E3779B97F4A7C15ULL);
        MatchJob a{}, b{};
        std::memcpy(a.p1, pa, sizeof(a.p1));
        std::memcpy(a.p2, pb, sizeof(a.p2));
        a.budget_iters_p1 = ipm;
        a.budget_iters_p2 = ipm;
        a.budget_ms_p1 = search_ms;
        a.budget_ms_p2 = search_ms;
        a.scenario_seed_p1 = sa;
        a.scenario_seed_p2 = sb;
        a.job_id = static_cast<size_t>(2) * j;
        std::memcpy(b.p1, pb, sizeof(b.p1));
        std::memcpy(b.p2, pa, sizeof(b.p2));
        b.budget_iters_p1 = ipm;
        b.budget_iters_p2 = ipm;
        b.budget_ms_p1 = search_ms;
        b.budget_ms_p2 = search_ms;
        b.scenario_seed_p1 = sa;
        b.scenario_seed_p2 = sb;
        b.job_id = static_cast<size_t>(2) * j + 1;
        jobs.push_back(a);
        jobs.push_back(b);
    }

    std::println("[VS] {} vs {}, {} seat-swapped pairs over {} rounds, {} search, seed {}, threads={}",
                 path_a, path_b, pairs, max_rounds,
                 ipm > 0 ? (std::to_string(ipm) + " iters") : (std::to_string(search_ms) + "ms"),
                 seed, threads);
    std::fflush(stdout);

    std::atomic<bool> view{ false };
    std::atomic<uint32_t> view_index{ 0 };
    std::mutex view_mutex;
    auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_mutex, view_index);

    double wins_a = 0;
    double app_a = 0, app_b = 0, apl_a = 0, apl_b = 0, rounds_sum = 0;
    int deaths_a = 0, deaths_b = 0;
    for (int j = 0; j < pairs; ++j)
    {
        MatchOutcome const &ga = out[2 * j];
        MatchOutcome const &gb = out[2 * j + 1];
        if (ga.winner > 0) wins_a += 1.0;
        else if (ga.winner == 0) wins_a += 0.5;
        if (gb.winner < 0) wins_a += 1.0;
        else if (gb.winner == 0) wins_a += 0.5;
        app_a += ga.app1 + gb.app2;
        app_b += ga.app2 + gb.app1;
        apl_a += ga.apl1 + gb.apl2;
        apl_b += ga.apl2 + gb.apl1;
        rounds_sum += ga.rounds + gb.rounds;
        deaths_a += ga.dead1 + gb.dead2;
        deaths_b += ga.dead2 + gb.dead1;
    }
    int games = 2 * pairs;
    double phat = wins_a / static_cast<double>(games);
    double z = 1.96;
    double denom = 1.0 + z * z / games;
    double mid = phat + z * z / (2.0 * games);
    double margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / games + z * z / (4.0 * games * games));
    double lo = (mid - margin) / denom;
    double hi = (mid + margin) / denom;

    std::println("[VS] A win-equiv: {:.1f}/{:.0f} = {:.1f}% (95% CI [{:.1f}%, {:.1f}%])",
                 wins_a, static_cast<double>(games), 100.0 * phat,
                 100.0 * std::max(0.0, lo), 100.0 * std::min(1.0, hi));
    std::println("[VS] APP: A={:.3f} B={:.3f} | APL: A={:.3f} B={:.3f} | R={:.1f} | deaths: A={} B={}",
                 app_a / games, app_b / games, apl_a / games, apl_b / games,
                 rounds_sum / games, deaths_a, deaths_b);
    return 0;
}

static int run_probe(int argc, char *argv[])
{
    std::vector<char const *> positional;
    bool want_default = false;
    positional.push_back(argv[0]);
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--default") == 0)
        {
            want_default = true;
        }
        else
        {
            positional.push_back(argv[i]);
        }
    }
    int const n = static_cast<int>(positional.size());

    int batches = 8;
    int eval_matches = 14;
    int search_ms = 20;
    unsigned seed = 555;
    int threads = 14;
    int max_rounds = 3600;
    double step = 0.30;
    if (n > 2) batches = std::stoi(positional[2]);
    if (n > 3) eval_matches = std::stoi(positional[3]);
    if (n > 4) search_ms = std::stoi(positional[4]);
    if (n > 5) seed = static_cast<unsigned>(std::stoul(positional[5]));
    if (n > 6) threads = std::stoi(positional[6]);
    if (n > 7) max_rounds = std::stoi(positional[7]);
    if (n > 8) step = std::stod(positional[8]);
    if (n > 9) iters_per_move = std::stoi(positional[9]);
    if (batches <= 0 || search_ms <= 0 || max_rounds <= 0 || step <= 0 || !std::isfinite(step))
    {
        std::println(stderr, "[PROBE] invalid arguments");
        return 1;
    }
    if (seed == 0) seed = static_cast<unsigned>(std::time(nullptr));
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());

    int games = eval_matches;
    if (games % 2 != 0) --games;
    int q = std::max(1, games / 2);

    TunerEngine global_ai;
    global_ai.prepare(10, 40);
    double theta[NUM_PARAMS];
    if (!load_params(theta))
    {
        if (want_default)
        {
            default_params(theta);
            std::println(stderr, "[PROBE] warning: {} missing or invalid; using built-in defaults", param::filename(""));
        }
        else
        {
            std::println(stderr, "[PROBE] error: {} missing or invalid; provide a valid param file or pass --default.", param::filename(""));
            return 1;
        }
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        theta[i] = std::isfinite(theta[i]) ? theta[i] : 0.0;
    }
    double x[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i) x[i] = theta[i] / param_scale[i];

    double gradient_sum[NUM_PARAMS] = { 0 };
    double gradient_square_sum[NUM_PARAMS] = { 0 };
    int gradient_sign_sum[NUM_PARAMS] = { 0 };
    std::atomic<bool> view{ false };
    std::atomic<uint32_t> view_index{ 0 };
    std::mutex view_mutex;

    std::println("[PROBE] fixed theta: {} batches, {} games/batch ({} directions), {} rounds/match, {} search, step={:.4f}",
                batches, 2 * q, q, max_rounds, iters_per_move > 0 ? (std::to_string(iters_per_move) + " iters") : (std::to_string(search_ms) + "ms"), step);
    std::fflush(stdout);

    for (int k = 0; k < batches; ++k)
    {
        std::vector<MatchJob> jobs;
        jobs.reserve(2 * q);
        for (int j = 0; j < q; ++j)
        {
            uint64_t scenario_seed = splitmix64(seed
                ^ splitmix64(static_cast<uint64_t>(k) * 0x94D049BB133111EBULL + static_cast<uint64_t>(j)));
            uint64_t scenario_seed_a = splitmix64(scenario_seed);
            uint64_t scenario_seed_b = splitmix64(scenario_seed ^ 0x9E3779B97F4A7C15ULL);
            MatchJob a{}, b{};
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                int eps = rademacher(seed, k, j, static_cast<int>(i));
                double xp = x[i] + step * eps;
                double xm = x[i] - step * eps;
                a.p1[i] = xp * param_scale[i];
                a.p2[i] = xm * param_scale[i];
                b.p1[i] = xm * param_scale[i];
                b.p2[i] = xp * param_scale[i];
            }
            a.scenario_seed_p1 = scenario_seed_a;
            a.scenario_seed_p2 = scenario_seed_b;
            b.scenario_seed_p1 = scenario_seed_a;
            b.scenario_seed_p2 = scenario_seed_b;
            a.job_id = static_cast<size_t>(2) * j;
            b.job_id = static_cast<size_t>(2) * j + 1;
            jobs.push_back(a);
            jobs.push_back(b);
        }

        auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_mutex, view_index);
        double score = 0;
        double grad[NUM_PARAMS] = { 0 };
        int capped = 0;
        int cap_apl = 0;
        double avg_rounds = 0;
        for (int j = 0; j < q; ++j)
        {
            double rj = paired_reward(out[2 * j], out[2 * j + 1]);
            score += rj;
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                grad[i] += rj * rademacher(seed, k, j, static_cast<int>(i));
            }
            for (int g = 0; g < 2; ++g)
            {
                MatchOutcome const &outcome = out[2 * j + g];
                capped += outcome.capped;
                cap_apl += outcome.winner_reason == MatchResult::P1_CAP_APL
                    || outcome.winner_reason == MatchResult::P2_CAP_APL;
                avg_rounds += outcome.rounds;
            }
        }
        score /= q;
        avg_rounds /= 2 * q;
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            double const g = grad[i] / (2.0 * step * q);
            gradient_sum[i] += g;
            gradient_square_sum[i] += g * g;
            gradient_sign_sum[i] += g > 0 ? 1 : g < 0 ? -1 : 0;
        }
        std::println("[PROBE] batch {:3d} | score={:+.3f} | R={:.0f} C={} CA={}",
                    k, score, avg_rounds, capped, cap_apl);
    }

    std::println("[PROBE] parameter gradient summary (normalized coordinates):");
    std::println("  {:<14} {:12} {:12} {:12}", "parameter", "mean", "stddev", "sign_agree");
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        double const mean = gradient_sum[i] / batches;
        double variance = gradient_square_sum[i] / batches - mean * mean;
        variance = std::max(0.0, variance);
        double const sign_agreement = std::abs(static_cast<double>(gradient_sign_sum[i])) / batches;
        std::println("  {:<14} {:+.6e} {:+.6e} {:+.3f}", param_names[i], mean, std::sqrt(variance), sign_agreement);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

    if (argc > 1 && std::strcmp(argv[1], "probe") == 0)
    {
        return run_probe(argc, argv);
    }
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0)
    {
        return run_bench(argc, argv);
    }
    if (argc > 1 && std::strcmp(argv[1], "vs") == 0)
    {
        return run_vs(argc, argv);
    }

    std::vector<char const *> positional;
    bool want_default = false;
    bool want_zero = false;
    positional.push_back(argv[0]);
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--default") == 0)
        {
            want_default = true;
        }
        else if (std::strcmp(argv[i], "--fresh-zero") == 0)
        {
            want_zero = true;
        }
        else
        {
            positional.push_back(argv[i]);
        }
    }
    int const n = static_cast<int>(positional.size());

    int num_iters = 10000;
    int eval_matches = 14;
    int search_ms = 20;
    unsigned seed = 0;
    int threads = 1;
    int max_rounds = 3600;

    if (n > 1) num_iters = std::stoi(positional[1]);
    if (n > 2) eval_matches = std::stoi(positional[2]);
    if (n > 3) search_ms = std::stoi(positional[3]);
    if (n > 4) seed = static_cast<unsigned>(std::stoul(positional[4]));
    if (n > 5) threads = std::stoi(positional[5]);
    if (n > 6) max_rounds = std::stoi(positional[6]);
    if (n > 7) iters_per_move = std::stoi(positional[7]);
    if (num_iters <= 0 || eval_matches <= 0 || eval_matches == 1 || threads < 0
        || iters_per_move < 0)
    {
        std::println(stderr, "[TUNER] invalid arguments");
        return 1;
    }
    if (iters_per_move <= 0 && search_ms <= 0)
    {
        std::println(stderr, "[TUNER] search_ms must be > 0 when using time-based search");
        return 1;
    }
    if (threads == 0)
    {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    max_rounds = std::max(1, max_rounds);

    int games = eval_matches;
    if (games % 2 != 0)
    {
        --games;
        std::println("[TUNER] eval_matches {} is odd; using {} (even seat-swapped pairs)", eval_matches, games);
    }
    int q = std::max(1, games / 2);

    std::string data_file = param::tag_filename("tuner_data.bin", "");

    TunerEngine global_ai;
    global_ai.prepare(10, 40);

    double theta[NUM_PARAMS];
    double es_sigma = ES_SIGMA0;
    double es_m1[NUM_PARAMS] = { 0 };
    double es_m2[NUM_PARAMS] = { 0 };
    int resume_k = 0;
    CheckpointConfig saved_cfg;
    int load_result = load_state(data_file, resume_k, theta, es_sigma, es_m1, es_m2,
                                 saved_cfg);
    if (load_result == CHECKPOINT_OK && seed == 0)
    {
        seed = saved_cfg.seed;
    }
    if (seed == 0)
    {
        seed = static_cast<unsigned>(std::time(nullptr));
    }

    CheckpointConfig cfg;
    cfg.max_rounds = max_rounds;
    cfg.eval_matches = games;
    cfg.search_ms = search_ms;
    cfg.seed = seed;
    cfg.iters_per_move = iters_per_move;
    std::memcpy(cfg.param_scale, param_scale, sizeof(param_scale));
    std::memcpy(cfg.combo_table, combo_table, sizeof(combo_table));
    cfg.next_length = next_length;

    if (load_result == CHECKPOINT_OK)
    {
        if (!checkpoint_config_equal(saved_cfg, cfg))
        {
            std::println("[TUNER] Checkpoint configuration mismatch:\n"
                        "       saved: rounds={} games={} search_ms={} seed={} iters={}\n"
                        "       requested: rounds={} games={} search_ms={} seed={} iters={}\n"
                        "       Delete {} for a fresh run.",
                        saved_cfg.max_rounds, saved_cfg.eval_matches, saved_cfg.search_ms, saved_cfg.seed,
                        saved_cfg.iters_per_move,
                        cfg.max_rounds, cfg.eval_matches, cfg.search_ms, cfg.seed,
                        cfg.iters_per_move, data_file);
            return 1;
        }
        if (resume_k >= num_iters)
        {
            std::println("[TUNER] Warning: resume point {} >= num_iters {}; nothing to do.\n"
                        "       Delete {} for a fresh start.", resume_k, num_iters, data_file);
            return 0;
        }
        std::println("[TUNER] Resumed from iteration {} (overrides best_param)", resume_k);
    }
    else
    {
        if (load_result == CHECKPOINT_CORRUPT)
        {
            std::println(stderr, "[TUNER] warning: {} exists but is corrupt or an unsupported format; starting fresh from params.", data_file);
        }
        std::println("[TUNER] Starting fresh");
    }

    if (load_result != CHECKPOINT_OK)
    {
        if (want_zero)
        {
            double const zero[NUM_PARAMS] = {};
            std::memcpy(theta, zero, sizeof(zero));
            std::println("[TUNER] Fresh start from zero weights (--fresh-zero)");
        }
        else if (want_default)
        {
            default_params(theta);
            std::println("[TUNER] Fresh start from built-in defaults (--default)");
        }
        else if (load_params(theta))
        {
            std::println("[TUNER] Fresh warm start from {}", param::filename(""));
        }
        else
        {
            default_params(theta);
            std::println(stderr, "[TUNER] warning: {} missing or invalid; using built-in defaults (pass --fresh-zero to start from zero).", param::filename(""));
        }
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        theta[i] = std::isfinite(theta[i]) ? theta[i] : 0.0;
    }

    double best_theta[NUM_PARAMS];
    if (param::read(best_theta, NUM_PARAMS, ""))
    {
        std::println("[TUNER] Incumbent (validated best) loaded from {}", param::filename(""));
    }
    else
    {
        std::memcpy(best_theta, theta, sizeof(best_theta));
        std::println("[TUNER] No incumbent; best_param.bin will be written after the first successful challenge");
    }

    double x[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        x[i] = theta[i] / param_scale[i];
    }

    int stall_batches = 0;
    bool rollback_pending = false;
    int restarts = 0;

    std::println("[TUNER] paired mirrored ES: {} iters, {} games/batch ({} directions), {} rounds/match, {} search, seed {}, threads={}, sigma_tau={:.0f}, lr_horizon={:.0f}",
                num_iters, 2 * q, q, max_rounds,
                iters_per_move > 0 ? (std::to_string(iters_per_move) + " iters") : (std::to_string(search_ms) + "ms"), seed, threads,
                ES_SIGMA_TAU, ES_LR_HORIZON);
    std::fflush(stdout);

    std::atomic<bool> view{ false };
    std::atomic<uint32_t> view_index{ 0 };
    std::mutex view_mutex;

    std::thread stdin_thread([&]()
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line == "view")
            {
                view = true;
                std::print("\033[2J");
            }
            else if (line.empty())
            {
                view = false;
                std::lock_guard<std::mutex> lock(view_mutex);
                view_index = 0;
            }
        }
    });
    stdin_thread.detach();

    auto start_time = std::chrono::steady_clock::now();

    for (int k = resume_k; k < num_iters; ++k)
    {
        double step = es_sigma;
        std::vector<MatchJob> jobs;
        jobs.reserve(2 * q);
        for (int j = 0; j < q; ++j)
        {
            uint64_t scenario_seed = splitmix64(seed
                ^ splitmix64(static_cast<uint64_t>(k) * 0x94D049BB133111EBULL + static_cast<uint64_t>(j)));
            uint64_t scenario_seed_a = splitmix64(scenario_seed);
            uint64_t scenario_seed_b = splitmix64(scenario_seed ^ 0x9E3779B97F4A7C15ULL);
            MatchJob a{}, b{};
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                int eps = rademacher(seed, k, j, static_cast<int>(i));
                double xp = x[i] + step * eps;
                double xm = x[i] - step * eps;
                a.p1[i] = xp * param_scale[i];
                a.p2[i] = xm * param_scale[i];
                b.p1[i] = xm * param_scale[i];
                b.p2[i] = xp * param_scale[i];
            }
            a.scenario_seed_p1 = scenario_seed_a;
            a.scenario_seed_p2 = scenario_seed_b;
            b.scenario_seed_p1 = scenario_seed_a;
            b.scenario_seed_p2 = scenario_seed_b;
            a.job_id = static_cast<size_t>(2) * j;
            b.job_id = static_cast<size_t>(2) * j + 1;
            jobs.push_back(a);
            jobs.push_back(b);
        }

        auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_mutex, view_index);

        double score = 0;
        double grad[NUM_PARAMS] = { 0 };
        double avg_rounds = 0;
        int deaths = 0;
        int capped_games = 0;
        int capped_apl_games = 0;
        int nonzero_directions = 0;
        double reward_sum = 0;
        double reward_sq_sum = 0;
        for (int j = 0; j < q; ++j)
        {
            double rj = paired_reward(out[2 * j], out[2 * j + 1]);
            score += rj;
            reward_sum += rj;
            reward_sq_sum += rj * rj;
            if (rj != 0.0)
            {
                ++nonzero_directions;
            }
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                grad[i] += rj * rademacher(seed, k, j, static_cast<int>(i));
            }
            for (int g = 0; g < 2; ++g)
            {
                avg_rounds += out[2 * j + g].rounds;
                deaths += out[2 * j + g].dead1 + out[2 * j + g].dead2;
                capped_games += out[2 * j + g].capped;
                capped_apl_games += out[2 * j + g].winner_reason == MatchResult::P1_CAP_APL
                    || out[2 * j + g].winner_reason == MatchResult::P2_CAP_APL;
            }
        }
        score /= q;
        avg_rounds /= 2 * q;

        double dx[NUM_PARAMS] = { 0 };
        double grad_norm = 0;
        bool zero_gradient = true;
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            if (grad[i] != 0.0)
            {
                zero_gradient = false;
                break;
            }
        }
        if (zero_gradient)
        {
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                es_m1[i] = ES_BETA1 * es_m1[i];
                es_m2[i] = ES_BETA2 * es_m2[i];
            }
        }
        else
        {
            adam_update(grad, q, es_sigma, k, es_m1, es_m2, dx, grad_norm);
        }
        es_sigma = std::max(ES_SIGMA_FLOOR, ES_SIGMA0 * std::exp(-static_cast<double>(k) / ES_SIGMA_TAU));

        double dx_norm = 0;
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            dx_norm += dx[i] * dx[i];
        }
        dx_norm = std::sqrt(dx_norm);

        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            x[i] += dx[i];
            if (!std::isfinite(x[i]))
            {
                x[i] = 0.0;
            }
            theta[i] = x[i] * param_scale[i];
        }

        if (!save_state(data_file, k + 1, theta, es_sigma, es_m1, es_m2, cfg))
        {
            std::println(stderr, "[TUNER] warning: failed to write checkpoint {}", data_file);
        }
        durable_write_doubles("current_param.bin", theta, NUM_PARAMS);

        bool promoted = false;
        if ((k + 1) % CHALLENGE_EVERY == 0)
        {
            double wins_equiv = 0.0;
            int played = 0;
            for (int pairs = 0; pairs < CHALLENGE_MAX_PAIRS; pairs += CHALLENGE_PAIRS)
            {
                double stage = run_challenge(theta, best_theta, threads, search_ms, max_rounds,
                                             global_ai, view, view_mutex, view_index,
                                             CHALLENGE_SEED_BASE
                                                 + static_cast<uint64_t>(k + 1) * 0x9E3779B97F4A7C15ULL
                                                 + static_cast<uint64_t>(pairs),
                                             CHALLENGE_PAIRS);
                wins_equiv += 2.0 * CHALLENGE_PAIRS * (1.0 + stage) / 2.0;
                played += 2 * CHALLENGE_PAIRS;
                double phat = wins_equiv / static_cast<double>(played);
                double z = 1.6448536269514722;
                double denom = 1.0 + z * z / played;
                double mid = phat + z * z / (2.0 * played);
                double margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / played
                                              + z * z / (4.0 * played * played));
                double p_lo = (mid - margin) / denom;
                std::println("[ES]   challenge @ iter {}: stage={:+.3f} (pairs {}..{}), games={}, win-equiv={:.1f}, LB={:.1f}%",
                             k + 1, stage, pairs + 1, pairs + CHALLENGE_PAIRS, played,
                             wins_equiv, 100.0 * p_lo);
                if (p_lo * 100.0 >= CHALLENGE_PROMOTE_LEVEL)
                {
                    std::memcpy(best_theta, theta, sizeof(best_theta));
                    durable_write_doubles("best_param.bin", best_theta, NUM_PARAMS);
                    std::println("[ES]   promoted candidate to best_param.bin ({} games, {:.1f}% LB)",
                                 played, 100.0 * p_lo);
                    promoted = true;
                    break;
                }
            }
            if (!promoted)
            {
                std::println("[ES]   challenge not promoted after {} games", played);
            }
        }

        if (promoted)
        {
            stall_batches = 0;
        }
        else
        {
            ++stall_batches;
        }
        if (!promoted && (k + 1) % CHALLENGE_EVERY == 0
            && stall_batches >= STALL_WINDOW && restarts < MAX_RESTARTS)
        {
            std::memcpy(theta, best_theta, sizeof(best_theta));
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                x[i] = theta[i] / param_scale[i];
                es_m1[i] = 0.0;
                es_m2[i] = 0.0;
            }
            es_sigma = RESTART_SIGMA;
            stall_batches = 0;
            ++restarts;
            std::println("[ES]   STALL: {} batches without promotion; rolled back to incumbent, reset Adam, sigma={:.3f} (restart {}/{})",
                         STALL_WINDOW, RESTART_SIGMA, restarts, MAX_RESTARTS);
        }

        double dist_inc_norm = 0;
        double dist_inc_phys = 0;
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            double d = x[i] - best_theta[i] / param_scale[i];
            dist_inc_norm += d * d;
            double dp = theta[i] - best_theta[i];
            dist_inc_phys = std::max(dist_inc_phys, std::fabs(dp));
        }
        dist_inc_norm = std::sqrt(dist_inc_norm);

        double reward_sd = std::sqrt(std::max(0.0, reward_sq_sum / q - (reward_sum / q) * (reward_sum / q)));

        auto now = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(now - start_time).count();
        std::println("[ES]   iter {:5d} | score={:+.3f} sd={:.3f} nz={}/{} | g={:.3f} dx={:.3f} | sigma={:.4f} | R={:.0f} D={} C={} CA={} | dInc={:.3f}/{:.4f} | {:.1f}s",
                     k, score, reward_sd, nonzero_directions, q, grad_norm, dx_norm, es_sigma,
                     avg_rounds, deaths, capped_games, capped_apl_games,
                     dist_inc_norm, dist_inc_phys, sec);
        std::fflush(stdout);
    }

    durable_write_doubles("current_param.bin", theta, NUM_PARAMS);
    if (!save_state(data_file, num_iters, theta, es_sigma, es_m1, es_m2, cfg))
    {
        std::println(stderr, "[TUNER] warning: failed to write checkpoint {}", data_file);
    }

    std::println("\n[TUNER] Done. {} iterations completed", num_iters);
    std::println("[TUNER] Current params saved to {}", "current_param.bin");
    if (param::read(best_theta, NUM_PARAMS, ""))
    {
        std::println("[TUNER] Validated best saved to {}", param::filename(""));
    }
    else
    {
        std::println("[TUNER] No validated best was promoted during this run");
    }
    std::println("[TUNER] theta:");
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        std::println("  {:<14} {:+.6f}", param_names[i], theta[i]);
    }
    return 0;
}
