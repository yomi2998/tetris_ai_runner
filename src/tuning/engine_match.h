#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <numeric>
#include <print>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tetris_core.h"
#include "tournament/scheduler.h"
#include "tuning/domain.h"
#include "tuning/match.h"

namespace tuning
{
    struct EngineViewState
    {
        std::atomic<bool> enabled{false};
        std::mutex mutex;
    };

    struct GameScenario
    {
        std::deque<char> pieces;
        std::uint64_t pair_seed = 0;
        int round = 0;
        int packet_index = 0;
    };

    inline GameScenario make_game_scenario(std::uint64_t seed, std::size_t max_rounds, std::size_t next_len)
    {
        std::mt19937 rng(static_cast<unsigned>(mix64(seed)));
        std::string bag = "IJLOSTZ";
        GameScenario scenario;
        std::size_t const pieces_needed = max_rounds * 2 + next_len * 2 + 4;
        while (scenario.pieces.size() < pieces_needed)
        {
            std::shuffle(bag.begin(), bag.end(), rng);
            for (char c : bag)
            {
                scenario.pieces.push_back(c);
            }
        }
        scenario.pair_seed = seed;
        return scenario;
    }

    inline void begin_game_round(GameScenario& a, GameScenario& b, int round)
    {
        a.round = round;
        b.round = round;
        a.packet_index = 0;
        b.packet_index = 0;
    }

    inline int game_scenario_hole(GameScenario const& scenario)
    {
        std::uint64_t const hash = mix64(scenario.pair_seed
            ^ mix64(static_cast<std::uint64_t>(scenario.round) * 0x9E3779B97F4A7C15ULL
                    + static_cast<std::uint64_t>(scenario.packet_index) * 0x94D049BB133111EBULL));
        return static_cast<int>(hash % 10);
    }

    inline int combo_attack(std::span<int const> table, int combo)
    {
        if (table.empty())
        {
            return 0;
        }
        std::size_t const index = static_cast<std::size_t>(std::max(0, combo));
        return table[std::min(table.size() - 1, index)];
    }

    template<class Adapter>
    struct EngineBotState
    {
        typename Adapter::instance_type instance;
        GameScenario scenario;
        m_tetris::TetrisMap map;
        std::vector<char> next;
        std::deque<int> recv_attack;
        int send_attack = 0;
        int combo = 0;
        bool b2b = false;
        char hold = ' ';
        bool dead = false;
        int last_clear = 0;
        int total_block = 0;
        int total_clear = 0;
        int total_attack = 0;
        int total_receive = 0;

        EngineBotState(std::uint64_t seed, std::size_t max_rounds, std::size_t next_len,
                       std::shared_ptr<m_tetris::TetrisContext> const& context)
            : instance(Adapter::make_instance(context))
            , scenario(make_game_scenario(seed, max_rounds, next_len))
            , map(10, 40)
        {
            next.reserve(next_len + 1);
        }
    };

    template<class Adapter>
    inline void engine_prepare_side(EngineBotState<Adapter>& bot, std::size_t next_len)
    {
        if (!bot.next.empty())
        {
            bot.next.erase(bot.next.begin());
        }
        while (bot.next.size() <= next_len)
        {
            bot.next.push_back(bot.scenario.pieces.front());
            bot.scenario.pieces.pop_front();
        }
    }

