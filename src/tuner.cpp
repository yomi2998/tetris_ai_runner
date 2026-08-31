
#include <ctime>
#include <cstring>
#include <fstream>
#include <thread>
#include <iostream>
#include <chrono>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <limits>
#include <mutex>
#include <numbers>
#include <print>
#include <string>
#include <vector>

#include "param.h"
#include "tuner_match.h"

namespace stat_util
{
    inline constexpr double Z_ONE_SIDED_95 = 1.6448536269514722;

    inline double normal_quantile_upper(double p)
    {
        double const target = 2.0 * p;
        double lo = 0.0;
        double hi = 10.0;
        if (target <= std::erfc(hi / std::sqrt(2.0)))
        {
            return hi;
        }
        for (int i = 0; i < 200; ++i)
        {
            double const mid = 0.5 * (lo + hi);
            if (std::erfc(mid / std::sqrt(2.0)) > target)
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }
        return 0.5 * (lo + hi);
    }

    inline double wilson_lower_bound(double wins_equiv, int games, double z)
    {
        if (games <= 0 || wins_equiv < 0.0)
        {
            return 0.0;
        }
        double const n = static_cast<double>(games);
        double const phat = std::min(1.0, std::max(0.0, wins_equiv / n));
        double const denom = 1.0 + z * z / n;
        double const mid = phat + z * z / (2.0 * n);
        double const margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / n
                                            + z * z / (4.0 * n * n));
        return (mid - margin) / denom;
    }

    inline void wilson_interval(double wins_equiv, int games, double z, double &lo, double &hi)
    {
        lo = 0.0;
        hi = 1.0;
        if (games <= 0)
        {
            return;
        }
        double const n = static_cast<double>(games);
        double const phat = std::min(1.0, std::max(0.0, wins_equiv / n));
        double const denom = 1.0 + z * z / n;
        double const mid = phat + z * z / (2.0 * n);
        double const margin = z * std::sqrt(std::max(0.0, phat * (1.0 - phat)) / n
                                            + z * z / (4.0 * n * n));
        lo = std::max(0.0, (mid - margin) / denom);
        hi = std::min(1.0, (mid + margin) / denom);
    }

    inline double look_z(int look_index, int looks_before_final, double alpha_final)
    {
        constexpr double z_interim = 3.0;
        if (look_index > looks_before_final)
        {
            return normal_quantile_upper(alpha_final);
        }
        return z_interim;
    }
}

static double const ES_SIGMA0 = 0.30;
static double const ES_SIGMA_FLOOR = 0.08;
static double const ES_SIGMA_TAU = 2000.0;
static double const ES_PROMOTION_SIGMA = ES_SIGMA0;
static double const ES_LR0 = 0.025;
static double const ES_LR1 = 0.004;
static double const ES_LR_HORIZON = 5000.0;
static double const ES_BETA1 = 0.9;
static double const ES_BETA2 = 0.999;
static double const ES_EPS = 1e-8;
static double const ES_DX_MAX = 0.04;
static double const ES_DX_L2_MAX = 0.10;

static double const ES_RELATIVE_PARAM_SCALE = 0.025;

static uint64_t const CHALLENGE_SEED_BASE = 0x123456789ABCDEF0ULL;
static int const CHALLENGE_EVERY = 50;
static int const CHALLENGE_PAIRS = 4;
static int const CHALLENGE_MAX_PAIRS = 32;
static double const CHALLENGE_PROMOTE_LB = 0.50;
static double const CHALLENGE_ALPHA = 0.05;
static int const CHALLENGE_TOTAL_LOOKS = CHALLENGE_MAX_PAIRS / CHALLENGE_PAIRS;

static int const STALL_WINDOW = 200;
static int const MAX_RESTART_LEVEL = 4;
static double const RESTART_SIGMA0 = 0.40;
static double const RESTART_SIGMA_GROWTH = 1.25;
static double const RESTART_SIGMA_MAX = 0.65;
static double const RESTART_SIGMA_TAU = 500.0;
static double const RESTART_KICK_L2_0 = 0.50;
static double const RESTART_KICK_GROWTH = 1.50;
static double const RESTART_KICK_L2_MAX = 1.50;

namespace tmatch = tuner_match;

using TunerEngine = tmatch::TunerEngine;
using MatchJob = tmatch::MatchJob;
using MatchOutcome = tmatch::MatchOutcome;
using MatchResult = tmatch::MatchResult;

constexpr size_t NUM_PARAMS = tmatch::NUM_PARAMS;
inline constexpr double const (&param_scale)[NUM_PARAMS] = tmatch::param_scale;
inline constexpr int const next_length = tmatch::next_length;
inline constexpr int const combo_table_max = tmatch::combo_table_max;
inline int const (&combo_table)[combo_table_max] = tmatch::combo_table;
inline char const *const (&param_names)[NUM_PARAMS] = tmatch::param_names;

