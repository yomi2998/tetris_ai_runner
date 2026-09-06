#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace profile_value
{
    inline constexpr char bag_order[] = { 'I', 'J', 'L', 'O', 'S', 'T', 'Z' };
    inline constexpr std::size_t bag_size = 7;
    inline constexpr std::size_t max_supported_maxdepth = 255;

    inline constexpr int combo_table[] = { 0, 0, 0, 1, 1, 2, 2, 3, 3, 4 };
    inline constexpr int combo_table_max = 10;

    enum class SpinClass : std::uint8_t
    {
        None,
        Mini,
        Full,
    };

    struct Options
    {
        std::size_t moves = 200;
        double level = 10;
        double ms = 0;
        std::uint32_t seed = 1;
        std::size_t maxdepth = 6;
        bool hold = true;
        std::string param_file;
        bool quiet = false;
        std::size_t iters = 0;
        std::size_t warmup_moves = 0;
        int quiet_version = 3;
        bool telemetry = true;
    };

    inline Options parse_args(int argc, char **argv)
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
            else if (a == "--seed") opt.seed = static_cast<std::uint32_t>(std::strtoul(next(a).c_str(), nullptr, 10));
            else if (a == "--maxdepth") opt.maxdepth = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--no-hold") opt.hold = false;
            else if (a == "--param-file") opt.param_file = next(a);
            else if (a == "--iters") opt.iters = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--warmup-moves") opt.warmup_moves = std::strtoull(next(a).c_str(), nullptr, 10);
            else if (a == "--quiet-version") opt.quiet_version = std::atoi(next(a).c_str());
            else if (a == "--telemetry") opt.telemetry = std::string(next(a)) != "off";
            else if (a == "--quiet") opt.quiet = true;
            else
            {
                std::println(stderr, "unknown option: {}", a);
                std::exit(1);
            }
        }
        if (opt.quiet_version != 3)
        {
            std::println(stderr, "only --quiet-version 3 is supported");
            std::exit(1);
        }
        if (opt.maxdepth > max_supported_maxdepth)
        {
            std::println(stderr, "maxdepth out of supported range");
            std::exit(1);
        }
        return opt;
    }

    class Scenario
    {
    public:
        explicit Scenario(std::uint32_t seed)
            : rng_(seed)
        {
        }

        void start_move(std::size_t maxdepth)
        {
            if (!next_.empty())
            {
                next_.erase(next_.begin());
            }
            while (next_.size() <= maxdepth)
            {
                for (std::size_t i = 0; i < bag_size; ++i)
                {
                    next_.push_back(bag_order[i]);
                }
                std::shuffle(next_.end() - bag_size, next_.end(), rng_);
            }
        }

        void pop_played_extra()
        {
            if (!next_.empty())
            {
                next_.erase(next_.begin());
            }
        }

        void reset_on_death()
        {
            next_.clear();
        }

        char current() const
        {
            return next_.front();
        }

        std::vector<char> const &queue() const
        {
            return next_;
        }

    private:
        std::mt19937 rng_;
        std::vector<char> next_;
    };

    inline int combo_attack(int combo)
    {
        return combo_table[std::min(combo_table_max - 1, combo)];
    }

    inline int score_attack(int clear, SpinClass spin, bool board_empty, int &combo, int &b2b)
    {
        int attack = 0;
        auto advance_combo = [&]() {
            attack += combo_attack(++combo);
        };
        switch (clear)
        {
        case 0:
            combo = 0;
            break;
        case 1:
            if (spin == SpinClass::Mini) { attack += 1 + b2b; b2b = 1; }
            else if (spin == SpinClass::Full) { attack += 2 + b2b; b2b = 1; }
            else { b2b = 0; }
            advance_combo();
            break;
        case 2:
            if (spin != SpinClass::None) { attack += 4 + b2b; b2b = 1; }
            else { attack += 1; b2b = 0; }
            advance_combo();
            break;
        case 3:
            if (spin != SpinClass::None) { attack += 6 + b2b * 2; b2b = 1; }
            else { attack += 2; b2b = 0; }
            advance_combo();
            break;
        case 4:
            advance_combo();
            attack += 4 + b2b;
            b2b = 1;
            break;
        default:
            break;
        }
        if (board_empty) attack += 6;
        return attack;
    }

    inline double percentile(std::vector<double> const &v, double p)
    {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        std::size_t idx = std::min(s.size() - 1, static_cast<std::size_t>(p * s.size()));
        return s[idx];
    }

    struct V3Row
    {
        std::size_t moves = 0;
        double total_s = 0;
        double min_ms = 0;
        double median_ms = 0;
        double p95_ms = 0;
        double p99_ms = 0;
        double max_ms = 0;
        std::optional<std::int64_t> evals;
        std::optional<std::int64_t> transitions;
        std::optional<std::int64_t> searches;
        std::int64_t dead_moves = 0;
        std::int64_t games = 0;
        std::int64_t node_live_delta_bytes = 0;
        std::optional<double> evals_per_s;
        std::optional<double> transitions_per_s;
        std::optional<double> searches_per_s;
        std::size_t warmup_moves = 0;
        std::uint32_t seed = 0;
        std::size_t iters = 0;
        std::size_t maxdepth = 0;
        double budget_ms = 0;
        std::string mode;
        std::string telemetry;
        double emove_min_ms = 0;
        double emove_med_ms = 0;
        double emove_p95_ms = 0;
        double emove_p99_ms = 0;
        double emove_max_ms = 0;
        double setup_ms = 0;
        double setup_eval_ms = 0;
        double run_ms = 0;
        double path_ms = 0;
        double apply_ms = 0;
        double init_ms = 0;
        std::optional<std::int64_t> parents;
        std::optional<std::int64_t> parent_ns;
        std::optional<std::int64_t> widening_iters;
        std::optional<std::int64_t> enum_ns;
        std::optional<std::int64_t> raw_landings;
        std::optional<std::int64_t> unique_candidates;
        std::optional<std::int64_t> rule_transitions;
        std::optional<std::int64_t> rule_ns;
        std::optional<std::int64_t> eval_hit_ns;
        std::optional<std::int64_t> eval_miss_ns;
        std::optional<std::int64_t> eval_memo_hits;
        std::optional<std::int64_t> eval_computed;
        std::optional<std::int64_t> cache_requests;
        std::optional<std::int64_t> cache_hits;
        std::optional<std::int64_t> cache_misses;
        std::optional<std::int64_t> cache_replacements;
        std::optional<std::int64_t> materialized_nodes;
        std::optional<std::int64_t> materialize_ns;
        std::optional<std::int64_t> policy_ns;
        std::optional<std::int64_t> transposition_merges;
        std::optional<std::int64_t> promotions_refused;
        std::optional<std::int64_t> pending_end_max;
        std::optional<std::int64_t> texhaust_moves;
        std::optional<std::int64_t> path_calls;
        std::optional<std::int64_t> path_states;
        std::optional<std::int64_t> path_find_ns;
        std::optional<std::int64_t> path_replay_ns;
        std::optional<std::int64_t> replay_failures;
        std::int64_t mem_retained_bytes = 0;
        std::int64_t arena_reserved_bytes = 0;
        std::int64_t idmap_reserved_bytes = 0;
        std::optional<std::int64_t> raw_unique_ratio_x1000;
    };

    inline std::string format_count(std::optional<std::int64_t> v)
    {
        return v.has_value() ? std::to_string(*v) : std::string("na");
    }

    inline std::string format_rate(std::optional<double> v)
    {
        return v.has_value() ? std::format("{:.0f}", *v) : std::string("na");
    }

    inline std::string format_v3(V3Row const &r)
    {
        std::string out = "PROFILE_V3";
        out += std::format(" moves={} total_s={:.3f}", r.moves, r.total_s);
        out += std::format(" min_ms={:.3f} median_ms={:.3f} p95_ms={:.3f} p99_ms={:.3f} max_ms={:.3f}",
            r.min_ms, r.median_ms, r.p95_ms, r.p99_ms, r.max_ms);
        out += " evals=" + format_count(r.evals);
        out += " transitions=" + format_count(r.transitions);
        out += " searches=" + format_count(r.searches);
        out += std::format(" dead_moves={} games={} node_live_delta_bytes={}",
            r.dead_moves, r.games, r.node_live_delta_bytes);
        out += " evals_per_s=" + format_rate(r.evals_per_s);
        out += " transitions_per_s=" + format_rate(r.transitions_per_s);
        out += " searches_per_s=" + format_rate(r.searches_per_s);
        out += std::format(" warmup_moves={} seed={} iters={} maxdepth={} budget_ms={:.3f} mode={} telemetry={}",
            r.warmup_moves, r.seed, r.iters, r.maxdepth, r.budget_ms, r.mode, r.telemetry);
        out += std::format(" emove_min_ms={:.3f} emove_med_ms={:.3f} emove_p95_ms={:.3f} emove_p99_ms={:.3f} emove_max_ms={:.3f}",
            r.emove_min_ms, r.emove_med_ms, r.emove_p95_ms, r.emove_p99_ms, r.emove_max_ms);
        out += std::format(" setup_ms={:.3f} setup_eval_ms={:.3f} run_ms={:.3f} path_ms={:.3f} apply_ms={:.3f} init_ms={:.3f}",
            r.setup_ms, r.setup_eval_ms, r.run_ms, r.path_ms, r.apply_ms, r.init_ms);
        out += " parents=" + format_count(r.parents);
        out += " parent_ns=" + format_count(r.parent_ns);
        out += " widening_iters=" + format_count(r.widening_iters);
        out += " enum_ns=" + format_count(r.enum_ns);
        out += " raw_landings=" + format_count(r.raw_landings);
        out += " unique_candidates=" + format_count(r.unique_candidates);
        out += " rule_transitions=" + format_count(r.rule_transitions);
        out += " rule_ns=" + format_count(r.rule_ns);
        out += " eval_hit_ns=" + format_count(r.eval_hit_ns);
        out += " eval_miss_ns=" + format_count(r.eval_miss_ns);
        out += " eval_memo_hits=" + format_count(r.eval_memo_hits);
        out += " eval_computed=" + format_count(r.eval_computed);
        out += " cache_requests=" + format_count(r.cache_requests);
        out += " cache_hits=" + format_count(r.cache_hits);
        out += " cache_misses=" + format_count(r.cache_misses);
        out += " cache_replacements=" + format_count(r.cache_replacements);
        out += " materialized_nodes=" + format_count(r.materialized_nodes);
        out += " materialize_ns=" + format_count(r.materialize_ns);
        out += " policy_ns=" + format_count(r.policy_ns);
        out += " transposition_merges=" + format_count(r.transposition_merges);
        out += " promotions_refused=" + format_count(r.promotions_refused);
        out += " pending_end_max=" + format_count(r.pending_end_max);
        out += " texhaust_moves=" + format_count(r.texhaust_moves);
        out += " path_calls=" + format_count(r.path_calls);
        out += " path_states=" + format_count(r.path_states);
        out += " path_find_ns=" + format_count(r.path_find_ns);
        out += " path_replay_ns=" + format_count(r.path_replay_ns);
        out += " replay_failures=" + format_count(r.replay_failures);
        out += std::format(" mem_retained_bytes={} arena_reserved_bytes={} idmap_reserved_bytes={}",
            r.mem_retained_bytes, r.arena_reserved_bytes, r.idmap_reserved_bytes);
        out += " raw_unique_ratio_x1000=" + format_count(r.raw_unique_ratio_x1000);
        return out;
    }
}
