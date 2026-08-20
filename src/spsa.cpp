// Usage:
//   spsa [num_iters] [eval_matches] [search_ms] [seed] [threads] [algo] [max_rounds] [iters_per_move]
//   spsa probe [batches] [eval_matches] [search_ms] [seed] [threads] [max_rounds] [step] [iters_per_move]

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
#include <numbers>

#include "tetris_core.h"
#include "search_tspin.h"
#include "ai_zzz.h"
#include "rule_toj.h"
#include "param.h"

static int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
static int const combo_table_max = 10;

size_t const NUM_PARAMS = 29;
static int iters_per_move = 0;

static double const param_scale[NUM_PARAMS] = {
    2.0, 1.5, 2.5, 2.5, 1.5, 2.0, 0.05, 0.05, 1.0, 1.0,
    0.01, 0.3, 2.0, 0.001, 0.001, 0.4, 0.2, 0.2, 0.3, 0.1,
    0.1, 0.002, 0.08, 0.2, 0.1, 0.1, 0.3, 0.2, 0.15,
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
    uint64_t h = splitmix64(seed ^ splitmix64((uint64_t)k * 0x9E3779B97F4A7C15ULL
                                              + (uint64_t)j * 0xBF58476D1CE4E5B9ULL
                                              + (uint64_t)i));
    return (int)(h & 1) ? 1 : -1;
}

struct Scenario
{
    std::deque<char> pieces;
    std::deque<int> holes;
};

static Scenario make_scenario(uint64_t seed)
{
    std::mt19937 rng((unsigned)splitmix64(seed));
    std::string bag = "IJLOSTZ";
    Scenario s;
    while (s.pieces.size() < 4096)
    {
        std::shuffle(bag.begin(), bag.end(), rng);
        for (char c : bag)
        {
            s.pieces.push_back(c);
        }
    }
    while (s.holes.size() < 1024)
    {
        s.holes.push_back((int)(rng() % 10));
    }
    return s;
}

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

// ---- optimizer configuration (normalized coordinates) ----
static double const SPSA_A = 35.0;
static double const SPSA_ALPHA = 0.602;
static double const SPSA_C = 0.30;
static double const SPSA_GAMMA = 0.101;
static double const SPSA_CK_FLOOR = 0.08;
static double const SPSA_RATE = 0.10;

// Paired mirrored ES (OpenAI-ES style with antithetic seat-swapped pairs):
static double const ES_SIGMA0 = 0.30;
static double const ES_SIGMA_FLOOR = 0.08;
static double const ES_SIGMA_TAU = 2000.0;
static double const ES_LR0 = 0.025;
static double const ES_LR1 = 0.004;
static double const ES_LR_HORIZON = 5000.0;
static double const ES_BETA1 = 0.9;
static double const ES_BETA2 = 0.999;
static double const ES_EPS = 1e-8;
static double const ES_DX_MAX = 0.04;    // per-coordinate update clip
static double const ES_DX_L2_MAX = 0.10; // update L2 clip

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
    Scenario *scenario = nullptr;

    int next_length = 6;
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
        ai.status()->under_attack = (int)std::accumulate(recv_attack.begin(), recv_attack.end(), 0);
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
        if (result.target == nullptr)
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
            uint32_t hole = 1u << scenario->holes.front();
            scenario->holes.pop_front();
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
    int winner;      // +1 = seat1, -1 = seat2, 0 = draw
    bool dead1;
    bool dead2;
    bool capped;
    int winner_reason;
    int rounds;
    double app1, app2;  // attack / piece
    double apl1, apl2;  // attack / cleared line
};

