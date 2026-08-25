// tetris_profile.cpp
// Standalone profiling/benchmark driver for m_tetris::TetrisEngine using the
// ai_zzz::TOJ configuration (rule_toj + search_tspin), mirroring the setup of
// "srs_ai" in src/ai.cpp (TetrisAI entry point).
//
// It plays 7-bag games with hold against the real engine and reports per-move
// timing and eval/get/search call counts, so hotspots and throughput of the
// engine can be measured. Wrapper types (ProfiledTOJ / ProfiledSearch) count
// the number of AI eval()/get() and Search::search() calls the engine makes.
//
// Usage:
//   tetris_profile [--moves N] [--level L | --ms T] [--seed S] [--maxdepth D]
//                  [--no-hold] [--param-file file] [--quiet]
//   --level L : time budget per move = pow(100, L/8) ms, exactly like ai.cpp
//   --ms T    : fixed time budget per move in ms (overrides --level)

#include "tetris_core.h"
#include "rule_toj.h"
#include "ai_zzz.h"
#include "search_tspin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace
{
    struct ProfiledTOJ : ai_zzz::TOJ
    {
        static size_t evals, gets;
        static void reset()
        {
            evals = 0;
            gets = 0;
        }
        Result eval(TetrisNodeEx const &node, m_tetris::TetrisMap const &map, m_tetris::TetrisMap const &src_map) const
        {
            ++evals;
            return ai_zzz::TOJ::eval(node, map, src_map);
        }
        Status get(TetrisNodeEx &node, Result const &eval_result, size_t clear, m_tetris::TetrisMap const &map, size_t depth, Status const &status, m_tetris::TetrisContext::Env const &env) const
        {
            ++gets;
            return ai_zzz::TOJ::get(node, eval_result, clear, map, depth, status, env);
        }
    };
    size_t ProfiledTOJ::evals = 0;
    size_t ProfiledTOJ::gets = 0;

    struct ProfiledSearch : search_tspin::Search
    {
        static size_t searches;
        static void reset()
        {
            searches = 0;
        }
        std::vector<TetrisNodeWithTSpinType> const *search(m_tetris::TetrisMap const &map, m_tetris::TetrisNode const *node, size_t depth)
        {
            ++searches;
            return search_tspin::Search::search(map, node, depth);
        }
    };
    size_t ProfiledSearch::searches = 0;

    using Engine = m_tetris::TetrisEngine<rule_toj::TetrisRule, ProfiledTOJ, ProfiledSearch>;

    ai_zzz::TOJ::Param const default_param = {
        10.507166148, 7.539860726, 13.048099725, 13.388476179, 6.728747539, 9.476881786,
        0.258534525, -0.108269503, 4.394241496, -4.892359035, 0.049148374, 1.586714505,
        8.885878229, -0.006001836, -0.004336234, -2.021765056, -0.951446468, -1.145468832,
        -1.515758227, -0.612910192, -0.476031978, 0.009596827, -0.399212013, -0.855819915,
        -0.418779377, -0.454784178, -1.417493065, 1.050941751, 0.756272086,
    };

    int const combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    int const combo_table_max = 10;

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
        double level = 10;      // time budget = pow(100, level/8) ms
        double ms = 0;          // 0 => derive from level
        uint32_t seed = 1;
        size_t maxdepth = 6;
        bool hold = true;
        std::string param_file;
        bool quiet = false;
        size_t iters = 0;   // >0 => iteration-based deterministic search
    };

    Options parse_args(int argc, char **argv)
    {
        Options opt;
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
            if (a == "--moves") opt.moves = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--level") opt.level = std::strtod(next(a).c_str(), nullptr);
            else if (a == "--ms") opt.ms = std::strtod(next(a).c_str(), nullptr);
            else if (a == "--seed") opt.seed = static_cast<uint32_t>(std::strtoul(next(a).c_str(), nullptr, 10));
            else if (a == "--maxdepth") opt.maxdepth = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--no-hold") opt.hold = false;
            else if (a == "--param-file") opt.param_file = next(a);
            else if (a == "--iters") opt.iters = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--quiet") opt.quiet = true;
            else
            {
                std::println(stderr, "unknown option: {}", a);
                std::exit(1);
            }
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
    time_t budget_ms = opt.ms > 0 ? static_cast<time_t>(opt.ms) : static_cast<time_t>(std::pow(100.0, opt.level / 8.0));

    Engine engine;
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
    std::vector<size_t> move_evals, move_gets, move_searches, move_nodes;

    steady_clock::time_point t_total0 = steady_clock::now();

    while (moves_done < opt.moves)
    {
        // refill the 7-bag queue
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
        ProfiledTOJ::reset();
        ProfiledSearch::reset();

        steady_clock::time_point t0 = steady_clock::now();
        auto result = opt.iters > 0
            ? engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1, opt.maxdepth, m_tetris::SearchBudget::by_iterations(opt.iters))
            : engine.run_hold(map, engine.context()->generate(current), hold, true, next.data() + 1, opt.maxdepth, budget_ms);
        steady_clock::time_point t1 = steady_clock::now();
        double elapsed_ms = duration<double, std::milli>(t1 - t0).count();
        size_t nodes_alloc = engine.memory_usage() - mem_before;
        move_ms.push_back(elapsed_ms);
        move_evals.push_back(ProfiledTOJ::evals);
        move_gets.push_back(ProfiledTOJ::gets);
        move_searches.push_back(ProfiledSearch::searches);
        move_nodes.push_back(nodes_alloc);

        bool dead = false;
        size_t clear = 0;
        if (result.target == nullptr || result.target->row >= 20)
        {
            dead = true;
        }
        else
        {
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
        ++moves_done;
    }

    double total_sec = duration<double>(steady_clock::now() - t_total0).count();
    size_t total_evals = 0, total_gets = 0, total_searches = 0;
    for (size_t i = 0; i < move_ms.size(); ++i)
    {
        total_evals += move_evals[i];
        total_gets += move_gets[i];
        total_searches += move_searches[i];
    }

    if (opt.quiet)
    {
        std::println("{} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {:.3f} {} {} {} {} {:.0f} {:.0f} {:.0f} {}",
            move_ms.size(), total_sec,
            move_ms.empty() ? 0 : *std::min_element(move_ms.begin(), move_ms.end()),
            move_ms.empty() ? 0 : pct(move_ms, 0.5), pct(move_ms, 0.95), pct(move_ms, 0.99),
            move_ms.empty() ? 0 : *std::max_element(move_ms.begin(), move_ms.end()),
            total_evals, total_gets, total_searches, dead_moves,
            total_evals / total_sec, total_gets / total_sec, total_searches / total_sec, games);
        return 0;
    }

    std::println("=== TetrisEngine profile (rule_toj + ai_zzz::TOJ + search_tspin) ===");
    std::println("board 10x40, budget {}, maxdepth {}, hold {}, seed {}",
        opt.iters > 0 ? (std::to_string(opt.iters) + " iters [DETERMINISTIC]") : (std::to_string(budget_ms) + " ms/move"),
        opt.maxdepth, opt.hold ? "on" : "off", opt.seed);
    std::println("moves: {} (games completed: {}, dead-moves: {})", move_ms.size(), games, dead_moves);
    std::println("total wall time: {:.3f} s", total_sec);
    std::println("per-move wall time [ms]: min {:.3f} | median {:.3f} | p95 {:.3f} | p99 {:.3f} | max {:.3f} | avg {:.3f}",
        move_ms.empty() ? 0 : *std::min_element(move_ms.begin(), move_ms.end()),
        pct(move_ms, 0.5), pct(move_ms, 0.95), pct(move_ms, 0.99),
        move_ms.empty() ? 0 : *std::max_element(move_ms.begin(), move_ms.end()),
        total_sec * 1000.0 / std::max<size_t>(1, move_ms.size()));
    std::println("eval calls:  total {} | per move avg {:.1f} | rate {:.0f}/s",
        total_evals, double(total_evals) / std::max<size_t>(1, move_ms.size()), total_evals / total_sec);
    std::println("get  calls:  total {} | per move avg {:.1f} | rate {:.0f}/s",
        total_gets, double(total_gets) / std::max<size_t>(1, move_ms.size()), total_gets / total_sec);
    std::println("search calls:total {} | per move avg {:.1f} | rate {:.0f}/s",
        total_searches, double(total_searches) / std::max<size_t>(1, move_ms.size()), total_searches / total_sec);
    size_t node_sum = 0;
    for (size_t n : move_nodes) node_sum += n;
    std::println("tree node pool growth (new allocs): total {} | per move avg {:.1f}", node_sum, double(node_sum) / std::max<size_t>(1, move_nodes.size()));
    std::println("engine memory usage: {} bytes ({:.1f} MB)",
        engine.memory_usage(), engine.memory_usage() / (1024.0 * 1024.0));
    std::println("game stats: clears {}, attack {}, final roof {}, b2b {}, combo {}",
        total_clear, total_attack, map.roof, b2b, combo);
    return 0;
}