using tmatch::Scenario;
using tmatch::begin_round;
using tmatch::make_scenario;
using tmatch::scenario_hole;
using tmatch::splitmix64;
using tmatch::rademacher;
using tmatch::paired_reward;

static int iters_per_move = 0;

static std::vector<MatchOutcome> run_batch(std::vector<MatchJob> const &jobs, int threads,
                                           int search_ms, int max_rounds,
                                           std::atomic<bool> &view, std::mutex &view_mutex,
                                           std::atomic<uint32_t> &view_index)
{
    return tmatch::run_batch(jobs, threads, iters_per_move, search_ms, max_rounds,
                         view, view_mutex, view_index);
}

static bool load_params(double *out, std::string const &tag = "")
{
    std::string const path = param::filename(tag);
    if (!tmatch::read_theta_strict(path, out))
    {
        return false;
    }
    std::println("[TUNER] Loaded best parameters from {}", path);
    return true;
}

static void default_params(double *out)
{
    ai_zzz::TOJ::production_default_theta(out);
}

static void compute_effective_scale(double const *reference_theta, double *out)
{
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        double const base_scale = param_scale[i];
        double const relative_scale = ES_RELATIVE_PARAM_SCALE * std::fabs(reference_theta[i]);
        out[i] = std::max(base_scale, relative_scale);
    }
}

static double restart_sigma_for_level(int level)
{
    int const exponent = std::max(0, level - 1);
    return std::min(RESTART_SIGMA_MAX,
                    RESTART_SIGMA0 * std::pow(RESTART_SIGMA_GROWTH, exponent));
}

static double restart_kick_l2_for_level(int level)
{
    int const exponent = std::max(0, level - 1);
    return std::min(RESTART_KICK_L2_MAX,
                    RESTART_KICK_L2_0 * std::pow(RESTART_KICK_GROWTH, exponent));
}

static bool restart_state_needs_rebase(double saved_sigma, int level)
{
    return level > 0
        && saved_sigma > restart_sigma_for_level(level) * (1.0 + 1e-9);
}

static void apply_restart_kick(double const *incumbent, double const *scale,
                               unsigned seed, int iteration, int level,
                               double *theta, double *x)
{
    double direction[NUM_PARAMS];
    double norm = 0.0;
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        direction[i] = static_cast<double>(rademacher(
            seed ^ 0xD1B54A32D192ED03ULL, iteration, level, static_cast<int>(i)));
        norm += direction[i] * direction[i];
    }
    norm = std::sqrt(norm);
    double const kick_l2 = restart_kick_l2_for_level(level);
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        double const incumbent_x = incumbent[i] / scale[i];
        x[i] = incumbent_x + kick_l2 * direction[i] / norm;
        theta[i] = x[i] * scale[i];
    }
}


