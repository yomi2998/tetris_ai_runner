#pragma once

#include "profile_value_support.h"
#include "tetris_board.h"
#include "tetris_engine.h"
#include "tetris_types.h"
#include "toj_policy.h"
#include "toj_rule.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace profile_value
{
    inline std::int64_t steady_nanos()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    struct MoveRecord
    {
        enum class Kind
        {
            Placed,
            SpawnDeath,
            LockoutDeath,
            Invalid,
        };

        Kind kind = Kind::Invalid;
        std::string invalid_reason;
        double setup_ms = 0;
        double setup_eval_ms = 0;
        double rootsearch_ms = 0;
        double run_ms = 0;
        double path_ms = 0;
        double apply_ms = 0;
        double emove_ms = 0;
        tetris_engine::SearchStats stats{};
        tetris_engine::ComponentTimers timers{};
        std::int64_t path_calls = 0;
        std::int64_t path_states = 0;
        std::int64_t path_failures = 0;
        std::int64_t arena_delta_bytes = 0;
        std::size_t table_used = 0;
        bool used_hold = false;
        tetris::Piece played = tetris::Piece::T;
        tetris::Candidate candidate{};
        bool has_candidate = false;
        std::string path_commands;
        int clear_count = 0;
        SpinClass spin = SpinClass::None;
        bool perfect_clear = false;
    };

    struct Totals
    {
        std::vector<double> rootsearch_ms;
        std::vector<double> emove_ms;
        double setup_ms = 0;
        double setup_eval_ms = 0;
        double run_ms = 0;
        double path_ms = 0;
        double apply_ms = 0;
        std::int64_t dead_moves = 0;
        std::int64_t games = 0;
        std::int64_t total_clear = 0;
        std::int64_t total_attack = 0;
        std::int64_t parents = 0;
        std::int64_t widening_iters = 0;
        std::int64_t enum_calls = 0;
        std::int64_t raw_landings = 0;
        std::int64_t unique_candidates = 0;
        std::int64_t rule_transitions = 0;
        std::int64_t eval_requests = 0;
        std::int64_t eval_memo_hits = 0;
        std::int64_t eval_computed = 0;
        std::int64_t cache_requests = 0;
        std::int64_t cache_hits = 0;
        std::int64_t cache_misses = 0;
        std::int64_t cache_replacements = 0;
        std::int64_t materialized_nodes = 0;
        std::int64_t policy_transitions = 0;
        std::int64_t transposition_merges = 0;
        std::int64_t promotions_refused = 0;
        std::int64_t texhaust_moves = 0;
        std::size_t pending_end_max = 0;
        std::int64_t path_calls = 0;
        std::int64_t path_states = 0;
        std::int64_t replay_failures = 0;
        std::int64_t enum_ns = 0;
        std::int64_t rule_ns = 0;
        std::int64_t eval_hit_ns = 0;
        std::int64_t eval_miss_ns = 0;
        std::int64_t materialize_ns = 0;
        std::int64_t policy_ns = 0;
        std::int64_t parent_ns = 0;
        std::int64_t path_find_ns = 0;
        std::int64_t path_replay_ns = 0;
        std::int64_t node_live_delta_bytes = 0;

        void add(MoveRecord const &record, int attack)
        {
            rootsearch_ms.push_back(record.rootsearch_ms);
            emove_ms.push_back(record.emove_ms);
            setup_ms += record.setup_ms;
            setup_eval_ms += record.setup_eval_ms;
            run_ms += record.run_ms;
            path_ms += record.path_ms;
            apply_ms += record.apply_ms;
            total_clear += record.clear_count;
            total_attack += attack;
            parents += static_cast<std::int64_t>(record.stats.expanded_parents);
            widening_iters += static_cast<std::int64_t>(record.stats.widening_passes);
            enum_calls += static_cast<std::int64_t>(record.stats.enumeration_calls);
            raw_landings += static_cast<std::int64_t>(record.stats.raw_kernel_landings);
            unique_candidates += static_cast<std::int64_t>(record.stats.unique_candidates);
            rule_transitions += static_cast<std::int64_t>(record.stats.rule_applications);
            eval_requests += static_cast<std::int64_t>(record.stats.eval_requests);
            eval_memo_hits += static_cast<std::int64_t>(record.stats.eval_memo_hits);
            eval_computed += static_cast<std::int64_t>(record.stats.eval_computed);
            cache_requests += static_cast<std::int64_t>(record.stats.cache_requests);
            cache_hits += static_cast<std::int64_t>(record.stats.cache_hits);
            cache_misses += static_cast<std::int64_t>(record.stats.cache_misses);
            cache_replacements +=
                static_cast<std::int64_t>(record.stats.cache_replacements);
            materialized_nodes +=
                static_cast<std::int64_t>(record.stats.materialized_nodes);
            policy_transitions +=
                static_cast<std::int64_t>(record.stats.policy_transitions);
            transposition_merges +=
                static_cast<std::int64_t>(record.stats.transposition_merges);
            promotions_refused +=
                static_cast<std::int64_t>(record.stats.promotions_refused);
            texhaust_moves += record.stats.transposition_exhausted ? 1 : 0;
            pending_end_max =
                std::max(pending_end_max, record.stats.pending_occupancy);
            path_calls += record.path_calls;
            path_states += record.path_states;
            replay_failures += record.path_failures;
            enum_ns += record.timers.enum_ns;
            rule_ns += record.timers.rule_ns;
            eval_hit_ns += record.timers.eval_hit_ns;
            eval_miss_ns += record.timers.eval_miss_ns;
            materialize_ns += record.timers.materialize_ns;
            policy_ns += record.timers.policy_ns;
            parent_ns += record.timers.parent_ns;
            path_find_ns += record.timers.path_find_ns;
            path_replay_ns += record.timers.path_replay_ns;
            node_live_delta_bytes += record.arena_delta_bytes;
        }

        void add_death()
        {
            ++dead_moves;
            ++games;
        }
    };

    class Runner
    {
    public:
        struct Config
        {
            std::size_t maxdepth = 6;
            bool hold = true;
            std::size_t iters = 0;
            std::uint64_t budget_ms = 0;
        };

        Runner(toj_policy::Config &policy_config, toj_policy::Policy &seed_policy,
            tetris_engine::Engine &engine, std::uint32_t seed, Config config,
            std::function<std::int64_t()> clock = steady_nanos)
            : policy_config_(&policy_config)
            , seed_policy_(&seed_policy)
            , engine_(&engine)
            , scenario_(seed)
            , config_(config)
            , clock_(std::move(clock))
        {
        }

        tetris::Board const &board() const
        {
            return board_;
        }

        std::vector<char> const &queue() const
        {
            return scenario_.queue();
        }

        std::optional<tetris::Piece> hold() const
        {
            return hold_piece_;
        }

        int combo() const
        {
            return combo_;
        }

        int b2b() const
        {
            return b2b_;
        }

        MoveRecord step()
        {
            MoveRecord record;
            std::int64_t t_setup0 = now();
            scenario_.start_move(config_.maxdepth);
            auto current_piece = tetris::try_from_char(scenario_.current());
            if (!current_piece.has_value())
            {
                return invalid(record, "scenario produced an unknown piece");
            }
            tetris::Piece current = *current_piece;
            std::size_t arena_before = engine_->arena_size();
            tetris::Placement const spawn = tetris::Placement::unchecked(
                tetris::toj::spawn_x, tetris::toj::spawn_y, 0);

            if (!tetris::toj::fits(current, spawn, board_))
            {
                record.kind = MoveRecord::Kind::SpawnDeath;
                record.setup_ms = millis(now() - t_setup0);
                record.emove_ms = record.setup_ms;
                reset_on_death();
                return record;
            }

            toj_policy::State root_state;
            root_state.combo = static_cast<std::int8_t>(combo_);
            root_state.b2b = static_cast<std::int8_t>(b2b_ ? 1 : 0);
            policy_config_->safe = seed_policy_->safe_margin(board_, current);
            std::int64_t t_eval0 = now();
            toj_policy::Evaluation seed_eval = seed_policy_->evaluate(board_);
            record.setup_eval_ms = millis(now() - t_eval0);
            root_state.t2_value = seed_eval.t2_value;
            root_state.t3_value = seed_eval.t3_value;

            tetris_engine::HoldState hold;
            hold.piece = config_.hold ? hold_piece_ : std::nullopt;
            hold.locked = !config_.hold;

            tetris_engine::Queue queue;
            for (std::size_t i = 0; i <= config_.maxdepth; ++i)
            {
                auto piece = tetris::try_from_char(scenario_.queue()[i]);
                if (!piece.has_value())
                {
                    return invalid(record, "scenario queue holds an unknown piece");
                }
                queue.pieces.push_back(*piece);
                queue.boundary.push_back(false);
            }

            std::int64_t t_rootsearch0 = now();
            tetris_engine::NodeId root =
                engine_->set_root(board_, root_state, queue, hold);
            std::int64_t t_setup1 = now();
            if (root == tetris_engine::no_node)
            {
                return invalid(record,
                    "root rejected (invalid queue, full row, or exhausted capacity)");
            }

            tetris_engine::SearchBudget budget = config_.iters > 0
                ? tetris_engine::SearchBudget::by_iterations(config_.iters)
                : tetris_engine::SearchBudget::by_time(config_.budget_ms);
            std::int64_t t_run0 = now();
            engine_->run(budget);
            std::int64_t t_run1 = now();

            tetris_engine::PathTelemetry path_before = engine_->path_telemetry();
            tetris_engine::FinalResult result = engine_->finalize(spawn);
            std::int64_t t_path1 = now();

            record.stats = engine_->search_stats();
            record.timers = engine_->component_timers();
            record.table_used = engine_->transposition_used();
            tetris_engine::PathTelemetry path_after = engine_->path_telemetry();
            record.path_calls = static_cast<std::int64_t>(path_after.calls)
                - static_cast<std::int64_t>(path_before.calls);
            record.path_states =
                static_cast<std::int64_t>(path_after.states_expanded)
                - static_cast<std::int64_t>(path_before.states_expanded);
            record.path_failures = static_cast<std::int64_t>(path_after.failures)
                - static_cast<std::int64_t>(path_before.failures);
            record.arena_delta_bytes =
                static_cast<std::int64_t>(engine_->arena_size())
                    * static_cast<std::int64_t>(sizeof(tetris_engine::Node))
                - static_cast<std::int64_t>(arena_before)
                    * static_cast<std::int64_t>(sizeof(tetris_engine::Node));

            record.setup_ms = millis(t_setup1 - t_setup0);
            record.rootsearch_ms = millis(t_run1 - t_rootsearch0);
            record.run_ms = millis(t_run1 - t_run0);
            record.path_ms = millis(t_path1 - t_run1);
            record.emove_ms = millis(t_path1 - t_setup0);

            if (!result.has_selection || !result.path_ok)
            {
                return invalid(record,
                    "finalization failed (missing selection or unverified path)");
            }
            record.used_hold = result.used_hold;
            record.played = result.played;
            record.candidate = *result.candidate;
            record.has_candidate = true;
            record.path_commands = std::string(result.path.view());

            if (toj_policy::Policy::is_lockout(
                    result.played, result.candidate->placement))
            {
                record.kind = MoveRecord::Kind::LockoutDeath;
                reset_on_death();
                return record;
            }

            std::int64_t t_apply0 = now();
            std::optional<tetris::toj::RuleResult> applied =
                tetris::toj::apply(board_, result.played, *result.candidate);
            if (!applied.has_value())
            {
                return invalid(record,
                    "rule application rejected the finalized candidate");
            }
            record.spin = static_cast<SpinClass>(
                static_cast<std::uint8_t>(applied->spin));
            record.clear_count = applied->clear_count;
            record.perfect_clear = applied->perfect_clear;
            int const attack = score_attack(applied->clear_count, record.spin,
                applied->perfect_clear, combo_, b2b_);
            board_ = applied->board;
            bool hold_was_empty = !hold_piece_.has_value();
            if (result.used_hold)
            {
                hold_piece_ = current;
                if (hold_was_empty)
                {
                    scenario_.pop_played_extra();
                }
            }
            record.apply_ms = millis(now() - t_apply0);
            record.emove_ms += record.apply_ms;
            record.kind = MoveRecord::Kind::Placed;
            last_attack_ = attack;
            return record;
        }

        int last_attack() const
        {
            return last_attack_;
        }

        void test_set_board(tetris::Board board)
        {
            board_ = board;
        }

    private:
        toj_policy::Config *policy_config_;
        toj_policy::Policy *seed_policy_;
        tetris_engine::Engine *engine_;
        Scenario scenario_;
        Config config_;
        std::function<std::int64_t()> clock_;
        tetris::Board board_;
        std::optional<tetris::Piece> hold_piece_;
        int combo_ = 0;
        int b2b_ = 0;
        int last_attack_ = 0;

        std::int64_t now() const
        {
            return clock_();
        }

        static double millis(std::int64_t nanos)
        {
            return static_cast<double>(nanos) / 1e6;
        }

        MoveRecord invalid(MoveRecord &record, std::string const &reason)
        {
            record.kind = MoveRecord::Kind::Invalid;
            record.invalid_reason = reason;
            return record;
        }

        void reset_on_death()
        {
            board_ = tetris::Board{};
            scenario_.reset_on_death();
            hold_piece_.reset();
            combo_ = 0;
            b2b_ = 0;
        }
    };

    inline V3Row build_v3_row(Totals const &totals, Options const &opt,
        double total_sec, double init_ms, std::int64_t mem_retained,
        std::int64_t arena_reserved, std::int64_t idmap_reserved, bool telemetry)
    {
        auto as_count = [&](std::int64_t v) -> std::optional<std::int64_t> {
            return telemetry ? std::optional<std::int64_t>(v) : std::nullopt;
        };
        auto as_time = [&](std::int64_t v) -> std::optional<std::int64_t> {
            return telemetry && opt.timers ? std::optional<std::int64_t>(v) : std::nullopt;
        };
        V3Row row;
        row.moves = totals.rootsearch_ms.size();
        row.total_s = total_sec;
        row.min_ms = totals.rootsearch_ms.empty()
            ? 0
            : *std::min_element(
                totals.rootsearch_ms.begin(), totals.rootsearch_ms.end());
        row.median_ms = percentile(totals.rootsearch_ms, 0.5);
        row.p95_ms = percentile(totals.rootsearch_ms, 0.95);
        row.p99_ms = percentile(totals.rootsearch_ms, 0.99);
        row.max_ms = totals.rootsearch_ms.empty()
            ? 0
            : *std::max_element(
                totals.rootsearch_ms.begin(), totals.rootsearch_ms.end());
        row.evals = as_count(totals.eval_requests);
        row.transitions = as_count(totals.policy_transitions);
        row.searches = as_count(totals.enum_calls);
        row.dead_moves = totals.dead_moves;
        row.games = totals.games;
        row.node_live_delta_bytes = totals.node_live_delta_bytes;
        if (telemetry)
        {
            row.evals_per_s = static_cast<double>(totals.eval_requests) / total_sec;
            row.transitions_per_s =
                static_cast<double>(totals.policy_transitions) / total_sec;
            row.searches_per_s = static_cast<double>(totals.enum_calls) / total_sec;
        }
        row.warmup_moves = opt.warmup_moves;
        row.seed = opt.seed;
        row.iters = opt.iters;
        row.maxdepth = opt.maxdepth;
        row.budget_ms = opt.ms > 0 ? opt.ms : 0.0;
        row.mode = opt.iters > 0 ? "iters" : "ms";
        row.telemetry = telemetry ? "on" : "off";
        row.timers = !telemetry ? "na" : (opt.timers ? "on" : "off");
        row.emove_min_ms = totals.emove_ms.empty()
            ? 0
            : *std::min_element(totals.emove_ms.begin(), totals.emove_ms.end());
        row.emove_med_ms = percentile(totals.emove_ms, 0.5);
        row.emove_p95_ms = percentile(totals.emove_ms, 0.95);
        row.emove_p99_ms = percentile(totals.emove_ms, 0.99);
        row.emove_max_ms = totals.emove_ms.empty()
            ? 0
            : *std::max_element(totals.emove_ms.begin(), totals.emove_ms.end());
        row.setup_ms = totals.setup_ms;
        row.setup_eval_ms = totals.setup_eval_ms;
        row.run_ms = totals.run_ms;
        row.path_ms = totals.path_ms;
        row.apply_ms = totals.apply_ms;
        row.init_ms = init_ms;
        row.parents = as_count(totals.parents);
        row.parent_ns = as_time(totals.parent_ns);
        row.widening_iters = as_count(totals.widening_iters);
        row.enum_ns = as_time(totals.enum_ns);
        row.raw_landings = as_count(totals.raw_landings);
        row.unique_candidates = as_count(totals.unique_candidates);
        row.rule_transitions = as_count(totals.rule_transitions);
        row.rule_ns = as_time(totals.rule_ns);
        row.eval_hit_ns = as_time(totals.eval_hit_ns);
        row.eval_miss_ns = as_time(totals.eval_miss_ns);
        row.eval_memo_hits = as_count(totals.eval_memo_hits);
        row.eval_computed = as_count(totals.eval_computed);
        row.cache_requests = as_count(totals.cache_requests);
        row.cache_hits = as_count(totals.cache_hits);
        row.cache_misses = as_count(totals.cache_misses);
        row.cache_replacements = as_count(totals.cache_replacements);
        row.materialized_nodes = as_count(totals.materialized_nodes);
        row.materialize_ns = as_time(totals.materialize_ns);
        row.policy_ns = as_time(totals.policy_ns);
        row.transposition_merges = as_count(totals.transposition_merges);
        row.promotions_refused = as_count(totals.promotions_refused);
        row.pending_end_max = as_count(static_cast<std::int64_t>(totals.pending_end_max));
        row.texhaust_moves = as_count(totals.texhaust_moves);
        row.path_calls = as_count(totals.path_calls);
        row.path_states = as_count(totals.path_states);
        row.path_find_ns = as_time(totals.path_find_ns);
        row.path_replay_ns = as_time(totals.path_replay_ns);
        row.replay_failures = as_count(totals.replay_failures);
        row.mem_retained_bytes = mem_retained;
        row.arena_reserved_bytes = arena_reserved;
        row.idmap_reserved_bytes = idmap_reserved;
        if (telemetry)
        {
            row.raw_unique_ratio_x1000 = 1000 * totals.raw_landings
                / std::max<std::int64_t>(1, totals.unique_candidates);
        }
        return row;
    }
}