    template<class Adapter>
    inline void engine_run_side(EngineBotState<Adapter>& bot, m_tetris::TetrisContext const& shared_context,
                                std::size_t next_len, m_tetris::SearchBudget budget,
                                std::span<int const> combo_table)
    {
        char const current = bot.next.front();
        tuning::MoveRequest request;
        request.map = &bot.map;
        request.current = current;
        request.hold = bot.hold;
        request.hold_free = true;
        request.next = bot.next.data() + 1;
        request.next_length = next_len;
        request.last_clear = bot.last_clear;
        request.combo = bot.combo;
        request.b2b = bot.b2b;
        request.under_attack = std::accumulate(bot.recv_attack.begin(), bot.recv_attack.end(), 0);

        tuning::MoveOutcome const outcome = bot.instance.run_move(request, budget);
        if (outcome.dead)
        {
            bot.dead = true;
            return;
        }
        if (outcome.change_hold)
        {
            if (bot.hold == ' ')
            {
                bot.next.erase(bot.next.begin());
            }
            bot.hold = current;
        }

        int const clear = outcome.clear;
        bot.total_clear += clear;
        bot.last_clear = clear;

        int attack = 0;
        switch (clear)
        {
        case 0:
            bot.combo = 0;
            break;
        case 1:
            if (outcome.spin == tuning::Spin::Mini)
            {
                attack += 1 + static_cast<int>(bot.b2b);
                bot.b2b = true;
            }
            else if (outcome.spin == tuning::Spin::Full)
            {
                attack += 2 + static_cast<int>(bot.b2b);
                bot.b2b = true;
            }
            else
            {
                bot.b2b = false;
            }
            attack += combo_attack(combo_table, ++bot.combo);
            break;
        case 2:
            if (outcome.spin != tuning::Spin::None)
            {
                attack += 4 + static_cast<int>(bot.b2b);
                bot.b2b = true;
            }
            else
            {
                attack += 1;
                bot.b2b = false;
            }
            attack += combo_attack(combo_table, ++bot.combo);
            break;
        case 3:
            if (outcome.spin != tuning::Spin::None)
            {
                attack += 6 + static_cast<int>(bot.b2b) * 2;
                bot.b2b = true;
            }
            else
            {
                attack += 2;
                bot.b2b = false;
            }
            attack += combo_attack(combo_table, ++bot.combo);
            break;
        case 4:
            attack += combo_attack(combo_table, ++bot.combo) + 4 + static_cast<int>(bot.b2b);
            bot.b2b = true;
            break;
        default:
            break;
        }
        if (bot.map.count == 0)
        {
            attack += 6;
        }

        ++bot.total_block;
        bot.total_attack += attack;
        bot.send_attack = attack;

        while (!bot.recv_attack.empty())
        {
            if (bot.send_attack > 0)
            {
                if (bot.recv_attack.front() <= bot.send_attack)
                {
                    bot.send_attack -= bot.recv_attack.front();
                    bot.recv_attack.pop_front();
                    continue;
                }
                bot.recv_attack.front() -= bot.send_attack;
                bot.send_attack = 0;
            }
            if (bot.send_attack > 0 || bot.combo > 0)
            {
                break;
            }
            int const line = bot.recv_attack.front();
            bot.total_receive += line;
            bot.recv_attack.pop_front();
            for (int y = bot.map.height - 1; y >= line; --y)
            {
                bot.map.row[y] = bot.map.row[y - line];
            }
            std::uint32_t const hole = 1u << game_scenario_hole(bot.scenario);
            ++bot.scenario.packet_index;
            std::uint32_t const garbage_row = shared_context.full() & ~hole;
            for (int y = 0; y < line; ++y)
            {
                bot.map.row[y] = garbage_row;
            }
            bot.map.count = 0;
            bot.map.roof = 0;
            for (int my = 0; my < bot.map.height; ++my)
            {
                for (int mx = 0; mx < bot.map.width; ++mx)
                {
                    if (bot.map.full(mx, my))
                    {
                        bot.map.top[mx] = bot.map.roof = my + 1;
                        ++bot.map.count;
                    }
                }
            }
        }
    }

    template<class Adapter>
    inline void render_engine_sides(EngineBotState<Adapter> const& a, EngineBotState<Adapter> const& b,
                                    char const* name_a, char const* name_b)
    {
        std::string upcoming_a(a.next.size() > 1 ? a.next.begin() + 1 : a.next.end(), a.next.end());
        std::string upcoming_b(b.next.size() > 1 ? b.next.begin() + 1 : b.next.end(), b.next.end());
        int up_a = std::accumulate(a.recv_attack.begin(), a.recv_attack.end(), 0);
        int up_b = std::accumulate(b.recv_attack.begin(), b.recv_attack.end(), 0);
        double apl_a = a.total_clear > 0 ? static_cast<double>(a.total_attack) / a.total_clear : 0.0;
        double apl_b = b.total_clear > 0 ? static_cast<double>(b.total_attack) / b.total_clear : 0.0;
        double app_a = a.total_block > 0 ? static_cast<double>(a.total_attack) / a.total_block : 0.0;
        double app_b = b.total_block > 0 ? static_cast<double>(b.total_attack) / b.total_block : 0.0;
        std::print("\x1b[H\x1b[2J");
        std::println(
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {}\n"
            "HOLD={} NEXT={} COMBO={} B2B={} UP={} P={} L={} A={} APL={:.2f} APP={:.2f} {}",
            a.hold, upcoming_a, a.combo, a.b2b ? 1 : 0, up_a, a.total_block, a.total_clear,
            a.total_attack, apl_a, app_a, name_a,
            b.hold, upcoming_b, b.combo, b.b2b ? 1 : 0, up_b, b.total_block, b.total_clear,
            b.total_attack, apl_b, app_b, name_b);
        int width = std::min(a.map.width, b.map.width);
        for (int y = 21; y >= 0; --y)
        {
            for (int x = 0; x < width; ++x)
            {
                std::print("{}", a.map.full(x, y) ? "[]" : "  ");
            }
            std::print("  ");
            for (int x = 0; x < width; ++x)
            {
                std::print("{}", b.map.full(x, y) ? "[]" : "  ");
            }
            std::println("");
        }
        std::fflush(stdout);
    }

