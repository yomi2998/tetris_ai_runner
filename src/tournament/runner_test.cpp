#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <print>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "tournament/runner.h"
#include "tournament/scheduler.h"

namespace tr = tournament_runner;
namespace tb = tournament_bracket;
namespace ts = tournament_scheduler;
namespace tu = tuning;

using tr::CandidateId;

namespace
{
    int failures = 0;
    int checks = 0;

    void check(bool ok, std::string_view name)
    {
        ++checks;
        std::println("{} {}", ok ? "PASS" : "FAIL", name);
        if (!ok)
        {
            ++failures;
        }
    }

    using WinnerFn = std::function<int(int, int)>;

    struct Script
    {
        std::map<std::pair<int, int>, int> exact;
        std::map<int, WinnerFn> per_series;
        WinnerFn fallback;
    };

    struct CallLog
    {
        std::vector<std::vector<std::uint64_t>> call_ids;
        std::vector<std::pair<std::uint64_t, std::vector<double>>> player_one_thetas;
        std::vector<tu::RunConfig> configs;

        std::set<std::uint64_t> all_ids() const
        {
            std::set<std::uint64_t> ids;
            for (auto const &call : call_ids)
            {
                ids.insert(call.begin(), call.end());
            }
            return ids;
        }
    };

    enum class Fault
    {
        None,
        DropLast,
        WrongId,
        InvalidWinner,
        NegativeRounds,
        Reversed,
        InconsistentReason,
        Throw,
    };

    tu::ParamSchema fake_schema()
    {
        static char const *const names[] = { "alpha", "beta", "gamma" };
        static double const scales[] = { 1.0, 2.0, 4.0 };
        static double const defaults[] = { 0.5, -1.25, 8.0 };
        return tu::ParamSchema{
            .adapter_id = "fake_tournament",
            .names = names,
            .scales = scales,
            .defaults = defaults,
        };
    }

    int pseudo_winner(int series, int index)
    {
        return tu::mix64(tr::game_id_for(series, index) ^ 0x51D5EEDBULL) % 2 == 0 ? 1 : -1;
    }

    int script_winner(Script const &script, int series, int index)
    {
        if (auto it = script.exact.find({series, index}); it != script.exact.end())
        {
            return it->second;
        }
        if (auto it = script.per_series.find(series); it != script.per_series.end() && it->second)
        {
            return it->second(series, index);
        }
        if (script.fallback)
        {
            return script.fallback(series, index);
        }
        return pseudo_winner(series, index);
    }

    std::vector<tu::GameOutcome> assemble_outcomes(std::vector<tu::BatchGame> const &games, Script const &script)
    {
        std::vector<tu::GameOutcome> results;
        results.reserve(games.size());
        for (tu::BatchGame const &game : games)
        {
            int series = 0;
            int index = 0;
            tr::decode_game_id(game.id, series, index);
            int const winner = script_winner(script, series, index);
            tu::GameOutcome outcome;
            outcome.id = game.id;
            outcome.winner = winner;
            outcome.rounds = static_cast<int>(tu::mix64(game.id ^ 0xABCDEFULL) % 13u) + 1;
            outcome.reason = winner > 0 ? tu::WinReason::ASurvivor
                : winner < 0 ? tu::WinReason::BSurvivor : tu::WinReason::CapDraw;
            results.push_back(outcome);
        }
        return results;
    }

    struct ScriptedBackend
    {
        std::shared_ptr<Script> script = std::make_shared<Script>();
        std::shared_ptr<CallLog> log = std::make_shared<CallLog>();
        std::shared_ptr<Fault> fault = std::make_shared<Fault>(Fault::None);

        tu::ParamSchema schema() const
        {
            return fake_schema();
        }

        bool validate(std::vector<double> const &theta) const
        {
            return theta.size() == 3 && tu::theta_finite(theta.data(), theta.size());
        }