static void adam_update(double const *grad, int q, double es_sigma, int k, int schedule_iter,
                        double *es_m1, double *es_m2, double *dx, double &grad_norm)
{
    double g[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        g[i] = grad[i] / (2.0 * q * es_sigma);
        grad_norm += g[i] * g[i];
    }
    grad_norm = std::sqrt(grad_norm);
    (void)k;
    double t = std::min(1.0, static_cast<double>(schedule_iter) / ES_LR_HORIZON);
    double lr = ES_LR1 + 0.5 * (ES_LR0 - ES_LR1) * (1.0 + std::cos(std::numbers::pi * t));
    int const adam_step = std::max(1, schedule_iter + 1);
    double beta1t = std::pow(ES_BETA1, adam_step);
    double beta2t = std::pow(ES_BETA2, adam_step);
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
                           int search_ms, int max_rounds,
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
    auto out = run_batch(jobs, threads, search_ms, max_rounds, view, view_mutex, view_index);
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


static bool durable_write_doubles(std::string const &path, double const *data, size_t n)
{
    return durable::write_doubles(path, data, n);
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
                      CheckpointConfig &saved_cfg, int &stall_batches,
                      int &restarts, int &restart_origin)
{
    std::ifstream ifs(data_file, std::ios::binary);
    if (!ifs.good())
    {
        return CHECKPOINT_MISSING;
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
    ifs.read(reinterpret_cast<char *>(&stall_batches), sizeof(stall_batches));
    ifs.read(reinterpret_cast<char *>(&restarts), sizeof(restarts));
    ifs.read(reinterpret_cast<char *>(&restart_origin), sizeof(restart_origin));
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
        + sizeof(saved_cfg.next_length) + NUM_PARAMS * sizeof(double) * 3 + sizeof(es_sigma)
        + sizeof(stall_batches) + sizeof(restarts) + sizeof(restart_origin);
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
    std::memcpy(p, &stall_batches, sizeof(stall_batches)); p += sizeof(stall_batches);
    std::memcpy(p, &restarts, sizeof(restarts)); p += sizeof(restarts);
    std::memcpy(p, &restart_origin, sizeof(restart_origin)); p += sizeof(restart_origin);
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
                       CheckpointConfig const &cfg, int stall_batches,
                       int restarts, int restart_origin)
{
    size_t const payload_bytes = sizeof(k) + sizeof(cfg.max_rounds) + sizeof(cfg.eval_matches)
        + sizeof(cfg.search_ms) + sizeof(cfg.seed) + sizeof(cfg.iters_per_move)
        + sizeof(cfg.param_scale) + sizeof(cfg.combo_table)
        + sizeof(cfg.next_length) + NUM_PARAMS * sizeof(double) * 3 + sizeof(es_sigma)
        + sizeof(stall_batches) + sizeof(restarts) + sizeof(restart_origin);
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
    std::memcpy(p, &stall_batches, sizeof(stall_batches)); p += sizeof(stall_batches);
    std::memcpy(p, &restarts, sizeof(restarts)); p += sizeof(restarts);
    std::memcpy(p, &restart_origin, sizeof(restart_origin)); p += sizeof(restart_origin);
    uint64_t checksum = checkpoint_checksum(payload.data(), payload.size());

    std::string const tmp = data_file + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write(payload.data(), (std::streamsize)payload.size());
        ofs.write(reinterpret_cast<char const *>(&checksum), sizeof(checksum));
        ofs.flush();
        if (!ofs.good())
        {
            std::remove(tmp.c_str());
            return false;
        }
    }
    durable::fsync_path(tmp);
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
    durable::fsync_directory(data_file);
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
    auto out = run_batch(jobs, threads, search_ms_arm, max_rounds, view, view_mutex, view_index);

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
    auto out = run_batch(jobs, threads, search_ms, max_rounds, view, view_mutex, view_index);

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
    double effective_scale[NUM_PARAMS];
    compute_effective_scale(theta, effective_scale);
    double x[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i) x[i] = theta[i] / effective_scale[i];

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
                a.p1[i] = xp * effective_scale[i];
                a.p2[i] = xm * effective_scale[i];
                b.p1[i] = xm * effective_scale[i];
                b.p2[i] = xp * effective_scale[i];
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

        auto out = run_batch(jobs, threads, search_ms, max_rounds, view, view_mutex, view_index);
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


struct SelfCheck
{
    int failures = 0;
    void check(bool cond, char const *name)
    {
        if (cond)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            std::println("FAIL: {}", name);
            ++failures;
        }
    }
};

static void check_promotion_statistics(SelfCheck &sc)
{
    double const z_final = stat_util::normal_quantile_upper(CHALLENGE_ALPHA);
    sc.check(std::fabs(z_final - stat_util::Z_ONE_SIDED_95) < 1e-4,
             "final-look z equals the one-sided 95% quantile");
    sc.check(stat_util::look_z(1, CHALLENGE_TOTAL_LOOKS - 1, CHALLENGE_ALPHA) > z_final,
             "interim looks are stricter than the final look");
    sc.check(stat_util::look_z(CHALLENGE_TOTAL_LOOKS, CHALLENGE_TOTAL_LOOKS - 1, CHALLENGE_ALPHA) == z_final,
             "final look uses the nominal alpha");

    double const lb_perfect = stat_util::wilson_lower_bound(64, 64, z_final);
    double const lb_63 = stat_util::wilson_lower_bound(63, 64, z_final);
    double const lb_coin = stat_util::wilson_lower_bound(32, 64, z_final);
    sc.check(lb_63 < 0.95, "a 63-1 record really cannot clear a 95% bound (the old rule)");
    sc.check(lb_perfect > CHALLENGE_PROMOTE_LB && lb_63 > CHALLENGE_PROMOTE_LB,
             "dominant candidates clear the 50% promotion bar at the budget ceiling");
    sc.check(lb_coin < CHALLENGE_PROMOTE_LB, "an exactly-equal candidate is not promoted at the ceiling");

    double const lb_interim = stat_util::wilson_lower_bound(8, 8, stat_util::look_z(1, 7, CHALLENGE_ALPHA));
    sc.check(lb_interim < CHALLENGE_PROMOTE_LB, "8-0 at the first interim look does not promote");
    double const lb_interim2 = stat_util::wilson_lower_bound(15.5, 16, stat_util::look_z(2, 7, CHALLENGE_ALPHA));
    sc.check(lb_interim2 > CHALLENGE_PROMOTE_LB, "a 15.5-0.5 interim lead does promote");
    sc.check(stat_util::wilson_lower_bound(0, 0, z_final) == 0.0, "zero-game bound is degenerate-safe");

    double const impossible_final = stat_util::wilson_lower_bound(18 + 0, 64, z_final);
    double const still_possible = stat_util::wilson_lower_bound(4 + 56, 64, z_final);
    sc.check(impossible_final < CHALLENGE_PROMOTE_LB && still_possible > CHALLENGE_PROMOTE_LB,
             "challenge futility bound distinguishes impossible and recoverable records");
}

static void check_crn_event_scope(SelfCheck &sc)
{
    Scenario a = make_scenario(0xC0FFEE, 200, static_cast<size_t>(next_length));
    Scenario b = make_scenario(0xC0FFEE, 200, static_cast<size_t>(next_length));
    begin_round(a, b, 99);
    for (int i = 0; i < 7; ++i) { ++a.packet_index; }
    for (int i = 0; i < 3; ++i) { ++b.packet_index; }
    begin_round(a, b, 100);
    bool same = true;
    for (int k = 0; k < 5; ++k)
    {
        same = same && (scenario_hole(a) == scenario_hole(b));
        ++a.packet_index;
        ++b.packet_index;
    }
    sc.check(same, "garbage hole columns are event-synchronised across policies");
    sc.check(a.packet_index == 5 && b.packet_index == 5,
             "packet counter is scoped per round, not per game");

    Scenario c = make_scenario(0xC0FFEE, 200, static_cast<size_t>(next_length));
    begin_round(c, c, 101);
    bool differs = false;
    for (int k = 0; k < 5; ++k)
    {
        if (scenario_hole(c) != scenario_hole(a)) { differs = true; }
        ++c.packet_index;
        ++a.packet_index;
    }
    sc.check(differs, "rounds are distinguished inside the CRN hash");
}

static void check_parallel_bot_slots(SelfCheck &sc)
{
    std::atomic<int> live{ 0 }, peak{ 0 };
    auto busy = [&]()
    {
        int const now = ++live;
        int prev = peak.load();
        while (now > prev && !peak.compare_exchange_weak(prev, now))
        {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        --live;
    };

    std::counting_semaphore<> roomy(4);
    for (int i = 0; i < 6; ++i)
    {
        tmatch::run_pair_parallel(busy, busy, roomy);
    }
    sc.check(peak == 2, "a match runs its two bots at once while slots are free");

    std::counting_semaphore<> tight(2);
    std::vector<std::thread> racers;
    for (int i = 0; i < 3; ++i)
    {
        racers.emplace_back([&]
        {
            for (int r = 0; r < 4; ++r)
            {
                tmatch::run_pair_parallel(busy, busy, tight);
            }
        });
    }
    for (auto &th : racers)
    {
        th.join();
    }
    sc.check(peak <= 2, "concurrent matches never exceed the free-thread budget");

    std::counting_semaphore<> single(1);
    live = 0;
    peak = 0;
    tmatch::run_pair_parallel(busy, busy, single);
    sc.check(peak == 1, "with no spare slot the bots are played one after the other");

    std::counting_semaphore<> drained(3);
    live = 0;
    peak = 0;
    std::vector<std::thread> five;
    for (int i = 0; i < 5; ++i)
    {
        five.emplace_back([&]
        {
            tmatch::run_pair_parallel(busy, busy, drained);
        });
    }
    for (auto &th : five)
    {
        th.join();
    }
    sc.check(peak <= 3 && drained.try_acquire() && drained.try_acquire() && drained.try_acquire(),
             "permits are returned even when a worker cannot pair up");
}

static void check_search_scale_and_restart_policy(SelfCheck &sc)
{
    double reference[NUM_PARAMS] = {};
    double scale[NUM_PARAMS];
    reference[0] = 123.45678;
    compute_effective_scale(reference, scale);
    sc.check(scale[0] > param_scale[0] * 10.0,
             "a large-magnitude parameter receives a meaningful relative search scale");
    sc.check(std::fabs(scale[0] - ES_RELATIVE_PARAM_SCALE * std::fabs(reference[0])) < 1e-9,
             "relative search scale tracks the configured fraction of a large parameter");

    double incumbent[NUM_PARAMS] = {};
    double theta[NUM_PARAMS] = {};
    double x[NUM_PARAMS] = {};
    compute_effective_scale(incumbent, scale);
    apply_restart_kick(incumbent, scale, 12345, 2000, 1, theta, x);
    double kick_norm = 0.0;
    for (double v : x) { kick_norm += v * v; }
    kick_norm = std::sqrt(kick_norm);
    sc.check(std::fabs(kick_norm - restart_kick_l2_for_level(1)) < 1e-9,
             "stall restart applies the declared normalized kick");
    sc.check(restart_sigma_for_level(2) > restart_sigma_for_level(1)
                 && restart_sigma_for_level(MAX_RESTART_LEVEL)
                     <= RESTART_SIGMA_MAX,
             "repeated stalls escalate sigma up to a bounded maximum");
    sc.check(restart_state_needs_rebase(1.14, MAX_RESTART_LEVEL)
                 && !restart_state_needs_rebase(0.60, MAX_RESTART_LEVEL),
             "an oversized legacy restart state is detected without disturbing a valid one");

    int restart_level = MAX_RESTART_LEVEL;
    restart_level = 0;
    sc.check(restart_level == 0,
             "promotion resets the restart escalation level");

    int stall = 150;
    bool challenge_due = false;
    if (challenge_due) { stall += CHALLENGE_EVERY; }
    sc.check(stall == 150,
             "non-challenge training iterations do not advance the stall clock");
    challenge_due = true;
    if (challenge_due) { stall += CHALLENGE_EVERY; }
    sc.check(stall == STALL_WINDOW,
             "a failed challenge advances the stall clock by its interval");
}

static void check_warm_start_identity(SelfCheck &sc)
{
    double prod[NUM_PARAMS], weak[NUM_PARAMS];
    default_params(prod);
    ai_zzz::TOJ::struct_defaults_theta(weak);
    bool differs = false;
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        if (prod[i] != weak[i]) { differs = true; }
    }
    sc.check(differs, "the --default baseline is the production policy, not the struct initializers");
    sc.check(std::fabs(prod[0] - 10.507166148) < 1e-9 && std::fabs(prod[28] - 0.756272086) < 1e-9,
             "production baseline values match the shipped policy");

    std::string const tmp = "selfcheck_param_tmp.bin";
    {
        std::ofstream f(tmp, std::ios::binary);
        f.write(reinterpret_cast<char const *>(prod), (NUM_PARAMS - 1) * sizeof(double));
    }
    double probe[NUM_PARAMS];
    sc.check(!tmatch::read_theta_strict(tmp, probe), "truncated param file is rejected");
    {
        std::ofstream f(tmp, std::ios::binary);
        double bad[NUM_PARAMS];
        std::memcpy(bad, prod, sizeof(bad));
        bad[3] = std::numeric_limits<double>::quiet_NaN();
        f.write(reinterpret_cast<char const *>(bad), sizeof(bad));
    }
    sc.check(!tmatch::read_theta_strict(tmp, probe), "non-finite param file is rejected");
    {
        std::ofstream f(tmp, std::ios::binary);
        f.write(reinterpret_cast<char const *>(prod), sizeof(prod));
    }
    bool ok = tmatch::read_theta_strict(tmp, probe);
    for (size_t i = 0; ok && i < NUM_PARAMS; ++i) { ok = probe[i] == prod[i]; }
    sc.check(ok, "a complete finite param file round-trips exactly");
    std::remove(tmp.c_str());
}

static void check_checkpoint_formats(SelfCheck &sc)
{
    std::string const path = "selfcheck_tuner_data_tmp.bin";
    double theta[NUM_PARAMS], loaded_theta[NUM_PARAMS];
    default_params(theta);
    double es_m1[NUM_PARAMS] = { 0 }, es_m2[NUM_PARAMS] = { 0 };
    for (size_t i = 0; i < NUM_PARAMS; ++i) { es_m1[i] = 0.001 * i; es_m2[i] = 0.002 * i; }
    CheckpointConfig cfg{};
    cfg.max_rounds = 3600;
    cfg.eval_matches = 14;
    cfg.search_ms = 20;
    cfg.seed = 4242;
    cfg.iters_per_move = 80;
    cfg.next_length = next_length;
    std::memcpy(cfg.param_scale, param_scale, sizeof(param_scale));
    std::memcpy(cfg.combo_table, combo_table, sizeof(combo_table));

    sc.check(save_state(path, 1000, theta, 0.15, es_m1, es_m2, cfg, 7, 2, 500), "checkpoint saves");
    int resume_k = 0;
    double sigma = 0.0;
    CheckpointConfig loaded_cfg{};
    int stall = 0, restarts = 0, origin = 0;
    int r = load_state(path, resume_k, loaded_theta, sigma, es_m1, es_m2, loaded_cfg,
                       stall, restarts, origin);
    sc.check(r == CHECKPOINT_OK && resume_k == 1000, "checkpoint resumes at the saved iteration");
    sc.check(stall == 7 && restarts == 2 && origin == 500,
             "stall and restart state survives a save/load round-trip");
    bool theta_ok = true;
    for (size_t i = 0; i < NUM_PARAMS; ++i) { theta_ok = theta_ok && loaded_theta[i] == theta[i]; }
    sc.check(theta_ok && std::fabs(sigma - 0.15) < 1e-12 && checkpoint_config_equal(loaded_cfg, cfg),
             "checkpoint payload is faithful");

    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(6, std::ios::beg);
        char ch = 0;
        f.read(&ch, 1);
        f.seekp(6, std::ios::beg);
        char flipped = static_cast<char>(ch ^ 0x20);
        f.write(&flipped, 1);
    }
    r = load_state(path, resume_k, loaded_theta, sigma, es_m1, es_m2, loaded_cfg,
                   stall, restarts, origin);
    sc.check(r == CHECKPOINT_CORRUPT, "a corrupted checkpoint is rejected rather than trusted");

    int pr = 0, pk = 0, ps = 0, po = 0;
    sc.check(save_state(path, 10, theta, 0.2, es_m1, es_m2, cfg, 0, 0, -1)
             && load_state(path, pr, loaded_theta, sigma, es_m1, es_m2, loaded_cfg, pk, ps, po)
                 == CHECKPOINT_OK && pr == 10 && pk == 0 && ps == 0 && po == -1,
             "a run with no restart history round-trips");

    sc.check(save_state(path, 10, theta, 0.2, es_m1, es_m2, cfg, 5, 9, 100)
             && load_state(path, pr, loaded_theta, sigma, es_m1, es_m2, loaded_cfg, pk, ps, po)
                 == CHECKPOINT_OK && pr == 10 && pk == 5 && ps == 9 && po == 100,
             "a mid-run stall clock and restart count resume intact");

    std::remove(path.c_str());
    std::remove((path + ".bak").c_str());
}