    template<class Adapter>
    inline int engine_play_game(tuning::BatchGame const& game, tuning::RunConfig const& config,
                                std::shared_ptr<m_tetris::TetrisContext> const& context,
                                tournament_scheduler::RunPair const& run_pair,
                                tuning::GameOutcome& out,
                                EngineViewState* view_state = nullptr, bool viewed = false)
    {
        if (!context)
        {
            throw std::invalid_argument("engine match backend requires a prepared shared context");
        }
        if (!tuning::valid_run_config(config))
        {
            throw std::invalid_argument("invalid run config");
        }
        if (!tuning::validate_theta(Adapter::schema(), game.theta_a)
            || !tuning::validate_theta(Adapter::schema(), game.theta_b))
        {
            throw std::invalid_argument("invalid theta for game");
        }

        std::size_t const next_len = Adapter::next_length();
        std::span<int const> const combo_table = Adapter::combo_table();
        m_tetris::SearchBudget const budget = m_tetris::SearchBudget::by_iterations(config.iterations_per_move);
        std::size_t const max_rounds = static_cast<std::size_t>(config.max_rounds);

        EngineBotState<Adapter> a(game.seed_a, max_rounds, next_len, context);
        EngineBotState<Adapter> b(game.seed_b, max_rounds, next_len, context);
        if (!a.instance.apply_theta(game.theta_a.data(), game.theta_a.size())
            || !b.instance.apply_theta(game.theta_b.data(), game.theta_b.size()))
        {
            throw std::runtime_error("engine instance rejected the candidate theta");
        }

        int played_rounds = 0;
        for (int round = 1; round <= config.max_rounds; ++round)
        {
            begin_game_round(a.scenario, b.scenario, round);
            engine_prepare_side(a, next_len);
            engine_prepare_side(b, next_len);
            auto leaf_a = [&]()
            {
                engine_run_side(a, *context, next_len, budget, combo_table);
            };
            auto leaf_b = [&]()
            {
                engine_run_side(b, *context, next_len, budget, combo_table);
            };
            run_pair(leaf_a, leaf_b);
            ++played_rounds;
            if (viewed && view_state != nullptr
                && view_state->enabled.load(std::memory_order_relaxed))
            {
                std::lock_guard<std::mutex> lock(view_state->mutex);
                if (view_state->enabled.load(std::memory_order_relaxed))
                {
                    render_engine_sides<Adapter>(a, b, "A", "B");
                }
            }
            if (a.dead || b.dead)
            {
                break;
            }
            int const cancelled = std::min(a.send_attack, b.send_attack);
            a.send_attack -= cancelled;
            b.send_attack -= cancelled;
            if (b.send_attack > 0)
            {
                a.recv_attack.push_back(b.send_attack);
            }
            if (a.send_attack > 0)
            {
                b.recv_attack.push_back(a.send_attack);
            }
        }

        out.id = game.id;
        out.dead_a = a.dead;
        out.dead_b = b.dead;
        out.capped = !a.dead && !b.dead;
        out.rounds = played_rounds;
        out.app_a = a.total_block > 0 ? static_cast<double>(a.total_attack) / a.total_block : 0.0;
        out.app_b = b.total_block > 0 ? static_cast<double>(b.total_attack) / b.total_block : 0.0;
        out.apl_a = a.total_clear > 0 ? static_cast<double>(a.total_attack) / a.total_clear : 0.0;
        out.apl_b = b.total_clear > 0 ? static_cast<double>(b.total_attack) / b.total_clear : 0.0;
        if (a.dead && !b.dead)
        {
            out.winner = -1;
            out.reason = tuning::WinReason::BSurvivor;
        }
        else if (b.dead && !a.dead)
        {
            out.winner = 1;
            out.reason = tuning::WinReason::ASurvivor;
        }
        else if (out.apl_a > out.apl_b)
        {
            out.winner = 1;
            out.reason = out.capped ? tuning::WinReason::ACapApl : tuning::WinReason::ABothDeadApl;
        }
        else if (out.apl_b > out.apl_a)
        {
            out.winner = -1;
            out.reason = out.capped ? tuning::WinReason::BCapApl : tuning::WinReason::BBothDeadApl;
        }
        else
        {
            out.winner = 0;
            out.reason = out.capped ? tuning::WinReason::CapDraw : tuning::WinReason::BothDeadDraw;
        }
        return out.winner;
    }