        std::vector<tu::GameOutcome> run_games(std::vector<tu::BatchGame> const &games, tu::RunConfig config) const
        {
            if (*fault == Fault::Throw)
            {
                throw std::runtime_error("backend exploded");
            }
            if (games.empty() || !tu::valid_run_config(config))
            {
                return {};
            }
            log->configs.push_back(config);
            std::vector<std::uint64_t> ids;
            ids.reserve(games.size());
            for (tu::BatchGame const &game : games)
            {
                ids.push_back(game.id);
                log->player_one_thetas.emplace_back(game.id, game.theta_a);
            }
            log->call_ids.push_back(std::move(ids));
            std::vector<tu::GameOutcome> results = assemble_outcomes(games, *script);
            switch (*fault)
            {
            case Fault::DropLast:
                results.pop_back();
                break;
            case Fault::WrongId:
                results.front().id = results.front().id + 1;
                break;
            case Fault::InvalidWinner:
                results.front().winner = 7;
                break;
            case Fault::NegativeRounds:
                results.front().rounds = -1;
                break;
            case Fault::Reversed:
                std::reverse(results.begin(), results.end());
                break;
            case Fault::InconsistentReason:
                results.front().winner = 1;
                results.front().reason = tu::WinReason::CapDraw;
                break;
            default:
                break;
            }
            return results;
        }
    };

    static_assert(tu::MatchBackend<ScriptedBackend>);

    struct ExecutorBackend
    {
        std::shared_ptr<Script> script = std::make_shared<Script>();

        tu::ParamSchema schema() const
        {
            return fake_schema();
        }

        bool validate(std::vector<double> const &theta) const
        {
            return theta.size() == 3 && tu::theta_finite(theta.data(), theta.size());
        }

        std::vector<tu::GameOutcome> run_games(std::vector<tu::BatchGame> const &games, tu::RunConfig config) const
        {
            if (games.empty() || !tu::valid_run_config(config))
            {
                return {};
            }
            std::vector<tu::GameOutcome> results;
            results.reserve(games.size());
            ts::GameExecutor executor{ts::ExecutorConfig{2, 4}};
            std::vector<ts::SeriesDemand> demands;
            demands.reserve(games.size());
            for (std::size_t i = 0; i < games.size(); ++i)
            {
                demands.push_back(ts::SeriesDemand{static_cast<std::uint64_t>(i), 1});
            }
            std::vector<ts::WaveSlot> const wave = ts::build_wave(demands, static_cast<int>(games.size()));
            Script const &shared_script = *script;
            auto const outcomes = ts::run_wave(executor, wave,
                                               [&](ts::WaveSlot const &slot) -> ts::GameJob
                                               {
                                                   tu::BatchGame const &game = games[static_cast<std::size_t>(slot.series_id)];
                                                   int series = 0;
                                                   int index = 0;
                                                   tr::decode_game_id(game.id, series, index);
                                                   int const winner = script_winner(shared_script, series, index);
                                                   return ts::GameJob{game.id,
                                                                      [winner](ts::RunPair const &run_pair) -> int
                                                                      {
                                                                          run_pair([] {}, [] {});
                                                                          return winner;
                                                                      }};
                                               });
            for (std::size_t i = 0; i < games.size() && i < outcomes.size(); ++i)
            {
                int const winner = outcomes[i];
                tu::GameOutcome outcome;
                outcome.id = games[i].id;
                outcome.winner = winner;
                outcome.rounds = static_cast<int>(tu::mix64(games[i].id ^ 0xABCDEFULL) % 13u) + 1;
                outcome.reason = winner > 0 ? tu::WinReason::ASurvivor
                    : winner < 0 ? tu::WinReason::BSurvivor : tu::WinReason::CapDraw;
                results.push_back(outcome);
            }
            return results;
        }
    };

    static_assert(tu::MatchBackend<ExecutorBackend>);

    using Runner = tr::TournamentRunner<ScriptedBackend>;

    struct RunnerHandle
    {
        Runner runner;
        std::shared_ptr<CallLog> log;
    };

    std::vector<tr::RosterEntry> make_roster(std::vector<CandidateId> const &ids)
    {
        std::vector<tr::RosterEntry> roster;
        roster.reserve(ids.size());
        for (CandidateId id : ids)
        {
            tr::RosterEntry entry;
            entry.id = id;
            entry.theta = {static_cast<double>(id), 0.0, 0.0};
            roster.push_back(std::move(entry));
        }
        return roster;
    }

    RunnerHandle make_runner(std::vector<tr::RosterEntry> roster, std::shared_ptr<Script> script,
                             std::uint64_t seed, tr::RunLimits limits,
                             tu::RunConfig config = {2, 500, 200},
                             std::vector<tr::GameRecord> prior = {},
                             Fault fault = Fault::None)
    {
        ScriptedBackend backend;
        backend.script = std::move(script);
        backend.fault = std::make_shared<Fault>(fault);
        auto log = backend.log;
        return RunnerHandle{Runner(std::move(backend), std::move(roster), seed, config, limits, std::move(prior)),
                            std::move(log)};
    }