static int run_selfcheck()
{
    SelfCheck sc;
    check_promotion_statistics(sc);
    check_crn_event_scope(sc);
    check_parallel_bot_slots(sc);
    check_search_scale_and_restart_policy(sc);
    check_warm_start_identity(sc);
    check_checkpoint_formats(sc);
    std::println("");
    if (sc.failures == 0)
    {
        std::println("ALL SELF-CHECKS PASSED");
        return 0;
    }
    std::println("{} SELF-CHECK(S) FAILED", sc.failures);
    return 1;
}

static void print_usage()
{
    std::println("tuner - paired mirrored ES tuner for the TOJ policy");
    std::println("");
    std::println("Usage:");
    std::println("  tuner [<iters> <games> <search_ms> <seed> <threads> <rounds> <iters_per_move>] [--default|--fresh-zero]");
    std::println("  tuner probe [<batches> <games> <search_ms> <seed> <threads> <rounds> <step> <iters_per_move>] [--default]");
    std::println("  tuner bench [<pairs> <iters_arm> <ms_arm> <seed> <threads> <rounds>] [--default]");
    std::println("  tuner vs <path_a> <path_b> [<pairs> <search_ms> <iters_per_move> <seed> <threads> <rounds>]");
    std::println("  tuner selfcheck");
    std::println("  tuner --help | -h | help");
    std::println("");
    std::println("Modes:");
    std::println("  (default)   Run the paired mirrored ES search from the current policy.");
    std::println("              Iterates <iters> batches of <games> seat-swapped matches,");
    std::println("              promoting to best_param.bin when a candidate clears the");
    std::println("              incumbent challenge. Checkpoints to tuner_data.bin and resumes.");
    std::println("  probe       Fixed-theta gradient probe: measures the parameter gradient at");
    std::println("              the current policy over <batches> batches of mirrored pairs.");
    std::println("  bench       Budget calibration: <pairs> seat-swapped pairs of iters-arm vs");
    std::println("              ms-arm, reporting win rate, CI, APP, APL and deaths.");
    std::println("  vs          Held-out comparison of two param files <path_a> and <path_b>,");
    std::println("              reporting win-equiv, CI, APP, APL, deaths and rounds.");
    std::println("  selfcheck   Fast deterministic assertions (promotion stats, CRN event");
    std::println("              scoping, checkpoint formats, warm-start identity). No games.");
    std::println("");
    std::println("Common options:");
    std::println("  --default     Start from the production baseline instead of best_param.bin.");
    std::println("  --fresh-zero  Start from zero weights (default mode only).");
    std::println("");
    std::println("Arguments (positional, defaults in parentheses):");
    std::println("  <iters>            total iterations (10000)");
    std::println("  <games>            matches per batch, even (14)");
    std::println("  <search_ms>        per-move search budget in ms (20)");
    std::println("  <seed>             RNG seed, 0 = time-based (default 0, others 555)");
    std::println("  <threads>          worker threads, 0 = auto (1)");
    std::println("  <rounds>           max rounds per match (3600)");
    std::println("  <iters_per_move>   search budget in iterations, 0 = use ms (0)");
    std::println("  <pairs>            seat-swapped pairs (bench 64, vs 32)");
    std::println("  <iters_arm>        bench: iterations budget for the iters arm (20)");
    std::println("  <ms_arm>           bench: ms budget for the ms arm (20)");
    std::println("  <batches>          probe: number of gradient batches (8)");
    std::println("  <step>             probe: mirrored step size in normalized coords (0.30)");
    std::println("");
    std::println("Files:");
    std::println("  best_param.bin     validated incumbent / warm-start policy (29 doubles)");
    std::println("  current_param.bin  latest candidate, written every iteration");
    std::println("  tuner_data.bin     resumable ES checkpoint (with .bak)");
    std::println("");
    std::println("Examples:");
    std::println("  tuner 5000 14 20 0 8 3600 80");
    std::println("  tuner --default 1000 14 20 42 4");
    std::println("  tuner probe 16 14 20 7 8");
    std::println("  tuner bench 64 20 20 555 8");
    std::println("  tuner vs best_param.bin candidate.bin 32 20 0 555 8");
    std::println("  tuner selfcheck");
}