    template<class Adapter>
    class EngineMatchBackend
    {
    public:
        using adapter_type = Adapter;

        EngineMatchBackend() = default;

        explicit EngineMatchBackend(std::shared_ptr<m_tetris::TetrisContext> context)
            : shared_context_(std::move(context))
        {
        }

        tuning::ParamSchema schema() const
        {
            return Adapter::schema();
        }

        bool validate(std::vector<double> const& theta) const
        {
            return tuning::validate_theta(Adapter::schema(), theta);
        }

        void set_view_state(std::shared_ptr<EngineViewState> view_state)
        {
            view_state_ = std::move(view_state);
        }

        tournament_scheduler::GameJob make_game_job(tuning::BatchGame const& game, tuning::RunConfig const& config,
                                                    std::shared_ptr<m_tetris::TetrisContext> context,
                                                    tuning::GameOutcome& out,
                                                    EngineViewState* view_state = nullptr,
                                                    bool viewed = false) const
        {
            tournament_scheduler::GameJob job;
            job.game_id = game.id;
            job.body = [&game, &out, context = std::move(context),
                        config, view_state, viewed](tournament_scheduler::RunPair const& run_pair) -> int
            {
                return tuning::engine_play_game<Adapter>(game, config, context, run_pair, out,
                                                         view_state, viewed);
            };
            return job;
        }

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const& games,
                                                   tuning::RunConfig const& config) const
        {
            if (!tuning::valid_run_config(config))
            {
                throw std::invalid_argument("invalid run config");
            }
            if (games.empty())
            {
                return {};
            }
            for (std::size_t i = 0; i < games.size(); ++i)
            {
                if (!validate(games[i].theta_a) || !validate(games[i].theta_b))
                {
                    throw std::invalid_argument("invalid theta for game index " + std::to_string(i));
                }
            }
            std::shared_ptr<m_tetris::TetrisContext> context = shared_context_
                ? shared_context_
                : Adapter::make_shared_context();
            if (!context)
            {
                throw std::runtime_error("adapter failed to prepare a shared context");
            }

            std::vector<tuning::GameOutcome> results(games.size());
            std::vector<tournament_scheduler::GameJob> jobs(games.size());
            bool const viewing = view_state_ != nullptr
                && view_state_->enabled.load(std::memory_order_relaxed);
            for (std::size_t i = 0; i < games.size(); ++i)
            {
                results[i].id = games[i].id;
                jobs[i] = make_game_job(games[i], config, context, results[i],
                                        view_state_.get(), viewing && i == 0);
            }

            tournament_scheduler::ExecutorConfig executor_config;
            executor_config.game_workers = std::min<int>(config.threads, static_cast<int>(games.size()));
            tournament_scheduler::GameExecutor executor(executor_config);
            std::vector<int> const outcomes = executor.run(std::move(jobs));
            for (std::size_t i = 0; i < outcomes.size(); ++i)
            {
                if (outcomes[i] != results[i].winner)
                {
                    throw std::runtime_error("game " + std::to_string(games[i].id)
                        + " failed or was abandoned by the executor");
                }
            }
            return results;
        }

    private:
        std::shared_ptr<m_tetris::TetrisContext> shared_context_;
        std::shared_ptr<EngineViewState> view_state_;
    };
}
