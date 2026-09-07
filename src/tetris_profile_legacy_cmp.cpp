#include "legacy_cmp_normalize.h"
#include "legacy_cmp_observer.h"
#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace
{
    struct CmpSearch : search_tspin::Search
    {
        static std::uint64_t calls;
        static std::uint64_t raw_landings;
        static std::uint64_t unique_candidates;
        static std::uint64_t unmatched_candidates;
        static std::int64_t search_ns;
        static std::int64_t norm_ns;
        static bool enabled;
        static bool timers_enabled;

        legacy_cmp::ProbeDedup dedup;

        static void reset()
        {
            calls = 0;
            raw_landings = 0;
            unique_candidates = 0;
            unmatched_candidates = 0;
            search_ns = 0;
            norm_ns = 0;
        }

        static std::int64_t now()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        std::vector<TetrisNodeWithTSpinType> const *search(
            m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node,
            size_t depth)
        {
            bool const count = enabled;
            bool const time = enabled && timers_enabled;
            std::int64_t start = time ? now() : 0;
            auto const *result = search_tspin::Search::search(map, node, depth);
            if (time)
            {
                search_ns += now() - start;
            }
            if (!count)
            {
                return result;
            }
            ++calls;
            raw_landings += result->size();
            if (!timers_enabled)
            {
                return result;
            }
            std::int64_t norm_start = time ? now() : 0;
            dedup.begin_call();
            for (auto const &land : *result)
            {
                int spin_class = land.type == search_tspin::Search::TSpinType::TSpin
                    ? 1
                    : (land.type == search_tspin::Search::TSpinType::TSpinMini ? 2 : 0);
                dedup.add(legacy_cmp::normalize_land_point(
                    land->status.t, land->status.x, land->status.y,
                    land->status.r, spin_class, land.is_last_rotate,
                    land->status.status));
            }
            unique_candidates += dedup.distinct();
            unmatched_candidates += dedup.unmatched();
            if (time)
            {
                norm_ns += now() - norm_start;
            }
            return result;
        }
    };
    std::uint64_t CmpSearch::calls = 0;
    std::uint64_t CmpSearch::raw_landings = 0;
    std::uint64_t CmpSearch::unique_candidates = 0;
    std::uint64_t CmpSearch::unmatched_candidates = 0;
    std::int64_t CmpSearch::search_ns = 0;
    std::int64_t CmpSearch::norm_ns = 0;
    bool CmpSearch::enabled = true;
    bool CmpSearch::timers_enabled = true;

    struct CmpTOJ : ai_zzz::TOJ
    {
        static std::uint64_t transitions;
        static std::int64_t transition_ns;
        static bool enabled;
        static bool timers_enabled;

        static void reset()
        {
            transitions = 0;
            transition_ns = 0;
        }

        static std::int64_t now()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }

        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear,
            m_tetris::TetrisMap const &map, size_t depth, Status const &status,
            m_tetris::TetrisContext::Env const &env) const
        {
            bool const count = enabled;
            bool const time = enabled && timers_enabled;
            std::int64_t start = time ? now() : 0;
            Status out = ai_zzz::TOJ::get(
                node, eval_result, clear, map, depth, status, env);
            if (count)
            {
                ++transitions;
            }
            if (time)
            {
                transition_ns += now() - start;
            }
            return out;
        }
    };
    std::uint64_t CmpTOJ::transitions = 0;
    std::int64_t CmpTOJ::transition_ns = 0;
    bool CmpTOJ::enabled = true;
    bool CmpTOJ::timers_enabled = true;

    using CmpEngine =
        m_tetris::TetrisEngine<rule_toj::TetrisRule, CmpTOJ, CmpSearch>;

    ai_zzz::TOJ::Param const default_param = {
        10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
        0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
        8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
        -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
        -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
    };

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    int const combo_table_max = 10;

    constexpr std::size_t max_supported_maxdepth = 255;
    constexpr double max_budget_ms = 1e15;

    bool parse_uint_strict(std::string const &text, std::size_t &out)
    {
        if (text.empty())
        {
            return false;
        }
        for (char c : text)
        {
            if (c < '0' || c > '9')
            {
                return false;
            }
        }
        errno = 0;
        unsigned long long value = std::strtoull(text.c_str(), nullptr, 10);
        if (errno == ERANGE)
        {
            return false;
        }
        out = static_cast<std::size_t>(value);
        return static_cast<unsigned long long>(out) == value;
    }

    bool parse_double_strict(std::string const &text, double &out)
    {
        if (text.empty())
        {
            return false;
        }
        char *end = nullptr;
        errno = 0;
        double value = std::strtod(text.c_str(), &end);
        if (errno == ERANGE || end != text.c_str() + text.size())
        {
            return false;
        }
        if (!std::isfinite(value))
        {
            return false;
        }
        out = value;
        return true;
    }

    double pct(std::vector<double> const &v, double p)
    {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        size_t idx = std::min(s.size() - 1, static_cast<size_t>(p * s.size()));
        return s[idx];
    }

    struct Options
    {
        size_t moves = 200;
        double level = 10;
        double ms = 0;
        uint32_t seed = 1;
        size_t maxdepth = 6;
        bool hold = true;
        std::string param_file;
        bool quiet = false;
        size_t iters = 0;
        size_t warmup_moves = 0;
        bool telemetry = true;
        bool timers = true;
    };

    void print_help()
    {
        std::println("usage: tetris_profile_legacy_cmp [options]");
        std::println("  Supplemental instrumented legacy comparison build.");
        std::println("  Same scenario, search, and scoring behavior as tetris_profile;");
        std::println("  emits one PROFILE_CMP record. Diagnostic evidence only,");
        std::println("  never a replacement historical baseline.");
        std::println("  --moves N --warmup-moves N --level L --ms T --iters N");
        std::println("  --seed S --maxdepth D (0-255) --no-hold --param-file F");
        std::println("  --telemetry on|off --quiet --help");
        std::println("  --timers on|off (default on) keeps counters while disabling");
        std::println("  component timers and normalization; timed rows use counters only");
    }

    Options parse_args(int argc, char **argv)
    {
        Options opt;
        std::string telemetry_text = "on";
        bool telemetry_seen = false;
        std::string timers_text = "on";
        bool timers_seen = false;
        auto fail = [&](std::string const &message) -> Options
        {
            std::println(stderr, "tetris_profile_legacy_cmp: invalid option: {}", message);
            std::exit(1);
        };
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](std::string const &name) -> std::string
            {
                if (i + 1 >= argc)
                {
                    std::println(stderr, "missing value for {}", name);
                    std::exit(1);
                }
                return argv[++i];
            };
            std::size_t uint_value = 0;
            double double_value = 0;
            if (a == "--moves")
            {
                if (!parse_uint_strict(next(a), uint_value)) fail(a);
                opt.moves = uint_value;
            }
            else if (a == "--level")
            {
                if (!parse_double_strict(next(a), double_value)) fail(a);
                opt.level = double_value;
            }
            else if (a == "--ms")
            {
                if (!parse_double_strict(next(a), double_value) || double_value < 0
                    || double_value > max_budget_ms)
                {
                    fail(a);
                }
                opt.ms = double_value;
            }
            else if (a == "--seed")
            {
                if (!parse_uint_strict(next(a), uint_value)
                    || uint_value > std::numeric_limits<std::uint32_t>::max())
                {
                    fail(a);
                }
                opt.seed = static_cast<uint32_t>(uint_value);
            }
            else if (a == "--maxdepth")
            {
                if (!parse_uint_strict(next(a), uint_value)) fail(a);
                opt.maxdepth = uint_value;
            }
            else if (a == "--no-hold") opt.hold = false;
            else if (a == "--param-file") opt.param_file = next(a);
            else if (a == "--iters")
            {
                if (!parse_uint_strict(next(a), uint_value)) fail(a);
                opt.iters = uint_value;
            }
            else if (a == "--warmup-moves")
            {
                if (!parse_uint_strict(next(a), uint_value)) fail(a);
                opt.warmup_moves = uint_value;
            }
            else if (a == "--telemetry")
            {
                telemetry_text = next(a);
                telemetry_seen = true;
            }
            else if (a == "--timers")
            {
                timers_text = next(a);
                timers_seen = true;
            }
            else if (a == "--quiet") opt.quiet = true;
            else if (a == "--help")
            {
                print_help();
                std::exit(0);
            }
            else
            {
                fail(a);
            }
        }
        if (opt.maxdepth > max_supported_maxdepth)
        {
            std::println(stderr, "maxdepth out of supported range");
            std::exit(1);
        }
        if (telemetry_seen)
        {
            if (telemetry_text == "on")
            {
                opt.telemetry = true;
            }
            else if (telemetry_text == "off")
            {
                opt.telemetry = false;
            }
            else
            {
                std::println(stderr, "telemetry must be on or off");
                std::exit(1);
            }
        }
        if (timers_seen)
        {
            if (timers_text == "on")
            {
                opt.timers = true;
            }
            else if (timers_text == "off")
            {
                opt.timers = false;
            }
            else
            {
                std::println(stderr, "timers must be on or off");
                std::exit(1);
            }
        }
        if (opt.moves > std::numeric_limits<std::size_t>::max() - opt.warmup_moves)
        {
            std::println(stderr, "warmup-moves plus moves overflows");
            std::exit(1);
        }
        return opt;
    }

    bool read_param_file(std::string const &path, ai_zzz::TOJ::Param &param)
    {
        double values[29];
        FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr)
        {
            return false;
        }
        size_t const count = std::fread(values, sizeof(double), 29, file);
        std::fclose(file);
        if (count != 29)
        {
            return false;
        }
        param = {
            values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15],
            values[16], values[17], values[18], values[19], values[20], values[21], values[22], values[23],
            values[24], values[25], values[26], values[27], values[28],
        };
        return true;
    }
}