static bool wants_help(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0
            || std::strcmp(argv[i], "help") == 0)
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

    if (wants_help(argc, argv))
    {
        print_usage();
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "selfcheck") == 0)
    {
        return run_selfcheck();
    }
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

    double theta[NUM_PARAMS];
    double es_sigma = ES_SIGMA0;
    double es_m1[NUM_PARAMS] = { 0 };
    double es_m2[NUM_PARAMS] = { 0 };
    int resume_k = 0;
    CheckpointConfig saved_cfg;
    int stall_batches = 0;
    int restarts = 0;
    int restart_origin = -1;
    int load_result = load_state(data_file, resume_k, theta, es_sigma, es_m1, es_m2,
                                 saved_cfg, stall_batches, restarts, restart_origin);
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
        std::println("[TUNER] Resumed from iteration {} (overrides best_param); stall={} restart_level={}/{} restart_origin={}",
                     resume_k, stall_batches, restarts, MAX_RESTART_LEVEL, restart_origin);
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
            std::println("[TUNER] Fresh start from production defaults (--default)");
        }
        else if (load_params(theta))
        {
            std::println("[TUNER] Fresh warm start from {}", param::filename(""));
        }
        else
        {
            default_params(theta);
            std::println(stderr, "[TUNER] warning: {} missing or invalid; starting from the production default policy (pass --fresh-zero to start from zero).", param::filename(""));
        }
    }
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        theta[i] = std::isfinite(theta[i]) ? theta[i] : 0.0;
    }

    double best_theta[NUM_PARAMS];
    if (tmatch::read_theta_strict(param::filename(""), best_theta))
    {
        std::println("[TUNER] Incumbent (validated best) loaded from {}", param::filename(""));
    }
    else
    {
        std::memcpy(best_theta, theta, sizeof(best_theta));
        std::println("[TUNER] No incumbent; best_param.bin will be written after the first successful challenge");
    }

    double effective_scale[NUM_PARAMS];
    compute_effective_scale(best_theta, effective_scale);
    double x[NUM_PARAMS];
    for (size_t i = 0; i < NUM_PARAMS; ++i)
    {
        x[i] = theta[i] / effective_scale[i];
    }

    if (load_result == CHECKPOINT_OK && restart_state_needs_rebase(es_sigma, restarts))
    {
        restart_origin = resume_k;
        std::memcpy(theta, best_theta, sizeof(best_theta));
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            x[i] = theta[i] / effective_scale[i];
            es_m1[i] = 0.0;
            es_m2[i] = 0.0;
        }
        es_sigma = restart_sigma_for_level(restarts);
        stall_batches = 0;
        std::println("[TUNER] rebased oversized legacy restart state to incumbent: level={}, sigma={:.3f}, Adam reset",
                     restarts, es_sigma);
    }

    std::println("[TUNER] paired mirrored ES: {} iters, {} games/batch ({} directions), {} rounds/match, {} search, seed {}, threads={}, sigma_tau={:.0f}, lr_horizon={:.0f}, relative_scale={:.1f}%",
                num_iters, 2 * q, q, max_rounds,
                iters_per_move > 0 ? (std::to_string(iters_per_move) + " iters") : (std::to_string(search_ms) + "ms"), seed, threads,
                ES_SIGMA_TAU, ES_LR_HORIZON, 100.0 * ES_RELATIVE_PARAM_SCALE);
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
        int const schedule_iter = restart_origin >= 0 ? std::max(0, k - restart_origin) : k;
        double phase_sigma0 = ES_SIGMA0;
        if (restart_origin >= 0)
        {
            phase_sigma0 = restarts > 0
                ? restart_sigma_for_level(restarts)
                : ES_PROMOTION_SIGMA;
        }
        double const phase_sigma_tau = restart_origin >= 0 && restarts > 0
            ? RESTART_SIGMA_TAU : ES_SIGMA_TAU;
        es_sigma = std::max(ES_SIGMA_FLOOR,
                            phase_sigma0 * std::exp(-static_cast<double>(schedule_iter) / phase_sigma_tau));
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
                a.p1[i] = xp * effective_scale[i];
                a.p2[i] = xm * effective_scale[i];
                b.p1[i] = xm * effective_scale[i];
                b.p2[i] = xp * effective_scale[i];
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

        auto out = run_batch(jobs, threads, search_ms, max_rounds, view, view_mutex, view_index);

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
            adam_update(grad, q, es_sigma, k, schedule_iter, es_m1, es_m2, dx, grad_norm);
        }

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
            theta[i] = x[i] * effective_scale[i];
        }

        bool promoted = false;
        if ((k + 1) % CHALLENGE_EVERY == 0)
        {
            double wins_equiv = 0.0;
            int played = 0;
            int look = 0;
            for (int pairs = 0; pairs < CHALLENGE_MAX_PAIRS; pairs += CHALLENGE_PAIRS)
            {
                ++look;
                double stage = run_challenge(theta, best_theta, threads, search_ms, max_rounds,
                                             view, view_mutex, view_index,
                                             CHALLENGE_SEED_BASE
                                                 + static_cast<uint64_t>(k + 1) * 0x9E3779B97F4A7C15ULL
                                                 + static_cast<uint64_t>(pairs),
                                             CHALLENGE_PAIRS);
                wins_equiv += 2.0 * CHALLENGE_PAIRS * (1.0 + stage) / 2.0;
                played += 2 * CHALLENGE_PAIRS;
                double const z = stat_util::look_z(look, CHALLENGE_TOTAL_LOOKS - 1, CHALLENGE_ALPHA);
                double const phat = wins_equiv / static_cast<double>(played);
                double const p_lo = stat_util::wilson_lower_bound(wins_equiv, played, z);
                std::println("[ES]   challenge @ iter {}: stage={:+.3f} (pairs {}..{}), games={}, win-equiv={:.1f}, win={:.1f}%, LB={:.1f}% (z={:.2f}, need > {:.0f}%)",
                             k, stage, pairs + 1, pairs + CHALLENGE_PAIRS, played,
                             wins_equiv, 100.0 * phat, 100.0 * p_lo, z, 100.0 * CHALLENGE_PROMOTE_LB);
                if (p_lo > CHALLENGE_PROMOTE_LB)
                {
                    std::memcpy(best_theta, theta, sizeof(best_theta));
                    durable_write_doubles("best_param.bin", best_theta, NUM_PARAMS);
                    std::println("[ES]   promoted candidate to best_param.bin ({} games, win={:.1f}%, LB={:.1f}%)",
                                 played, 100.0 * phat, 100.0 * p_lo);
                    promoted = true;
                    break;
                }
                if (look < CHALLENGE_TOTAL_LOOKS)
                {
                    int const remaining_games = 2 * (CHALLENGE_MAX_PAIRS - pairs - CHALLENGE_PAIRS);
                    double const max_final_lb = stat_util::wilson_lower_bound(
                        wins_equiv + remaining_games, played + remaining_games,
                        stat_util::normal_quantile_upper(CHALLENGE_ALPHA));
                    if (max_final_lb <= CHALLENGE_PROMOTE_LB)
                    {
                        std::println("[ES]   challenge stopped for futility after {} games (even winning all {} remaining games cannot promote)",
                                     played, remaining_games);
                        break;
                    }
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
            restarts = 0;
            restart_origin = k + 1;
            compute_effective_scale(best_theta, effective_scale);
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                x[i] = theta[i] / effective_scale[i];
                es_m1[i] = 0.0;
                es_m2[i] = 0.0;
            }
            es_sigma = ES_PROMOTION_SIGMA;
            std::println("[ES]   new campaign: reset restart level, Adam, and schedule; sigma={:.3f}",
                         ES_PROMOTION_SIGMA);
        }
        else if ((k + 1) % CHALLENGE_EVERY == 0)
        {
            stall_batches += CHALLENGE_EVERY;
        }
        if (!promoted && (k + 1) % CHALLENGE_EVERY == 0
            && stall_batches >= STALL_WINDOW)
        {
            restart_origin = k + 1;
            restarts = std::min(MAX_RESTART_LEVEL, restarts + 1);
            compute_effective_scale(best_theta, effective_scale);
            for (size_t i = 0; i < NUM_PARAMS; ++i)
            {
                es_m1[i] = 0.0;
                es_m2[i] = 0.0;
            }
            apply_restart_kick(best_theta, effective_scale, seed, k + 1, restarts,
                               theta, x);
            es_sigma = restart_sigma_for_level(restarts);
            stall_batches = 0;
            std::println("[ES]   STALL: {} iterations without promotion; restart level {}/{} from incumbent, kick_l2={:.3f}, sigma={:.3f}, Adam reset, re-annealing from iter {}",
                         STALL_WINDOW, restarts, MAX_RESTART_LEVEL,
                         restart_kick_l2_for_level(restarts), es_sigma, restart_origin);
        }

        if (!save_state(data_file, k + 1, theta, es_sigma, es_m1, es_m2, cfg,
                        stall_batches, restarts, restart_origin))
        {
            std::println(stderr, "[TUNER] warning: failed to write checkpoint {}", data_file);
        }
        durable_write_doubles("current_param.bin", theta, NUM_PARAMS);

        double dist_inc_norm = 0;
        double dist_inc_phys = 0;
        for (size_t i = 0; i < NUM_PARAMS; ++i)
        {
            double d = x[i] - best_theta[i] / effective_scale[i];
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
    if (!save_state(data_file, num_iters, theta, es_sigma, es_m1, es_m2, cfg,
                    stall_batches, restarts, restart_origin))
    {
        std::println(stderr, "[TUNER] warning: failed to write checkpoint {}", data_file);
    }

    std::println("\n[TUNER] Done. {} iterations completed", num_iters);
    std::println("[TUNER] Current params saved to {}", "current_param.bin");
    if (tmatch::read_theta_strict(param::filename(""), best_theta))
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