    int always_a(int, int index)
    {
        return index % 2 == 0 ? 1 : -1;
    }

    int always_b(int, int index)
    {
        return index % 2 == 0 ? -1 : 1;
    }

    std::shared_ptr<Script> script_with_series(std::map<int, WinnerFn> per_series, WinnerFn fallback = nullptr)
    {
        auto script = std::make_shared<Script>();
        script->per_series = std::move(per_series);
        script->fallback = std::move(fallback);
        return script;
    }

    std::shared_ptr<Script> draw_every_17_script()
    {
        return script_with_series({}, [](int series, int index) -> int
        {
            if (index % 17 == 3)
            {
                return 0;
            }
            return pseudo_winner(series, index);
        });
    }

    struct Summary
    {
        std::uint64_t checksum = 0;
        std::vector<tr::GameRecord> canonical;
        CandidateId champion = tb::kNoCandidate;
        std::vector<CandidateId> standings;
        std::int64_t games = 0;
        std::int64_t draws = 0;
    };

    Summary summarize(Runner const &runner)
    {
        Summary summary;
        summary.checksum = runner.checksum();
        summary.canonical = tr::canonical_ledger(runner.ledger());
        summary.champion = runner.champion();
        summary.standings = runner.standings();
        summary.games = runner.total_games();
        summary.draws = runner.total_draws();
        return summary;
    }

    void check_two_loss_invariant(Runner const &runner, std::size_t entrants)
    {
        auto const standings = runner.standings();
        bool const size_ok = standings.size() == entrants;
        check(size_ok, "standings_cover_every_entrant");
        if (!size_ok)
        {
            return;
        }
        bool losses_ok = true;
        for (std::size_t i = 0; i < standings.size(); ++i)
        {
            int const losses = runner.bracket().losses(standings[i]);
            if (i == 0)
            {
                losses_ok = losses_ok && (losses == 0 || losses == 1);
            }
            else
            {
                losses_ok = losses_ok && losses == 2;
            }
        }
        check(losses_ok, "every_non_champion_has_exactly_two_losses");
    }

    void check_no_post_clinch(Runner const &runner)
    {
        std::map<int, tb::SeriesFormat> formats;
        for (int id = 0; id < runner.bracket().series_count(); ++id)
        {
            formats[id] = runner.bracket().series(id).format;
        }
        std::vector<tr::GameRecord> const canonical = tr::canonical_ledger(runner.ledger());
        std::map<int, std::pair<int, int>> wins;
        bool ok = true;
        for (tr::GameRecord const &record : canonical)
        {
            auto &score = wins[record.series_id];
            int const target = formats[record.series_id].first_to;
            if (std::max(score.first, score.second) >= target)
            {
                ok = false;
                break;
            }
            if (record.winner == tb::GameWinner::SideA)
            {
                ++score.first;
            }
            else if (record.winner == tb::GameWinner::SideB)
            {
                ++score.second;
            }
            if (score.first == target || score.second == target)
            {
                score = {0, 0};
            }
        }
        check(ok, "no_game_runs_after_a_possible_clinch");
    }

    void check_seat_alternation_and_mirroring(Runner const &runner)
    {
        std::vector<tr::GameRecord> const canonical = tr::canonical_ledger(runner.ledger());
        std::map<int, std::vector<tr::GameRecord const *>> by_series;
        for (tr::GameRecord const &record : canonical)
        {
            by_series[record.series_id].push_back(&record);
        }
        bool seats_ok = true;
        bool mirror_ok = true;
        bool distinct_ok = true;
        for (auto const &[id, records] : by_series)
        {
            (void)id;
            for (tr::GameRecord const *record : records)
            {
                seats_ok = seats_ok
                    && record->seat.side_a_is_player_one == (record->game_index % 2 == 0);
            }
            for (std::size_t p = 0; p + 1 < records.size(); p += 2)
            {
                tr::GameRecord const *first = records[p];
                tr::GameRecord const *second = records[p + 1];
                mirror_ok = mirror_ok && first->game_index % 2 == 0 && second->game_index % 2 == 1
                    && first->seed_player_one == second->seed_player_two
                    && first->seed_player_two == second->seed_player_one;
                if (p >= 2)
                {
                    distinct_ok = distinct_ok && first->seed_player_one != records[p - 2]->seed_player_one;
                }
            }
        }
        check(seats_ok, "seats_alternate_by_game_index");
        check(mirror_ok, "consecutive_games_mirror_scenario_seeds");
        check(distinct_ok, "mirror_blocks_use_distinct_scenarios");
    }