static MatchResult play_match(BotInstance &b1, BotInstance &b2, int max_rounds = 1000,
                              std::function<void()> view_cb = nullptr)
{
    for (int round = 1; round <= max_rounds; ++round)
    {
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

    MatchResult r;
    r.dead1 = b1.dead;
    r.dead2 = b2.dead;
    r.capped = !b1.dead && !b2.dead;
    r.rounds = std::max(b1.total_block, b2.total_block);
    r.app1 = b1.total_block > 0 ? (double)b1.total_attack / b1.total_block : 0.0;
    r.app2 = b2.total_block > 0 ? (double)b2.total_attack / b2.total_block : 0.0;
    r.apl1 = b1.total_clear > 0 ? (double)b1.total_attack / b1.total_clear : 0.0;
    r.apl2 = b2.total_clear > 0 ? (double)b2.total_attack / b2.total_clear : 0.0;
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
    uint64_t scenario_seed;
    size_t job_id;
    bool swapped;
};

struct MatchOutcome
{
    int winner;   // +1 = p1, -1 = p2, 0 = draw
    bool dead1;
    bool dead2;
    bool capped;
    int winner_reason;
    int rounds;
    double app1, app2;
    double apl1, apl2;
};

// Runs every job to completion (no early cancellation); outcome.winner is
// from p1's perspective.
static std::vector<MatchOutcome> run_batch(std::vector<MatchJob> const &jobs, int threads, int search_ms, int max_rounds,
                                           SpsaEngine &global_ai, std::atomic<bool> &view,
                                           std::atomic<uint32_t> &view_index)
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
            Scenario scenario = make_scenario(jobs[idx].scenario_seed);
            BotInstance b1(global_ai), b2(global_ai);
            b1.scenario = b2.scenario = &scenario;
            if (iters_per_move > 0)
            {
                b1.search_budget = b2.search_budget = m_tetris::SearchBudget::by_iterations((size_t)iters_per_move);
            }
            else
            {
                b1.search_budget = b2.search_budget = m_tetris::SearchBudget{ (time_t)search_ms };
            }
            b1.init(jobs[idx].p1);
            b2.init(jobs[idx].p2);
            std::function<void()> view_cb = [&, idx]()
            {
                if (!view.load(std::memory_order_relaxed))
                {
                    uint32_t mine = (uint32_t)idx;
                    view_index.compare_exchange_strong(mine, 0);
                    return;
                }
                uint32_t zero = 0;
                view_index.compare_exchange_strong(zero, (uint32_t)idx);
                if (view_index.load(std::memory_order_relaxed) != (uint32_t)idx)
                {
                    return;
                }
                if (jobs[idx].swapped)
                {
                    render_view(b1, b2, "MINUS", "PLUS");
                }
                else
                {
                    render_view(b1, b2, "PLUS", "MINUS");
                }
            };
            MatchResult r = play_match(b1, b2, max_rounds, view_cb);
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

static bool load_state(std::string const &data_file, int &algo, int &resume_k, double *theta,
                       double &es_sigma, double *es_m1, double *es_m2,
                       int &saved_max_rounds, int &saved_eval_matches, int &saved_search_ms,
                       unsigned &saved_seed, bool &has_run_config)
{
    saved_max_rounds = 0;
    saved_eval_matches = 0;
    saved_search_ms = 0;
    saved_seed = 0;
    has_run_config = false;
    std::ifstream ifs(data_file, std::ios::binary);
    if (!ifs.good())
    {
        return false;
    }
    char magic[8] = { 0 };
    ifs.read(magic, 8);
    if (std::memcmp(magic, "TETOPT3", 8) == 0 || std::memcmp(magic, "TETOPT2", 8) == 0)
    {
        int ver = 0;
        ifs.read(reinterpret_cast<char *>(&ver), sizeof(ver));
        ifs.read(reinterpret_cast<char *>(&algo), sizeof(algo));
        ifs.read(reinterpret_cast<char *>(&resume_k), sizeof(resume_k));
        if (std::memcmp(magic, "TETOPT3", 8) == 0 && ver >= 3)
        {
            ifs.read(reinterpret_cast<char *>(&saved_max_rounds), sizeof(saved_max_rounds));
            ifs.read(reinterpret_cast<char *>(&saved_eval_matches), sizeof(saved_eval_matches));
            ifs.read(reinterpret_cast<char *>(&saved_search_ms), sizeof(saved_search_ms));
            ifs.read(reinterpret_cast<char *>(&saved_seed), sizeof(saved_seed));
            has_run_config = true;
        }
        ifs.read(reinterpret_cast<char *>(theta), NUM_PARAMS * sizeof(double));
        if (ver >= 2)
        {
            ifs.read(reinterpret_cast<char *>(&es_sigma), sizeof(es_sigma));
            ifs.read(reinterpret_cast<char *>(es_m1), NUM_PARAMS * sizeof(double));
            ifs.read(reinterpret_cast<char *>(es_m2), NUM_PARAMS * sizeof(double));
        }
        return true;
    }
    ifs.clear();
    ifs.seekg(0);
    ifs.read(reinterpret_cast<char *>(&resume_k), sizeof(resume_k));
    ifs.read(reinterpret_cast<char *>(theta), NUM_PARAMS * sizeof(double));
    algo = 0;
    return true;
}

static void save_state(std::string const &data_file, int algo, int k, double const *theta,
                       double es_sigma, double const *es_m1, double const *es_m2,
                       int max_rounds, int eval_matches, int search_ms, unsigned seed)
{
    std::string tmp = data_file + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write("TETOPT3", 8);
        int ver = 3;
        ofs.write(reinterpret_cast<char const *>(&ver), sizeof(ver));
        ofs.write(reinterpret_cast<char const *>(&algo), sizeof(algo));
        ofs.write(reinterpret_cast<char const *>(&k), sizeof(k));
        ofs.write(reinterpret_cast<char const *>(&max_rounds), sizeof(max_rounds));
        ofs.write(reinterpret_cast<char const *>(&eval_matches), sizeof(eval_matches));
        ofs.write(reinterpret_cast<char const *>(&search_ms), sizeof(search_ms));
        ofs.write(reinterpret_cast<char const *>(&seed), sizeof(seed));
        ofs.write(reinterpret_cast<char const *>(theta), NUM_PARAMS * sizeof(double));
        ofs.write(reinterpret_cast<char const *>(&es_sigma), sizeof(es_sigma));
        ofs.write(reinterpret_cast<char const *>(es_m1), NUM_PARAMS * sizeof(double));
        ofs.write(reinterpret_cast<char const *>(es_m2), NUM_PARAMS * sizeof(double));
    }
    std::rename(tmp.c_str(), data_file.c_str());
}