int main(int argc, char **argv)
{
    using namespace std::chrono;
    Options opt = parse_args(argc, argv);
    double budget_value = opt.ms > 0 ? opt.ms : std::pow(100.0, opt.level / 8.0);
    if (!std::isfinite(budget_value) || budget_value < 0
        || budget_value > max_budget_ms)
    {
        std::println(stderr, "tetris_profile_legacy_cmp: time budget is not representable");
        return 1;
    }
    time_t budget_ms = static_cast<time_t>(budget_value);

    CmpEngine engine;
    if (!engine.prepare(10, 40))
    {
        std::println(stderr, "engine.prepare(10, 40) failed");
        return 1;
    }
    engine.memory_limit(256ull << 20);

    engine.search_config()->allow_rotate_move = false;
    engine.search_config()->allow_180 = true;
    engine.search_config()->allow_d = true;
    engine.search_config()->is_20g = false;
    engine.search_config()->last_rotate = false;

    engine.ai_config()->table = combo_table;
    engine.ai_config()->table_max = combo_table_max;
    engine.ai_config()->param = default_param;
    if (!opt.param_file.empty() && !read_param_file(opt.param_file, engine.ai_config()->param))
    {
        std::println(stderr, "failed to read 29-double parameter file: {}", opt.param_file);
        return 1;
    }

    CmpTOJ::enabled = opt.telemetry;
    CmpSearch::enabled = opt.telemetry;
    CmpTOJ::timers_enabled = opt.timers;
    CmpSearch::timers_enabled = opt.timers;
    legacy_cmp::Observer observer;
    observer.timers_enabled = opt.timers;

    m_tetris::TetrisMap map(10, 40);
    std::mt19937 rng(opt.seed);
    std::vector<char> next;
    char hold = ' ';
    int combo = 0;
    int b2b = 0;
    size_t total_clear = 0;
    size_t total_attack = 0;
    size_t games = 0;
    size_t moves_done = 0;
    size_t dead_moves = 0;

    std::vector<double> move_ms;
    std::uint64_t acc_eval_requests = 0;
    std::uint64_t acc_eval_hits = 0;
    std::uint64_t acc_eval_calls = 0;
    std::uint64_t acc_transitions = 0;
    std::uint64_t acc_searches = 0;
    std::uint64_t acc_widening = 0;
    std::uint64_t acc_parents = 0;
    std::uint64_t acc_raw = 0;
    std::uint64_t acc_unique = 0;
    std::uint64_t acc_unmatched = 0;
    std::uint64_t acc_fresh = 0;
    std::uint64_t acc_recycled = 0;
    std::uint64_t acc_roots = 0;
    std::uint64_t acc_reused = 0;
    std::uint64_t acc_path_states = 0;
    std::int64_t acc_eval_hit_ns = 0;
    std::int64_t acc_eval_miss_ns = 0;
    std::int64_t acc_parent_ns = 0;
    std::int64_t acc_alloc_ns = 0;
    std::int64_t acc_search_ns = 0;
    std::int64_t acc_norm_ns = 0;
    std::int64_t acc_transition_ns = 0;
    double acc_path_ms = 0;
    size_t node_sum = 0;

    size_t const total_moves = opt.warmup_moves + opt.moves;
    steady_clock::time_point t_total0 = steady_clock::now();

    while (moves_done < total_moves)
    {
        bool const warming = moves_done < opt.warmup_moves;
        if (!next.empty()) next.erase(next.begin());
        while (next.size() <= opt.maxdepth)
        {
            for (size_t i = 0; i < engine.context()->type_max(); ++i)
            {
                next.push_back(engine.context()->convert(i));
            }
            std::shuffle(next.end() - engine.context()->type_max(), next.end(), rng);
        }

        engine.ai_config()->safe = engine.ai()->get_safe(map, next.front());
        engine.status()->death = 0;
        engine.status()->combo = combo;
        engine.status()->under_attack = 0;
        engine.status()->map_rise = 0;
        engine.status()->b2b = !!b2b;
        engine.status()->acc_value = 0;
        engine.status()->like = 0;
        engine.status()->value = 0;
        ai_zzz::TOJ::Status::init_t_value(map, engine.status()->t2_value, engine.status()->t3_value);

        char current = next.front();
        size_t mem_before = engine.memory_usage();
        CmpTOJ::reset();
        CmpSearch::reset();
        observer.reset();
        legacy_cmp::AttachGuard guard(opt.telemetry ? &observer : nullptr);

        steady_clock::time_point t0 = steady_clock::now();
        auto result = opt.iters > 0
            ? engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1, opt.maxdepth, m_tetris::SearchBudget::by_iterations(opt.iters))
            : engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1, opt.maxdepth, budget_ms);
        steady_clock::time_point t1 = steady_clock::now();
        double elapsed_ms = duration<double, std::milli>(t1 - t0).count();

        bool dead = false;
        size_t clear = 0;
        double path_ms = 0;
        if (result.target == nullptr || result.target->row >= 20)
        {
            dead = true;
        }
        else
        {
            m_tetris::TetrisNode const *spawn_node = engine.context()->generate(current);
            steady_clock::time_point tp0 = steady_clock::now();
            std::vector<char> ai_path;
            if (result.change_hold)
            {
                ai_path = engine.make_path(
                    engine.context()->generate(result.target->status.t),
                    result.target, map);
            }
            else
            {
                ai_path = engine.make_path(spawn_node, result.target, map);
            }
            steady_clock::time_point tp1 = steady_clock::now();
            path_ms = duration<double, std::milli>(tp1 - tp0).count();
            if (result.change_hold)
            {
                if (hold == ' ')
                {
                    next.erase(next.begin());
                }
                hold = current;
            }
            clear = result.target->attach(engine.context().get(), map);
            total_clear += clear;

            int attack = 0;
            auto get_combo_attack = [&](int c)
            {
                return combo_table[std::min(combo_table_max - 1, c)];
            };
            switch (clear)
            {
            case 0:
                combo = 0;
                break;
            case 1:
                if (result.target.type == ai_zzz::TOJ::TSpinType::TSpinMini) { attack += 1 + b2b; b2b = 1; }
                else if (result.target.type == ai_zzz::TOJ::TSpinType::TSpin) { attack += 2 + b2b; b2b = 1; }
                else { b2b = 0; }
                attack += get_combo_attack(++combo);
                break;
            case 2:
                if (result.target.type != ai_zzz::TOJ::TSpinType::None) { attack += 4 + b2b; b2b = 1; }
                else { attack += 1; b2b = 0; }
                attack += get_combo_attack(++combo);
                break;
            case 3:
                if (result.target.type != ai_zzz::TOJ::TSpinType::None) { attack += 6 + b2b * 2; b2b = 1; }
                else { attack += 2; b2b = 0; }
                attack += get_combo_attack(++combo);
                break;
            case 4:
                attack += get_combo_attack(++combo) + 4 + b2b;
                b2b = 1;
                break;
            }
            if (map.count == 0) attack += 6;
            total_attack += attack;
            (void)ai_path;
        }

        size_t nodes_alloc = engine.memory_usage() - mem_before;
        if (!warming)
        {
            move_ms.push_back(elapsed_ms);
            acc_eval_requests += observer.counts.eval_requests;
            acc_eval_hits += observer.counts.eval_hits;
            acc_eval_calls += observer.counts.eval_calls;
            acc_transitions += CmpTOJ::transitions;
            acc_searches += CmpSearch::calls;
            acc_widening += observer.counts.widening_iters;
            acc_parents += observer.counts.parent_expansions;
            acc_raw += CmpSearch::raw_landings;
            acc_unique += CmpSearch::unique_candidates;
            acc_unmatched += CmpSearch::unmatched_candidates;
            acc_fresh += observer.counts.fresh_nodes;
            acc_recycled += observer.counts.recycled_nodes;
            acc_roots += observer.counts.root_nodes;
            acc_reused += observer.counts.reused_nodes;
            acc_path_states += observer.counts.path_valid_states;
            acc_eval_hit_ns += observer.counts.eval_hit_ns;
            acc_eval_miss_ns += observer.counts.eval_miss_ns;
            acc_parent_ns += observer.counts.parent_ns;
            acc_alloc_ns += observer.counts.alloc_ns;
            acc_search_ns += CmpSearch::search_ns;
            acc_norm_ns += CmpSearch::norm_ns;
            acc_transition_ns += CmpTOJ::transition_ns;
            acc_path_ms += path_ms;
            node_sum += nodes_alloc;
        }

        if (dead)
        {
            ++dead_moves;
            map = m_tetris::TetrisMap(10, 40);
            next.clear();
            hold = ' ';
            combo = 0;
            b2b = 0;
            ++games;
        }
        if (warming && moves_done + 1 == opt.warmup_moves)
        {
            t_total0 = steady_clock::now();
        }
        ++moves_done;
    }

    double total_sec = duration<double>(steady_clock::now() - t_total0).count();
    auto show = [&](std::uint64_t v) -> std::string {
        return opt.telemetry ? std::to_string(v) : std::string("na");
    };
    auto show_timed = [&](std::uint64_t v) -> std::string {
        return opt.telemetry && opt.timers ? std::to_string(v) : std::string("na");
    };
    auto show_timed_ns = [&](std::int64_t v) -> std::string {
        return opt.telemetry && opt.timers ? std::to_string(v) : std::string("na");
    };

    if (opt.quiet)
    {
        std::println("PROFILE_CMP moves={} total_s={:.3f} min_ms={:.3f} median_ms={:.3f} p95_ms={:.3f} p99_ms={:.3f} max_ms={:.3f}"
            " eval_requests={} eval_hits={} eval_calls={} transitions={} searches={} widening_iters={} parents={}"
            " raw_landings={} unique_candidates={} unmatched_candidates={} materialized_nodes={} recycled_nodes={} search_roots={} reused_nodes={} dedup_survivors={}"
            " path_states={} path_ms={:.3f} dead_moves={} games={} node_pool_bytes={}"
            " eval_hit_ns={} eval_miss_ns={} parent_ns={} search_ns={} transition_ns={}"
            " warmup_moves={} seed={} iters={} maxdepth={} budget_ms={} mode={} telemetry={}"
            " alloc_ns={} norm_ns={} timers={}",
            move_ms.size(), total_sec,
            move_ms.empty() ? 0 : *std::min_element(move_ms.begin(), move_ms.end()),
            move_ms.empty() ? 0 : pct(move_ms, 0.5), pct(move_ms, 0.95), pct(move_ms, 0.99),
            move_ms.empty() ? 0 : *std::max_element(move_ms.begin(), move_ms.end()),
            show(acc_eval_requests), show(acc_eval_hits), show(acc_eval_calls),
            show(acc_transitions), show(acc_searches), show(acc_widening), show(acc_parents),
            show(acc_raw), show_timed(acc_unique), show_timed(acc_unmatched), show(acc_fresh + acc_recycled),
            show(acc_recycled), show(acc_roots), show(acc_reused), show(acc_fresh + acc_recycled + acc_reused),
            show(acc_path_states), acc_path_ms, dead_moves, games, node_sum,
            show_timed_ns(acc_eval_hit_ns), show_timed_ns(acc_eval_miss_ns), show_timed_ns(acc_parent_ns),
            show_timed_ns(acc_search_ns), show_timed_ns(acc_transition_ns),
            opt.warmup_moves, opt.seed, opt.iters, opt.maxdepth,
            opt.ms > 0 ? opt.ms : 0.0, opt.iters > 0 ? "iters" : "ms",
            opt.telemetry ? "on" : "off",
            show_timed_ns(acc_alloc_ns), show_timed_ns(acc_norm_ns),
            !opt.telemetry ? "na" : (opt.timers ? "on" : "off"));
        return 0;
    }

    std::println("=== Legacy comparison profile (instrumented, diagnostic only) ===");
    std::println("moves: {} (games: {}, dead-moves: {})", move_ms.size(), games, dead_moves);
    std::println("total wall time: {:.3f} s", total_sec);
    std::println("eval requests {} (hits {} | calls {}) | transitions {} | searches {}",
        acc_eval_requests, acc_eval_hits, acc_eval_calls, acc_transitions, acc_searches);
    std::println("widening {} | parents {} | raw {} | unique {} | unmatched {}",
        acc_widening, acc_parents, acc_raw, acc_unique, acc_unmatched);
    std::println("materialized {} | recycled {} | reused {} | path states {} | path {:.3f} ms",
        acc_fresh + acc_recycled, acc_recycled, acc_reused, acc_path_states, acc_path_ms);
    return 0;
}
