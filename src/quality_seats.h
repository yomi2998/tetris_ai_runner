#pragma once

#include "tetris_engine.h"
#include "toj_policy.h"
#include "toj_rule.h"
#include "tuner_match.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace quality_harness
{
    using tuner_match::Scenario;
    using tuner_match::next_length;
    using tuner_match::combo_table;
    using tuner_match::combo_table_max;
    using tuner_match::splitmix64;

    inline constexpr int engine_legacy = 0;
    inline constexpr int engine_value = 1;

    inline constexpr std::size_t queue_depth = static_cast<std::size_t>(next_length);
    inline constexpr std::size_t window_size = queue_depth + 1;

    inline char const *engine_name(int id)
    {
        return id == engine_value ? "value" : "legacy";
    }

    struct SeatWallSample
    {
        double move_wall_ms = 0.0;
    };

    class ValueSeat
    {
    public:
        ValueSeat()
        {
            policy_config_.combo_table = combo_table;
            policy_config_.combo_table_max = combo_table_max;
            policy_config_.safe = 0;
            policy_config_.parameters = toj_policy::Parameters::production_defaults();
            policy_.init(&policy_config_);
            engine_config_.policy = &policy_config_;
            engine_config_.movement.allow_180 = true;
            engine_config_.telemetry_enabled = true;
            engine_config_.timers_enabled = false;
            if (!engine_.init(engine_config_))
            {
                init_failed_ = true;
            }
        }

        void init(double const *theta)
        {
            toj_policy::Parameters::from_theta(theta, policy_config_.parameters);
            board_ = tetris::Board{};
            hold_piece_.reset();
            hold = ' ';
            next.clear();
            recv_attack.clear();
            send_attack = 0;
            combo = 0;
            b2b = 0;
            dead = false;
            total_block = 0;
            total_clear = 0;
            total_attack = 0;
            total_receive = 0;
            tspin_mini = 0;
            tspin_single = 0;
            tspin_double = 0;
            tspin_triple = 0;
            perfect_clears = 0;
            replay_failures = 0;
            arena_exhaustions = 0;
            deaths_spawn = 0;
            deaths_lockout = 0;
            deaths_invalid = 0;
            last_clear = 0;
            consumed_digest_ = 1469598103934665603ull;
            move_wall_ms_.clear();
        }

        void set_budget(int iters, int ms)
        {
            budget_iters_ = iters > 0 ? static_cast<std::uint64_t>(iters) : 0;
            budget_ms_ = iters > 0 ? 0 : ms;
        }

        void prepare()
        {
            if (!next.empty())
            {
                mix_consumed(static_cast<unsigned char>(next.front()));
                next.erase(next.begin());
            }
            while (next.size() < window_size)
            {
                next.push_back(scenario->pieces.front());
                scenario->pieces.pop_front();
            }
        }

        void run()
        {
            auto t0 = std::chrono::steady_clock::now();
            if (init_failed_)
            {
                dead = true;
                ++deaths_invalid;
                record_wall(t0);
                return;
            }

            char current = next.front();
            auto current_piece = tetris::try_from_char(current);
            if (!current_piece.has_value())
            {
                dead = true;
                ++deaths_invalid;
                record_wall(t0);
                return;
            }

            policy_config_.safe = policy_.safe_margin(board_, *current_piece);
            toj_policy::Evaluation seed_eval = policy_.evaluate(board_);
            toj_policy::State root_state;
            root_state.combo = static_cast<std::int8_t>(combo);
            root_state.b2b = static_cast<std::int8_t>(b2b != 0 ? 1 : 0);
            root_state.t2_value = seed_eval.t2_value;
            root_state.t3_value = seed_eval.t3_value;

            tetris_engine::Queue queue;
            for (std::size_t i = 0; i <= queue_depth; ++i)
            {
                auto piece = tetris::try_from_char(next[i]);
                if (!piece.has_value())
                {
                    dead = true;
                    ++deaths_invalid;
                    record_wall(t0);
                    return;
                }
                queue.pieces.push_back(*piece);
                queue.boundary.push_back(false);
            }

            tetris::Placement const spawn = tetris::Placement::unchecked(
                tetris::toj::spawn_x, tetris::toj::spawn_y, 0);
            if (!tetris::toj::fits(*current_piece, spawn, board_))
            {
                dead = true;
                ++deaths_spawn;
                record_wall(t0);
                return;
            }

            tetris_engine::HoldState hold_state;
            hold_state.piece = hold_piece_;
            hold_state.locked = false;

            tetris_engine::NodeId root = engine_.set_root(board_, root_state, queue, hold_state);
            if (root == tetris_engine::no_node)
            {
                dead = true;
                ++deaths_invalid;
                record_wall(t0);
                return;
            }
            engine_.run(budget());
            tetris_engine::FinalResult result = engine_.finalize(spawn);
            if (engine_.arena_exhausted())
            {
                ++arena_exhaustions;
                dead = true;
                record_wall(t0);
                return;
            }
            if (!result.has_selection || !result.path_ok)
            {
                ++replay_failures;
                dead = true;
                ++deaths_invalid;
                record_wall(t0);
                return;
            }

            if (result.used_hold)
            {
                if (hold == ' ')
                {
                    next.erase(next.begin());
                }
                hold = current;
                hold_piece_ = *current_piece;
            }

            if (toj_policy::Policy::is_lockout(result.played, result.candidate->placement))
            {
                dead = true;
                ++deaths_lockout;
                record_wall(t0);
                return;
            }

            std::optional<tetris::toj::RuleResult> applied =
                tetris::toj::apply(board_, result.played, *result.candidate);
            if (!applied.has_value())
            {
                ++replay_failures;
                dead = true;
                ++deaths_invalid;
                record_wall(t0);
                return;
            }

            int clear = applied->clear_count;
            total_clear += clear;
            last_clear = clear;

            int attack = 0;
            auto const spin = applied->spin;
            auto get_combo_attack = [](int c)
            {
                return combo_table[std::min(combo_table_max - 1, c)];
            };
            switch (clear)
            {
            case 0:
                combo = 0;
                break;
            case 1:
                if (spin == tetris::toj::SpinType::Mini)
                {
                    attack += 1 + b2b;
                    b2b = 1;
                    ++tspin_mini;
                }
                else if (spin == tetris::toj::SpinType::Full)
                {
                    attack += 2 + b2b;
                    b2b = 1;
                    ++tspin_single;
                }
                else
                {
                    b2b = 0;
                }
                attack += get_combo_attack(++combo);
                break;
            case 2:
                if (spin != tetris::toj::SpinType::None)
                {
                    attack += 4 + b2b;
                    b2b = 1;
                    ++tspin_double;
                }
                else
                {
                    attack += 1;
                    b2b = 0;
                }
                attack += get_combo_attack(++combo);
                break;
            case 3:
                if (spin != tetris::toj::SpinType::None)
                {
                    attack += 6 + b2b * 2;
                    b2b = 1;
                    ++tspin_triple;
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
            default:
                break;
            }
            if (applied->perfect_clear)
            {
                attack += 6;
                ++perfect_clears;
            }

            ++total_block;
            total_attack += attack;
            send_attack = attack;
            board_ = applied->board;

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
                std::uint32_t hole = 1u << tuner_match::scenario_hole(*scenario);
                ++scenario->packet_index;
                std::uint32_t garbage_row = full_row_mask & ~hole;
                board_.add_garbage(line, static_cast<std::uint16_t>(garbage_row));
            }
            record_wall(t0);
        }

        void under_attack(int line)
        {
            if (line > 0)
            {
                recv_attack.emplace_back(line);
            }
        }

        std::uint64_t consumed_digest() const
        {
            return consumed_digest_;
        }

        std::vector<double> const &move_wall_ms() const
        {
            return move_wall_ms_;
        }

        Scenario *scenario = nullptr;
        std::deque<int> recv_attack;
        std::vector<char> next;
        int send_attack = 0;
        int combo = 0;
        int b2b = 0;
        char hold = ' ';
        bool dead = false;
        int total_block = 0;
        int total_clear = 0;
        int total_attack = 0;
        int total_receive = 0;
        int tspin_mini = 0;
        int tspin_single = 0;
        int tspin_double = 0;
        int tspin_triple = 0;
        int perfect_clears = 0;
        int last_clear = 0;
        std::size_t replay_failures = 0;
        std::size_t arena_exhaustions = 0;
        std::size_t deaths_spawn = 0;
        std::size_t deaths_lockout = 0;
        std::size_t deaths_invalid = 0;

    private:
        tetris_engine::SearchBudget budget() const
        {
            return budget_iters_ > 0
                ? tetris_engine::SearchBudget::by_iterations(budget_iters_)
                : tetris_engine::SearchBudget::by_time(
                    static_cast<std::uint64_t>(budget_ms_));
        }

        void record_wall(std::chrono::steady_clock::time_point t0)
        {
            std::chrono::duration<double, std::milli> const dt
                = std::chrono::steady_clock::now() - t0;
            move_wall_ms_.push_back(dt.count());
        }

        void mix_consumed(unsigned char c)
        {
            consumed_digest_ ^= static_cast<std::uint64_t>(c);
            consumed_digest_ *= 1099511628211ull;
        }

        toj_policy::Config policy_config_{};
        toj_policy::Policy policy_;
        tetris_engine::EngineConfig engine_config_{};
        tetris_engine::Engine engine_;
        tetris::Board board_{};
        std::optional<tetris::Piece> hold_piece_;
        std::vector<double> move_wall_ms_;
        std::uint64_t budget_iters_ = 0;
        int budget_ms_ = 20;
        std::uint64_t consumed_digest_ = 1469598103934665603ull;
        bool init_failed_ = false;

        static constexpr std::uint32_t full_row_mask = (1u << 10) - 1;
    };

    template <typename S1, typename S2>
    inline void run_round_seats(S1 &b1, S2 &b2)
    {
        b1.run();
        b2.run();
    }

    template <typename S1, typename S2>
    inline void run_round_parallel_seats(S1 &b1, S2 &b2, std::counting_semaphore<> &permits)
    {
        if (permits.try_acquire())
        {
            std::thread helper([&b2] { b2.run(); });
            b1.run();
            helper.join();
            permits.release();
        }
        else
        {
            b1.run();
            b2.run();
        }
    }

    template <typename S1, typename S2>
    inline tuner_match::MatchResult play_match_seats(S1 &b1, S2 &b2, int max_rounds,
        std::counting_semaphore<> *bot_permits = nullptr)
    {
        int played_rounds = 0;
        if (bot_permits != nullptr)
        {
            bot_permits->acquire();
        }
        for (int round = 1; round <= max_rounds; ++round)
        {
            tuner_match::begin_round(*b1.scenario, *b2.scenario, round);
            b1.prepare();
            b2.prepare();
            if (bot_permits != nullptr)
            {
                run_round_parallel_seats(b1, b2, *bot_permits);
            }
            else
            {
                run_round_seats(b1, b2);
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
        tuner_match::MatchResult r;
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
            r.winner_reason = tuner_match::MatchResult::P2_SURVIVOR;
        }
        else if (b2.dead && !b1.dead)
        {
            r.winner = +1;
            r.winner_reason = tuner_match::MatchResult::P1_SURVIVOR;
        }
        else if (r.apl1 > r.apl2)
        {
            r.winner = +1;
            r.winner_reason = r.capped ? tuner_match::MatchResult::P1_CAP_APL
                                      : tuner_match::MatchResult::P1_BOTH_DEAD_APL;
        }
        else if (r.apl2 > r.apl1)
        {
            r.winner = -1;
            r.winner_reason = r.capped ? tuner_match::MatchResult::P2_CAP_APL
                                      : tuner_match::MatchResult::P2_BOTH_DEAD_APL;
        }
        else
        {
            r.winner = 0;
            r.winner_reason = r.capped ? tuner_match::MatchResult::CAP_DRAW
                                      : tuner_match::MatchResult::BOTH_DEAD_DRAW;
        }
        return r;
    }
}