    void check_run_config_passthrough(RunnerHandle const &handle, tu::RunConfig const &config)
    {
        bool ok = !handle.log->configs.empty();
        for (tu::RunConfig const &logged : handle.log->configs)
        {
            ok = ok && logged.threads == config.threads
                && logged.iterations_per_move == config.iterations_per_move
                && logged.max_rounds == config.max_rounds;
        }
        check(ok, "run_config_reaches_backend_unchanged");
    }

    void run_helper_tests()
    {
        int series = 0;
        int index = 0;
        tr::decode_game_id(tr::game_id_for(41, 7), series, index);
        check(series == 41 && index == 7, "game_id_round_trips_series_and_index");
        bool unique = true;
        for (int s = 0; s < 6; ++s)
        {
            for (int i = 0; i < 6; ++i)
            {
                for (int s2 = 0; s2 < 6; ++s2)
                {
                    for (int i2 = 0; i2 < 6; ++i2)
                    {
                        if ((s != s2 || i != i2) && tr::game_id_for(s, i) == tr::game_id_for(s2, i2))
                        {
                            unique = false;
                        }
                    }
                }
            }
        }
        check(unique, "game_ids_are_unique_across_series_and_index");
        bool const parity = tr::side_a_is_player_one(0) && !tr::side_a_is_player_one(1)
            && tr::side_a_is_player_one(2) && !tr::side_a_is_player_one(3);
        check(parity, "seat_alternation_starts_with_side_a");
        bool mirror = true;
        bool distinct = true;
        for (int p = 0; p < 5; ++p)
        {
            auto const even = tr::player_seeds_for(999, 3, 2 * p);
            auto const odd = tr::player_seeds_for(999, 3, 2 * p + 1);
            mirror = mirror && even.first == odd.second && even.second == odd.first;
            if (p > 0)
            {
                auto const previous = tr::player_seeds_for(999, 3, 2 * p - 2);
                distinct = distinct && even.first != previous.first;
            }
        }
        check(mirror, "paired_games_swap_player_seeds");
        check(distinct, "seed_pairs_differ_across_mirrored_blocks");
        check(tr::normalize_outcome(1, true) == tb::GameWinner::SideA
                  && tr::normalize_outcome(1, false) == tb::GameWinner::SideB
                  && tr::normalize_outcome(-1, true) == tb::GameWinner::SideB
                  && tr::normalize_outcome(-1, false) == tb::GameWinner::SideA
                  && tr::normalize_outcome(0, true) == tb::GameWinner::Draw,
              "outcome_normalization_respects_seat_swaps");
        check(tr::normalize_reason(tu::WinReason::ASurvivor, false) == tu::WinReason::BSurvivor
                  && tr::normalize_reason(tu::WinReason::BCapApl, false) == tu::WinReason::ACapApl
                  && tr::normalize_reason(tu::WinReason::CapDraw, false) == tu::WinReason::CapDraw,
              "reason_normalization_swaps_sides");
    }