static int run_probe(int argc, char *argv[])
{
    int batches = 8;
    int eval_matches = 14;
    int search_ms = 20;
    unsigned seed = 555;
    int threads = 14;
    int max_rounds = 3600;
    double step = 0.30;
    if (argc > 2) batches = std::stoi(argv[2]);
    if (argc > 3) eval_matches = std::stoi(argv[3]);
    if (argc > 4) search_ms = std::stoi(argv[4]);
    if (argc > 5) seed = (unsigned)std::stoul(argv[5]);
    if (argc > 6) threads = std::stoi(argv[6]);
    if (argc > 7) max_rounds = std::stoi(argv[7]);
    if (argc > 8) step = std::stod(argv[8]);
    if (argc > 9) iters_per_move = std::stoi(argv[9]);
    if (batches <= 0 || search_ms <= 0 || max_rounds <= 0 || step <= 0 || !std::isfinite(step))
    {
        std::fprintf(stderr, "[PROBE] invalid arguments\n");
        return 1;
    }
    if (seed == 0) seed = (unsigned)std::time(nullptr);
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());

    int games = eval_matches;
    if (games % 2 != 0) --games;
    int q = std::max(1, games / 2);

    SpsaEngine global_ai;
    global_ai.prepare(10, 40);
    double theta[NUM_PARAMS];
    load_params(theta);
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

    std::printf("[PROBE] fixed theta: %d batches, %d games/batch (%d directions), %d rounds/match, %s search, step=%.4f\n",
                batches, 2 * q, q, max_rounds, iters_per_move > 0 ? (std::to_string(iters_per_move) + " iters").c_str() : (std::to_string(search_ms) + "ms").c_str(), step);
    std::fflush(stdout);

    for (int k = 0; k < batches; ++k)
    {
        std::vector<MatchJob> jobs;
        jobs.reserve(2 * q);
        for (int j = 0; j < q; ++j)
        {
            uint64_t scenario_seed = splitmix64(seed
                ^ splitmix64((uint64_t)k * 0x94D049BB133111EBULL + (uint64_t)j));
            MatchJob a{}, b{};
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                int eps = rademacher(seed, k, j, (int)i);
                double xp = x[i] + step * eps;
                double xm = x[i] - step * eps;
                a.p1[i] = xp * param_scale[i];
                a.p2[i] = xm * param_scale[i];
                b.p1[i] = xm * param_scale[i];
                b.p2[i] = xp * param_scale[i];
            }
            a.scenario_seed = b.scenario_seed = scenario_seed;
            a.job_id = (size_t)2 * j;
            b.job_id = (size_t)2 * j + 1;
            a.swapped = false;
            b.swapped = true;
            jobs.push_back(a);
            jobs.push_back(b);
        }

        auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_index);
        double score = 0;
        double grad[NUM_PARAMS] = { 0 };
        int capped = 0;
        int cap_apl = 0;
        double avg_rounds = 0;
        for (int j = 0; j < q; ++j)
        {
            int wA = out[2 * j].winner;
            int wB = out[2 * j + 1].winner;
            double rj = 0.5 * (wA - wB);
            score += rj;
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                grad[i] += rj * rademacher(seed, k, j, (int)i);
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
        std::printf("[PROBE] batch %3d | score=%+.3f | R=%.0f C=%d CA=%d\n",
                    k, score, avg_rounds, capped, cap_apl);
    }

    std::printf("[PROBE] parameter gradient summary (normalized coordinates):\n");
    std::printf("  %-14s %12s %12s %12s\n", "parameter", "mean", "stddev", "sign_agree");
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        double const mean = gradient_sum[i] / batches;
        double variance = gradient_square_sum[i] / batches - mean * mean;
        variance = std::max(0.0, variance);
        double const sign_agreement = std::abs((double)gradient_sign_sum[i]) / batches;
        std::printf("  %-14s %+.6e %+.6e %+.3f\n", param_names[i], mean, std::sqrt(variance), sign_agreement);
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

    int num_iters = 10000;
    int eval_matches = 14;
    int search_ms = 20;
    unsigned seed = 0;
    int threads = 1; // 0 = auto (hardware concurrency)
    int algo = 0;    // 0 = SPSA, 1 = paired mirrored ES
    int max_rounds = 1000;

    if (argc > 1) num_iters = std::stoi(argv[1]);
    if (argc > 2) eval_matches = std::stoi(argv[2]);
    if (argc > 3) search_ms = std::stoi(argv[3]);
    if (argc > 4) seed = (unsigned)std::stoul(argv[4]);
    if (argc > 5) threads = std::stoi(argv[5]);
    if (argc > 6) algo = std::stoi(argv[6]);
    if (argc > 7) max_rounds = std::stoi(argv[7]);
    if (argc > 8) iters_per_move = std::stoi(argv[8]);
    if (seed == 0) seed = (unsigned)std::time(nullptr);
    if (threads == 0)
    {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    max_rounds = std::max(1, max_rounds);

    int games = eval_matches;
    if (games % 2 != 0)
    {
        --games;
        std::printf("[SPSA] eval_matches %d is odd; using %d (even seat-swapped pairs)\n", eval_matches, games);
    }
    int q = std::max(1, games / 2); // directions per batch

    std::string data_file = param::tag_filename("spsa_data.bin", "");

    SpsaEngine global_ai;
    global_ai.prepare(10, 40);

    double theta[NUM_PARAMS];
    load_params(theta);
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        theta[i] = std::isfinite(theta[i]) ? theta[i] : 0.0;
    }

    double es_sigma = ES_SIGMA0;
    double es_m1[NUM_PARAMS] = { 0 };
    double es_m2[NUM_PARAMS] = { 0 };
    int resume_k = 0, saved_algo = -1;
    int saved_max_rounds = 0, saved_eval_matches = 0, saved_search_ms = 0;
    unsigned saved_seed = 0;
    bool has_saved_config = false;
    if (load_state(data_file, saved_algo, resume_k, theta, es_sigma, es_m1, es_m2,
                   saved_max_rounds, saved_eval_matches, saved_search_ms, saved_seed, has_saved_config))
    {
        if (saved_algo != algo)
        {
            std::printf("[SPSA] Checkpoint is for algo %d but algo %d was requested.\n"
                        "       Delete %s for a fresh start.\n", saved_algo, algo, data_file.c_str());
            return 1;
        }
        if (has_saved_config && (saved_max_rounds != max_rounds || saved_eval_matches != games
            || saved_search_ms != search_ms || saved_seed != seed))
        {
            std::printf("[SPSA] Checkpoint configuration mismatch:\n"
                        "       saved: rounds=%d games=%d search_ms=%d seed=%u\n"
                        "       requested: rounds=%d games=%d search_ms=%d seed=%u\n"
                        "       Delete %s for a fresh run.\n",
                        saved_max_rounds, saved_eval_matches, saved_search_ms, saved_seed,
                        max_rounds, games, search_ms, seed, data_file.c_str());
            return 1;
        }
        if (!has_saved_config)
        {
            std::printf("[SPSA] Warning: legacy checkpoint has no run configuration; verify rounds=%d, games=%d, search_ms=%d, seed=%u.\n",
                        max_rounds, games, search_ms, seed);
        }
        if (resume_k >= num_iters)
        {
            std::printf("[SPSA] Warning: resume point %d >= num_iters %d; nothing to do.\n"
                        "       Delete %s for a fresh start.\n", resume_k, num_iters, data_file.c_str());
            return 0;
        }
        std::printf("[SPSA] Resumed from iteration %d (overrides best_io_param)\n", resume_k);
    }
    else
    {
        std::printf("[SPSA] Starting fresh\n");
    }

    double x[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        x[i] = theta[i] / param_scale[i];
    }

    std::printf("[SPSA] %s: %d iters, %d games/batch (%d directions), %d rounds/match, %s search, seed %u, threads=%d\n",
                algo == 1 ? "paired mirrored ES" : "corrected SPSA", num_iters, 2 * q, q, max_rounds,
                iters_per_move > 0 ? (std::to_string(iters_per_move) + " iters").c_str() : (std::to_string(search_ms) + "ms").c_str(), seed, threads);
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
        // ---- build the batch: q directions, 2 seat-swapped games each ----
        double step = algo == 1
            ? es_sigma
            : std::max(SPSA_CK_FLOOR, SPSA_C / std::pow(k + 1.0, SPSA_GAMMA));
        std::vector<MatchJob> jobs;
        jobs.reserve(2 * q);
        for (int j = 0; j < q; ++j)
        {
            uint64_t scenario_seed = splitmix64(seed
                ^ splitmix64((uint64_t)k * 0x94D049BB133111EBULL + (uint64_t)j));
            MatchJob a{}, b{};
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                int eps = rademacher(seed, k, j, (int)i);
                double xp = x[i] + step * eps;
                double xm = x[i] - step * eps;
                a.p1[i] = xp * param_scale[i];
                a.p2[i] = xm * param_scale[i];
                b.p1[i] = xm * param_scale[i];
                b.p2[i] = xp * param_scale[i];
            }
            a.scenario_seed = b.scenario_seed = scenario_seed;
            a.job_id = (size_t)2 * j;
            b.job_id = (size_t)2 * j + 1;
            a.swapped = false;
            b.swapped = true;
            jobs.push_back(a);
            jobs.push_back(b);
        }

        auto out = run_batch(jobs, threads, search_ms, max_rounds, global_ai, view, view_index);

        // ---- rewards: r_j = (wA - wB)/2, both from the x+eps perspective ----
        double score = 0;
        double grad[NUM_PARAMS] = { 0 };
        double avg_rounds = 0;
        int deaths = 0;
        int capped_games = 0;
        int capped_apl_games = 0;
        for (int j = 0; j < q; ++j)
        {
            int wA = out[2 * j].winner;     // x+eps in seat 1
            int wB = out[2 * j + 1].winner; // x-eps in seat 1 (swapped)
            double rj = 0.5 * (wA - wB);
            score += rj;
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                grad[i] += rj * rademacher(seed, k, j, (int)i);
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
        if (algo == 1)
        {
            // ---- paired mirrored ES + Adam (normalized space) ----
            double g[NUM_PARAMS];
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                g[i] = grad[i] / (2.0 * q * es_sigma);
            }
            double lr = ES_LR1 + 0.5 * (ES_LR0 - ES_LR1)
                * (1.0 + std::cos(std::numbers::pi * std::min<double>(k, ES_LR_HORIZON) / ES_LR_HORIZON));
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
            es_sigma = std::max(ES_SIGMA_FLOOR, ES_SIGMA0 * std::exp(-(double)k / ES_SIGMA_TAU));
        }
        else
        {
            // ---- corrected multi-direction SPSA (normalized space) ----
            double ck = step;
            double ak = SPSA_A / std::pow(k + 1.0 + SPSA_A, SPSA_ALPHA);
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                dx[i] = SPSA_RATE * ak * param_rates[i] * grad[i] / (2.0 * ck * q);
            }
        }

        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            x[i] += dx[i];
            if (!std::isfinite(x[i]))
            {
                x[i] = 0.0;
            }
            theta[i] = x[i] * param_scale[i];
        }

        save_state(data_file, algo, k + 1, theta, es_sigma, es_m1, es_m2,
                   max_rounds, games, search_ms, seed);
        param::write(theta, NUM_PARAMS, "");
        if ((k + 1) % 10 == 0 || k == resume_k)
        {
            auto now = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(now - start_time).count();
            if (algo == 1)
            {
                std::printf("[ES]   iter %5d | score=%+.3f | sigma=%.4f | R=%.0f D=%d C=%d CA=%d | %.1fs\n",
                            k, score, es_sigma, avg_rounds, deaths, capped_games, capped_apl_games, sec);
            }
            else
            {
                double ck = step;
                double ak = SPSA_A / std::pow(k + 1.0 + SPSA_A, SPSA_ALPHA);
                std::printf("[SPSA] iter %5d | score=%+.3f | ak=%.4f ck=%.4f | R=%.0f D=%d C=%d CA=%d | %.1fs\n",
                            k, score, ak, ck, avg_rounds, deaths, capped_games, capped_apl_games, sec);
            }
        }
        std::fflush(stdout);
    }

    param::write(theta, NUM_PARAMS, "");
    save_state(data_file, algo, num_iters, theta, es_sigma, es_m1, es_m2,
               max_rounds, games, search_ms, seed);

    std::printf("\n[SPSA] Done. %d iterations completed\n", num_iters);
    std::printf("[SPSA] Params saved to %s\n", param::filename("").c_str());
    std::printf("[SPSA] theta:\n");
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        std::printf("  %-14s %+.6f\n", param_names[i], theta[i]);
    }
    return 0;
}