    void run_demand_window_tests()
    {
        tb::SeriesView view;
        view.id = 5;
        view.format = tb::SeriesFormat{1, 11};
        view.games_a = 9;
        view.games_b = 2;
        auto demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].series_id == 5 && demands[0].capacity == 2,
              "safe_demand_at_9_2_is_two_games");

        view.games_a = 10;
        view.games_b = 10;
        demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].capacity == 1, "safe_demand_at_10_10_is_one_game");

        view.format = tb::SeriesFormat{1, 7};
        view.games_a = 0;
        view.games_b = 0;
        demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].capacity == 7, "safe_demand_ft7_at_0_0_is_seven_games");

        view.format = tb::SeriesFormat{1, 11};
        demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].capacity == 11, "safe_demand_ft11_at_0_0_is_eleven_games");

        view.games_a = 2;
        view.games_b = 9;
        demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].capacity == 2, "safe_demand_tracks_leading_score");

        view.games_a = 11;
        view.games_b = 2;
        demands = tr::plan_demands({view});
        check(demands.empty(), "completed_set_demand_is_dropped");

        view.format = tb::SeriesFormat{3, 11};
        view.games_a = 9;
        view.games_b = 2;
        demands = tr::plan_demands({view});
        check(demands.size() == 1 && demands[0].capacity == 2, "bo5_set_scoped_demand_ignores_set_score");
    }

    void run_two_candidate_tests()
    {
        auto const roster = make_roster({101, 202});
        {
            auto handle = make_runner(roster, script_with_series({{0, always_a}, {1, always_a}, {2, always_a}}), 77, {});
            tr::RunResult const result = handle.runner.run();
            check(result.complete && handle.runner.champion() == 101, "two_candidate_straight_crowns_side_a");
            check(handle.runner.bracket().series(2).status == tb::SeriesStatus::Dormant,
                  "grand_final_without_reset_keeps_reset_dormant");
            check(handle.runner.total_games() == 55, "two_candidate_straight_game_count");
            check_two_loss_invariant(handle.runner, roster.size());
            check_no_post_clinch(handle.runner);
            check_seat_alternation_and_mirroring(handle.runner);
            check_run_config_passthrough(handle, {2, 500, 200});
        }
        {
            auto handle = make_runner(roster, script_with_series({{0, always_a}, {1, always_b}, {2, always_a}}), 77, {});
            tr::RunResult const result = handle.runner.run();
            check(result.complete && handle.runner.champion() == 101, "reset_series_won_by_bracket_winner");
            check(handle.runner.bracket().series(2).status == tb::SeriesStatus::Complete,
                  "reset_series_completes_when_triggered");
            check(handle.runner.bracket().losses(202) == 2, "reset_loser_accumulates_second_loss");
            check(handle.runner.total_games() == 88, "reset_path_game_count");
            check_two_loss_invariant(handle.runner, roster.size());
            check_no_post_clinch(handle.runner);
        }
        {
            auto handle = make_runner(roster, script_with_series({{0, always_a}, {1, always_b}, {2, always_b}}), 77, {});
            tr::RunResult const result = handle.runner.run();
            check(result.complete && handle.runner.champion() == 202, "reset_series_won_by_lower_bracket_finalist");
            check_two_loss_invariant(handle.runner, roster.size());
        }
    }

    void run_three_candidate_tests()
    {
        auto const roster = make_roster({7, 8, 9});
        auto handle = make_runner(roster, std::make_shared<Script>(), 31, {});
        tr::RunResult const result = handle.runner.run();
        check(result.complete && handle.runner.bracket().valid(), "three_candidate_tournament_completes");
        check(handle.runner.bracket().bye_count() == 1, "three_candidates_have_one_bye");
        int walkovers = 0;
        for (int id = 0; id < handle.runner.bracket().series_count(); ++id)
        {
            if (handle.runner.bracket().series(id).status == tb::SeriesStatus::Walkover)
            {
                ++walkovers;
            }
        }
        check(walkovers == 2, "bye_resolves_through_two_walkovers");
        bool no_walkover_games = true;
        for (tr::GameRecord const &record : handle.runner.ledger())
        {
            if (handle.runner.bracket().series(record.series_id).status == tb::SeriesStatus::Walkover)
            {
                no_walkover_games = false;
            }
        }
        check(no_walkover_games, "walkover_series_never_runs_games");
        check_two_loss_invariant(handle.runner, roster.size());
        check_no_post_clinch(handle.runner);
    }

    void run_eight_candidate_tests()
    {
        auto const roster = make_roster({1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007});
        auto script = draw_every_17_script();
        auto handle = make_runner(roster, script, 4242, {});
        tr::RunResult const result = handle.runner.run();
        check(result.complete, "eight_candidate_tournament_completes");
        check(handle.runner.total_draws() > 0, "draws_occur_and_award_no_ft_win");
        check_two_loss_invariant(handle.runner, roster.size());
        check_no_post_clinch(handle.runner);
        check_seat_alternation_and_mirroring(handle.runner);

        bool thetas_ok = true;
        for (auto const &[game_id, theta] : handle.log->player_one_thetas)
        {
            int series = 0;
            int index = 0;
            tr::decode_game_id(game_id, series, index);
            tb::SeriesView const view = handle.runner.bracket().series(series);
            CandidateId const expected = index % 2 == 0 ? view.side_a : view.side_b;
            thetas_ok = thetas_ok && theta.size() == 3
                && static_cast<CandidateId>(theta[0]) == expected;
        }
        check(thetas_ok, "player_one_theta_matches_bracket_seat");

        std::vector<std::pair<int, Summary>> summaries;
        for (int wave_limit : {0, 1, 3, 7})
        {
            tr::RunLimits limits;
            limits.wave_limit = wave_limit;
            auto limited = make_runner(roster, script, 4242, limits);
            tr::RunResult const limited_result = limited.runner.run();
            check(limited_result.complete, "limited_wave_tournament_completes");
            summaries.emplace_back(wave_limit, summarize(limited.runner));
        }
        Summary const &reference = summaries.front().second;
        bool same = true;
        for (auto const &[wave_limit, summary] : summaries)
        {
            (void)wave_limit;
            same = same && summary.checksum == reference.checksum && summary.canonical == reference.canonical
                && summary.champion == reference.champion && summary.standings == reference.standings
                && summary.games == reference.games && summary.draws == reference.draws;
        }
        check(same, "ledger_checksum_identical_across_wave_limits");
    }

    void run_thirty_candidate_tests()
    {
        std::vector<CandidateId> ids;
        for (CandidateId i = 0; i < 30; ++i)
        {
            ids.push_back(500 + i);
        }
        auto const roster = make_roster(ids);
        auto handle = make_runner(roster, std::make_shared<Script>(), 9001, {});
        tr::RunResult const result = handle.runner.run();
        check(result.complete, "thirty_candidate_tournament_completes");
        check(handle.runner.bracket().bye_count() == 2, "thirty_candidates_have_two_byes");
        check(handle.runner.total_games() > 400, "thirty_candidate_game_count_is_substantial");
        check_two_loss_invariant(handle.runner, roster.size());
        check_no_post_clinch(handle.runner);
        check_seat_alternation_and_mirroring(handle.runner);

        tr::RunLimits five;
        five.wave_limit = 5;
        auto limited = make_runner(roster, std::make_shared<Script>(), 9001, five);
        tr::RunResult const limited_result = limited.runner.run();
        check(limited_result.complete && summarize(limited.runner).checksum == summarize(handle.runner).checksum,
              "thirty_candidate_checksum_identical_across_wave_limits");
    }

    void run_nine_two_scheduling_test()
    {
        auto const roster = make_roster({101, 202});
        auto script = script_with_series({{0, always_a}, {2, always_a}},
                                         nullptr);
        script->per_series[1] = [](int, int index) -> int
        {
            return index < 9 ? always_a(0, index) : always_b(0, index);
        };
        auto handle = make_runner(roster, script, 55, tr::RunLimits{3, 0, 0});
        tr::RunResult const result = handle.runner.run();
        check(result.complete, "nine_two_tournament_completes");

        tb::SeriesView const gf = handle.runner.bracket().series(1);
        std::map<std::uint64_t, tr::GameRecord const *> by_id;
        for (tr::GameRecord const &record : handle.runner.ledger())
        {
            by_id[record.game_id] = &record;
        }
        int const target = gf.format.first_to;
        std::vector<std::pair<std::pair<int, int>, int>> observations;
        int wa = 0;
        int wb = 0;
        bool sizing_ok = true;
        for (auto const &call : handle.log->call_ids)
        {
            std::pair<int, int> const pre{wa, wb};
            int count = 0;
            for (std::uint64_t id : call)
            {
                int series = 0;
                int index = 0;
                tr::decode_game_id(id, series, index);
                if (series != 1)
                {
                    continue;
                }
                ++count;
                auto it = by_id.find(id);
                if (it == by_id.end())
                {
                    sizing_ok = false;
                    break;
                }
                if (it->second->winner == tb::GameWinner::SideA)
                {
                    ++wa;
                }
                else if (it->second->winner == tb::GameWinner::SideB)
                {
                    ++wb;
                }
                if (wa == target || wb == target)
                {
                    wa = 0;
                    wb = 0;
                }
            }
            int const capacity = target - std::max(pre.first, pre.second);
            if (count == 0)
            {
                continue;
            }
            sizing_ok = sizing_ok && count == std::min(3, capacity);
            observations.push_back({pre, count});
        }
        check(sizing_ok, "every_grand_final_wave_matches_safe_capacity");
        bool saw_9_0 = false;
        bool saw_9_2 = false;
        bool saw_cap_one = false;
        for (auto const &[pre, count] : observations)
        {
            if (pre.first == 9 && pre.second == 0 && count == 2)
            {
                saw_9_0 = true;
            }
            if (pre.first == 9 && pre.second == 2 && count == 2)
            {
                saw_9_2 = true;
            }
            if (pre.first == 9 && pre.second == 10 && count == 1)
            {
                saw_cap_one = true;
            }
        }
        check(saw_9_0, "capacity_clamps_below_wave_limit_at_9_0");
        check(saw_9_2, "safe_9_2_window_schedules_exactly_two_games");
        check(saw_cap_one, "capacity_of_one_schedules_single_game");
        check_no_post_clinch(handle.runner);
    }

    void run_malformed_backend_tests()
    {
        auto const roster = make_roster({101, 202});
        struct FaultCase
        {
            Fault fault;
            tr::ErrorCode code;
            std::string_view name;
        };
        std::vector<FaultCase> const cases{
            {Fault::DropLast, tr::ErrorCode::BackendIncomplete, "dropped_outcome_is_rejected"},
            {Fault::WrongId, tr::ErrorCode::BackendMalformedOutcome, "wrong_outcome_id_is_rejected"},
            {Fault::InvalidWinner, tr::ErrorCode::BackendMalformedOutcome, "invalid_winner_is_rejected"},
            {Fault::NegativeRounds, tr::ErrorCode::BackendMalformedOutcome, "negative_rounds_is_rejected"},
            {Fault::Reversed, tr::ErrorCode::BackendMalformedOutcome, "reversed_outcome_order_is_rejected"},
            {Fault::InconsistentReason, tr::ErrorCode::BackendMalformedOutcome, "inconsistent_reason_is_rejected"},
            {Fault::Throw, tr::ErrorCode::BackendFailed, "backend_exception_is_structured"},
        };
        for (FaultCase const &fault_case : cases)
        {
            auto handle = make_runner(roster, script_with_series({{0, always_a}, {1, always_a}, {2, always_a}}),
                                      77, {}, {2, 500, 200}, {}, fault_case.fault);
            tr::RunResult const result = handle.runner.run();
            check(!result.complete && result.error.code == fault_case.code && handle.runner.ledger().empty(),
                  fault_case.name);
            tr::RunResult const again = handle.runner.run();
            check(again.error.code == fault_case.code, "error_state_is_sticky");
        }
    }

    void run_limit_tests()
    {
        auto const roster = make_roster({1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007});
        {
            tr::RunLimits limits;
            limits.max_games = 5;
            auto handle = make_runner(roster, std::make_shared<Script>(), 4242, limits);
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::GameLimitExceeded && handle.runner.total_games() == 5
                      && !result.complete,
                  "game_limit_returns_structured_error");
            auto resumed = make_runner(roster, std::make_shared<Script>(), 4242, {}, {2, 500, 200},
                                       handle.runner.ledger());
            tr::RunResult const resumed_result = resumed.runner.run();
            check(resumed_result.complete, "run_stops_at_limit_then_resumes_to_completion");
        }
        {
            tr::RunLimits limits;
            limits.max_draws = 2;
            auto handle = make_runner(roster, draw_every_17_script(), 4242, limits);
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::DrawLimitExceeded && handle.runner.total_draws() == 4
                      && !result.complete,
                  "draw_limit_returns_structured_error");
        }
    }

    void run_resume_tests()
    {
        auto const roster = make_roster({1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007});
        auto script = draw_every_17_script();
        tr::RunLimits limits;
        limits.wave_limit = 3;
        auto full = make_runner(roster, script, 4242, limits);
        tr::RunResult const full_result = full.runner.run();
        check(full_result.complete, "resume_reference_run_completes");
        auto const &ledger = full.runner.ledger();
        std::vector<tr::GameRecord> const prefix(ledger.begin(), ledger.begin() + static_cast<std::ptrdiff_t>(ledger.size() / 2));

        auto resumed = make_runner(roster, script, 4242, limits, {2, 500, 200}, prefix);
        check(resumed.runner.ok(), "resume_accepts_valid_prefix_ledger");
        tr::RunResult const resumed_result = resumed.runner.run();
        check(resumed_result.complete, "resumed_run_completes");
        check(resumed.runner.ledger().size() == ledger.size(), "resumed_ledger_has_same_size");
        check(tr::canonical_ledger(resumed.runner.ledger()) == tr::canonical_ledger(ledger),
              "resumed_ledger_matches_full_run_byte_for_byte");
        check(resumed.runner.checksum() == full.runner.checksum(), "resumed_checksum_matches");
        check(resumed.runner.champion() == full.runner.champion(), "resumed_champion_matches");
        std::set<std::uint64_t> const replayed = resumed.log->all_ids();
        bool no_rerun = true;
        for (tr::GameRecord const &record : prefix)
        {
            no_rerun = no_rerun && replayed.find(record.game_id) == replayed.end();
        }
        check(no_rerun, "completed_games_are_not_rerun_on_resume");

        auto fully_replayed = make_runner(roster, script, 4242, limits, {2, 500, 200}, ledger);
        tr::RunResult const replayed_result = fully_replayed.runner.run();
        check(replayed_result.complete
                  && fully_replayed.runner.total_games() == static_cast<std::int64_t>(ledger.size())
                  && fully_replayed.log->call_ids.empty(),
              "resume_with_complete_ledger_runs_nothing");

        {
            std::vector<tr::GameRecord> tampered = prefix;
            tampered[0].seed_player_one ^= 1ULL;
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::SeedMismatch, "tampered_seed_is_rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = prefix;
            tampered[0].seat.side_a_is_player_one = !tampered[0].seat.side_a_is_player_one;
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::LedgerCorrupt, "tampered_seat_is_rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = prefix;
            tampered.erase(tampered.begin() + static_cast<std::ptrdiff_t>(tampered.size() / 2));
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::LedgerCorrupt, "ledger_gap_is_rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = prefix;
            tampered[0].series_id = 9999;
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::LedgerCorrupt, "unknown_series_is_rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = prefix;
            tampered[0].game_id = tampered[0].game_id + 1;
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::LedgerCorrupt, "corrupt_game_id_is_rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = prefix;
            for (tr::GameRecord &record : tampered)
            {
                if (record.winner == tb::GameWinner::SideA)
                {
                    record.winner = tb::GameWinner::SideB;
                    break;
                }
            }
            auto bad = make_runner(roster, script, 4242, limits, {2, 500, 200}, tampered);
            tr::RunResult const bad_result = bad.runner.run();
            check(bad_result.error.code == tr::ErrorCode::LedgerCorrupt, "inconsistent_replayed_outcome_is_rejected");
        }
    }

    void run_validation_tests()
    {
        {
            auto handle = make_runner({}, std::make_shared<Script>(), 1, {});
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::InvalidRoster, "empty_roster_is_rejected");
        }
        {
            auto handle = make_runner(make_roster({5, 5}), std::make_shared<Script>(), 1, {});
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::InvalidRoster, "duplicate_candidate_ids_are_rejected");
        }
        {
            auto handle = make_runner(make_roster({tb::kNoCandidate, 5}), std::make_shared<Script>(), 1, {});
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::InvalidRoster, "reserved_candidate_id_is_rejected");
        }
        {
            auto handle = make_runner(make_roster({42}), std::make_shared<Script>(), 1, {});
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::InvalidRoster, "single_entrant_is_rejected");
        }
        {
            auto roster = make_roster({10, 20});
            roster[1].theta = {1.0, 2.0};
            auto handle = make_runner(roster, std::make_shared<Script>(), 1, {});
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::BackendRejectedTheta, "malformed_theta_is_rejected");
        }
        {
            tu::RunConfig bad;
            bad.threads = 0;
            auto handle = make_runner(make_roster({10, 20}), std::make_shared<Script>(), 1, {}, bad);
            tr::RunResult const result = handle.runner.run();
            check(result.error.code == tr::ErrorCode::InvalidRunConfig, "invalid_run_config_is_rejected");
        }
    }

    void run_executor_backend_test()
    {
        auto const roster = make_roster({101, 202});
        auto script = std::make_shared<Script>();
        script->per_series = {{0, always_a}, {1, always_a}, {2, always_a}};

        ScriptedBackend direct_backend;
        direct_backend.script = script;
        Runner direct(direct_backend, make_roster({101, 202}), 77, {2, 500, 200}, {});
        tr::RunResult const direct_result = direct.run();
        check(direct_result.complete, "executor_reference_run_completes");

        ExecutorBackend executor_backend;
        executor_backend.script = script;
        tr::TournamentRunner<ExecutorBackend> via_executor(executor_backend, make_roster({101, 202}), 77,
                                                           {2, 500, 200}, {});
        tr::RunResult const executor_result = via_executor.run();
        check(executor_result.complete, "executor_backed_backend_completes");
        check(via_executor.checksum() == direct.checksum() && via_executor.champion() == direct.champion(),
              "executor_backed_backend_matches_direct_backend");
    }
}

int main()
{
    run_helper_tests();
    run_demand_window_tests();
    run_two_candidate_tests();
    run_three_candidate_tests();
    run_eight_candidate_tests();
    run_thirty_candidate_tests();
    run_nine_two_scheduling_test();
    run_malformed_backend_tests();
    run_limit_tests();
    run_resume_tests();
    run_validation_tests();
    run_executor_backend_test();
    std::println("{}/{} checks passed", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
