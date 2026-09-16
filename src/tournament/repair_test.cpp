#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "tournament/repair.h"

namespace
{
    namespace tr = tournament_runner;
    namespace tb = tournament_bracket;
    namespace trp = tournament_repair;
    namespace tu = tuning;

    int g_checks = 0;
    int g_failures = 0;

    void check(bool condition, std::string const &name)
    {
        ++g_checks;
        if (condition)
        {
            std::println("PASS: {}", name);
        }
        else
        {
            ++g_failures;
            std::println("FAIL: {}", name);
        }
    }

    int fake_winner(std::uint64_t seed_player_one)
    {
        return seed_player_one % 2 == 0 ? 1 : -1;
    }

    tu::GameOutcome fake_outcome(tu::BatchGame const &game)
    {
        tu::GameOutcome outcome;
        outcome.id = game.id;
        outcome.winner = fake_winner(game.seed_a);
        outcome.rounds = 42;
        outcome.reason = outcome.winner > 0 ? tu::WinReason::ASurvivor : tu::WinReason::BSurvivor;
        return outcome;
    }

    trp::ReRun fake_re_run()
    {
        return [](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            outcomes.reserve(games.size());
            for (tu::BatchGame const &game : games)
            {
                outcomes.push_back(fake_outcome(game));
            }
            return outcomes;
        };
    }

    struct FakeBackend
    {
        tu::ParamSchema schema() const
        {
            static char const *const names[] = {"alpha", "beta", "gamma"};
            static double const scales[] = {1.0, 2.0, 4.0};
            static double const defaults[] = {0.5, -1.25, 8.0};
            return tu::ParamSchema{.adapter_id = "fake_repair", .names = names, .scales = scales, .defaults = defaults};
        }

        bool validate(std::vector<double> const &theta) const
        {
            return theta.size() == 3 && tu::theta_finite(theta.data(), theta.size());
        }

        std::vector<tu::GameOutcome> run_games(std::vector<tu::BatchGame> const &games, tu::RunConfig const &) const
        {
            std::vector<tu::GameOutcome> outcomes;
            outcomes.reserve(games.size());
            for (tu::BatchGame const &game : games)
            {
                outcomes.push_back(fake_outcome(game));
            }
            return outcomes;
        }
    };

    static_assert(tu::MatchBackend<FakeBackend>);

    std::vector<tr::RosterEntry> make_roster(std::size_t count)
    {
        std::vector<tr::RosterEntry> roster;
        for (tb::CandidateId id = 1000; id < 1000 + count; ++id)
        {
            tr::RosterEntry entry;
            entry.id = id;
            entry.theta = {static_cast<double>(id) * 0.01, 1.5, -0.5};
            roster.push_back(std::move(entry));
        }
        return roster;
    }

    std::vector<tb::CandidateId> roster_ids(std::vector<tr::RosterEntry> const &roster)
    {
        std::vector<tb::CandidateId> ids;
        ids.reserve(roster.size());
        for (tr::RosterEntry const &entry : roster)
        {
            ids.push_back(entry.id);
        }
        return ids;
    }

    tb::GameWinner honest_winner(std::uint64_t generation_seed, tb::SeriesView const &view, int game_index)
    {
        auto const seeds = tr::player_seeds_for(generation_seed, view.id, game_index);
        return tr::normalize_outcome(fake_winner(seeds.first), tr::side_a_is_player_one(game_index));
    }

    using ScriptFn = std::function<tb::GameWinner(tb::SeriesView const &, int)>;

    struct GenerationResult
    {
        std::vector<tr::GameRecord> ledger;
        tb::CandidateId champion = tb::kNoCandidate;
    };

    GenerationResult generate_ledger(std::vector<tr::RosterEntry> const &roster, std::uint64_t generation_seed,
                                     ScriptFn const &script)
    {
        tb::Bracket bracket = tb::Bracket::create(roster_ids(roster));
        GenerationResult result;
        long guard = 0;
        while (!bracket.complete())
        {
            bool progressed = false;
            for (int series_id : bracket.ready_series())
            {
                while (bracket.series(series_id).status == tb::SeriesStatus::Ready)
                {
                    tb::SeriesView const view = bracket.series(series_id);
                    int const game_index = view.games_played;
                    tb::GameWinner const winner = script(view, game_index);
                    tr::GameRecord record;
                    record.series_id = series_id;
                    record.game_index = game_index;
                    record.game_id = tr::game_id_for(series_id, game_index);
                    record.seat = tr::seat_for(game_index, view.side_a, view.side_b);
                    auto const seeds = tr::player_seeds_for(generation_seed, series_id, game_index);
                    record.seed_player_one = seeds.first;
                    record.seed_player_two = seeds.second;
                    record.winner = winner;
                    record.reason = winner == tb::GameWinner::SideA ? tu::WinReason::ASurvivor
                        : winner == tb::GameWinner::SideB ? tu::WinReason::BSurvivor
                                                          : tu::WinReason::CapDraw;
                    record.rounds = 42;
                    if (bracket.report_game(series_id, game_index, winner) != tb::ReportStatus::Accepted)
                    {
                        check(false, "generate: bracket rejected a scripted game");
                        return result;
                    }
                    result.ledger.push_back(record);
                    progressed = true;
                    if (++guard > 1000000L)
                    {
                        check(false, "generate: guard tripped");
                        return result;
                    }
                }
            }
            if (!progressed)
            {
                check(false, "generate: no progress while driving the bracket");
                return result;
            }
        }
        result.champion = bracket.champion();
        return result;
    }

    tr::GameRecord crafted_record(std::uint64_t generation_seed, int series_id, int game_index, tb::CandidateId side_a,
                                  tb::CandidateId side_b, tb::GameWinner winner)
    {
        tr::GameRecord record;
        record.series_id = series_id;
        record.game_index = game_index;
        record.game_id = tr::game_id_for(series_id, game_index);
        record.seat = tr::seat_for(game_index, side_a, side_b);
        auto const seeds = tr::player_seeds_for(generation_seed, series_id, game_index);
        record.seed_player_one = seeds.first;
        record.seed_player_two = seeds.second;
        record.winner = winner;
        record.reason = winner == tb::GameWinner::SideA ? tu::WinReason::ASurvivor : tu::WinReason::BSurvivor;
        record.rounds = 42;
        return record;
    }

    struct ReplayResult
    {
        bool all_accepted = false;
        bool complete = false;
        tb::CandidateId champion = tb::kNoCandidate;
    };

    ReplayResult replay_ledger(std::vector<tb::CandidateId> const &ids, std::vector<tr::GameRecord> const &ledger)
    {
        tb::Bracket bracket = tb::Bracket::create(ids);
        ReplayResult result;
        result.all_accepted = true;
        for (tr::GameRecord const &record : ledger)
        {
            if (record.series_id < 0 || record.series_id >= bracket.series_count()
                || bracket.report_game(record.series_id, record.game_index, record.winner) != tb::ReportStatus::Accepted)
            {
                result.all_accepted = false;
                return result;
            }
        }
        result.complete = bracket.complete();
        result.champion = bracket.champion();
        return result;
    }

    trp::RepairRequest make_request(std::vector<tr::RosterEntry> const &roster, std::uint64_t generation_seed,
                                    std::vector<tr::GameRecord> const &ledger, std::vector<std::uint64_t> const &voided)
    {
        trp::RepairRequest request;
        request.roster = roster;
        request.generation_seed = generation_seed;
        request.config = tu::RunConfig{};
        request.ledger = ledger;
        request.voided_game_ids = voided;
        request.re_run = fake_re_run();
        return request;
    }

    std::uint64_t seed_with_player_one_win_at(int series_id, int game_index)
    {
        for (std::uint64_t seed = 1;; ++seed)
        {
            auto const seeds = tr::player_seeds_for(seed, series_id, game_index);
            if (fake_winner(seeds.first) > 0)
            {
                return seed;
            }
        }
    }

    void test_identity()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(8);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        GenerationResult const generation = generate_ledger(roster, 77,
                                                            [](tb::SeriesView const &view, int game_index)
                                                            {
                                                                return honest_winner(77, view, game_index);
                                                            });
        check(generation.champion != tb::kNoCandidate, "identity: reference tournament completed");
        check(!generation.ledger.empty(), "identity: reference ledger is nonempty");

        trp::RepairResult const result = trp::repair_ledger(make_request(roster, 77, generation.ledger, {}));
        check(result.ok, "identity: repair succeeds");
        check(result.error.empty(), "identity: repair reports no error");
        check(result.re_run_games == 0, "identity: no games re-run");
        check(result.dropped_games == 0, "identity: no games dropped");
        check(result.voided_games == 0, "identity: no games voided");
        check(result.diverged_games == 0, "identity: no games diverged");
        check(result.repaired_ledger == generation.ledger, "identity: repaired ledger matches the input in order");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "identity: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "identity: replay crowns the same champion");
    }

    void test_mid_series_void()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(8);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        GenerationResult const generation = generate_ledger(roster, 77,
                                                            [](tb::SeriesView const &view, int game_index)
                                                            {
                                                                return honest_winner(77, view, game_index);
                                                            });
        check(generation.champion != tb::kNoCandidate, "void mid-series: reference tournament completed");

        trp::RepairResult const result = trp::repair_ledger(make_request(roster, 77, generation.ledger,
                                                                         {tr::game_id_for(0, 2)}));
        check(result.ok, "void mid-series: repair succeeds");
        check(result.error.empty(), "void mid-series: repair reports no error");
        check(result.voided_games == 1, "void mid-series: one record voided");
        check(result.re_run_games == 1, "void mid-series: exactly one re-run fills the hole");
        check(result.dropped_games == 0, "void mid-series: no records dropped");
        check(result.diverged_games == 0, "void mid-series: no records diverged");
        check(result.repaired_ledger.size() == generation.ledger.size(), "void mid-series: ledger length is preserved");
        check(result.repaired_ledger == generation.ledger, "void mid-series: the re-run record matches the original");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "void mid-series: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "void mid-series: replay crowns the same champion");
    }

    void test_clinch_moves_earlier()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(8);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        std::uint64_t const generation_seed = seed_with_player_one_win_at(0, 6);
        auto const lie_script = [generation_seed](tb::SeriesView const &view, int game_index)
        {
            if (view.id == 0)
            {
                if (game_index <= 5)
                {
                    return tb::GameWinner::SideA;
                }
                if (game_index == 6)
                {
                    return tb::GameWinner::SideB;
                }
                return tb::GameWinner::SideA;
            }
            return honest_winner(generation_seed, view, game_index);
        };
        GenerationResult const generation = generate_ledger(roster, generation_seed, lie_script);
        check(generation.champion != tb::kNoCandidate, "clinch: scripted tournament completed");
        bool const extended = std::any_of(generation.ledger.begin(), generation.ledger.end(),
                                          [](tr::GameRecord const &record)
                                          {
                                              return record.game_id == tr::game_id_for(0, 7);
                                          });
        check(extended, "clinch: recorded series runs past the honest clinch point");
        check(fake_winner(tr::player_seeds_for(generation_seed, 0, 6).first) > 0,
              "clinch: honest re-run at the hole is a player one win");

        trp::RepairResult const result = trp::repair_ledger(make_request(roster, generation_seed, generation.ledger,
                                                                         {tr::game_id_for(0, 6)}));
        check(result.ok, "clinch: repair succeeds");
        check(result.error.empty(), "clinch: repair reports no error");
        check(result.voided_games == 1, "clinch: one record voided");
        check(result.re_run_games == 1, "clinch: the hole is re-run once");
        check(result.dropped_games >= 1, "clinch: surplus games are dropped");
        check(result.diverged_games == 0, "clinch: advancement is unchanged so nothing diverges");
        check(result.repaired_ledger.size() == generation.ledger.size() - 1, "clinch: one record left the ledger");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "clinch: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "clinch: replay crowns the same champion");
    }

    void test_advancement_divergence()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(4);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        std::uint64_t const generation_seed = seed_with_player_one_win_at(0, 10);
        auto const lie_script = [generation_seed](tb::SeriesView const &view, int game_index)
        {
            if (view.id == 0)
            {
                return game_index <= 9 ? tb::GameWinner::SideA : tb::GameWinner::SideB;
            }
            return honest_winner(generation_seed, view, game_index);
        };
        GenerationResult const lying = generate_ledger(roster, generation_seed, lie_script);
        check(lying.champion != tb::kNoCandidate, "diverge: lying tournament completed");
        bool const lied_game_present = std::any_of(lying.ledger.begin(), lying.ledger.end(),
                                                   [](tr::GameRecord const &record)
                                                   {
                                                       return record.game_id == tr::game_id_for(0, 10);
                                                   });
        check(lied_game_present, "diverge: the lied feeder game is in the ledger");
        check(fake_winner(tr::player_seeds_for(generation_seed, 0, 10).first) > 0,
              "diverge: the honest re-run at the lied game is a player one win");
        std::size_t const phantom_count = static_cast<std::size_t>(std::count_if(
            lying.ledger.begin(), lying.ledger.end(),
            [](tr::GameRecord const &record)
            {
                return record.series_id == 2 || record.series_id == 3;
            }));
        check(phantom_count >= 2, "diverge: downstream series carry records in the fixture");

        trp::RepairResult const result = trp::repair_ledger(make_request(roster, generation_seed, lying.ledger,
                                                                         {tr::game_id_for(0, 10)}));
        check(result.ok, "diverge: repair succeeds");
        check(result.error.empty(), "diverge: repair reports no error");
        check(result.voided_games == 1, "diverge: the lied game is voided");
        check(result.re_run_games == 1, "diverge: the hole is re-run once");
        check(result.diverged_games >= 2, "diverge: swapped-seat records are diverged");
        check(result.diverged_games == static_cast<int>(phantom_count), "diverge: every phantom record diverged");
        check(result.dropped_games >= 1, "diverge: surplus and deferred records are dropped");
        int const kept_from_input = static_cast<int>(result.repaired_ledger.size()) - result.re_run_games;
        int const expected_dropped = static_cast<int>(lying.ledger.size()) - result.voided_games
            - result.diverged_games - kept_from_input;
        check(result.dropped_games == expected_dropped, "diverge: every input record is accounted for");
        bool const phantom_absent = std::none_of(result.repaired_ledger.begin(), result.repaired_ledger.end(),
                                                 [](tr::GameRecord const &record)
                                                 {
                                                     return record.series_id >= 2;
                                                 });
        check(phantom_absent, "diverge: phantom records are absent from the repaired ledger");
        bool const feeder_kept = std::any_of(result.repaired_ledger.begin(), result.repaired_ledger.end(),
                                             [](tr::GameRecord const &record)
                                             {
                                                 return record.game_id == tr::game_id_for(0, 10);
                                             });
        check(feeder_kept, "diverge: the honest re-run fills the feeder hole");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "diverge: every repaired record is accepted in repaired order");

        auto const honest_script = [generation_seed](tb::SeriesView const &view, int game_index)
        {
            if (view.id == 0)
            {
                return tb::GameWinner::SideA;
            }
            return honest_winner(generation_seed, view, game_index);
        };
        GenerationResult const honest = generate_ledger(roster, generation_seed, honest_script);
        check(honest.champion != tb::kNoCandidate, "diverge: honest reference tournament completed");

        tr::TournamentRunner<FakeBackend> runner(FakeBackend{}, roster, generation_seed, tu::RunConfig{}, tr::RunLimits{},
                                                 result.repaired_ledger);
        tr::RunResult const resumed = runner.run();
        check(resumed.complete, "diverge: resumed tournament completes");
        check(tr::canonical_ledger(runner.ledger()) == tr::canonical_ledger(honest.ledger),
              "diverge: resumed ledger converges to the honest ledger");
        check(runner.checksum() == tr::ledger_checksum(honest.ledger), "diverge: checksum converges to the honest one");
        check(runner.champion() == honest.champion, "diverge: champion converges to the honest one");
    }

    void test_pending_series_is_deferred()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(4);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        std::uint64_t const generation_seed = 4242;
        std::vector<tr::GameRecord> ledger;
        for (int index = 0; index < 3; ++index)
        {
            ledger.push_back(crafted_record(generation_seed, 2, index, 1000, 1001, tb::GameWinner::SideA));
        }
        for (int index = 0; index < 11; ++index)
        {
            ledger.push_back(crafted_record(generation_seed, 0, index, 1000, 1003, tb::GameWinner::SideA));
        }
        for (int index = 0; index < 11; ++index)
        {
            ledger.push_back(crafted_record(generation_seed, 1, index, 1001, 1002, tb::GameWinner::SideA));
        }

        trp::RepairResult const result = trp::repair_ledger(make_request(roster, generation_seed, ledger, {}));
        check(result.ok, "defer: repair succeeds");
        check(result.error.empty(), "defer: repair reports no error");
        check(result.dropped_games == 0, "defer: nothing is dropped while waiting for dependencies");
        check(result.diverged_games == 0, "defer: nothing diverged");
        check(result.re_run_games == 0, "defer: no fills were needed");
        check(result.voided_games == 0, "defer: nothing was voided");
        check(result.repaired_ledger.size() == ledger.size(), "defer: every record is kept");
        std::size_t const kept_series_two = static_cast<std::size_t>(std::count_if(
            result.repaired_ledger.begin(), result.repaired_ledger.end(),
            [](tr::GameRecord const &record)
            {
                return record.series_id == 2;
            }));
        check(kept_series_two == 3, "defer: the pending series records are kept on a later pass");
        std::vector<tr::GameRecord> expected(ledger.begin() + 3, ledger.end());
        expected.push_back(ledger[0]);
        expected.push_back(ledger[1]);
        expected.push_back(ledger[2]);
        check(result.repaired_ledger == expected, "defer: repaired ledger follows walk acceptance order");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "defer: repaired ledger replays in order");
    }

    void test_duplicate_index_is_dropped()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(8);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        GenerationResult const generation = generate_ledger(roster, 4242,
                                                            [](tb::SeriesView const &view, int game_index)
                                                            {
                                                                return honest_winner(4242, view, game_index);
                                                            });
        check(generation.champion != tb::kNoCandidate, "duplicate: reference tournament completed");

        std::vector<tr::GameRecord> ledger = generation.ledger;
        ledger.insert(ledger.begin() + 1, ledger.front());
        trp::RepairResult const result = trp::repair_ledger(make_request(roster, 4242, ledger, {}));
        check(result.ok, "duplicate: repair succeeds");
        check(result.error.empty(), "duplicate: repair reports no error");
        check(result.dropped_games == 1, "duplicate: the replayed index is dropped");
        check(result.diverged_games == 0, "duplicate: nothing diverged");
        check(result.re_run_games == 0, "duplicate: nothing was re-run");
        check(result.repaired_ledger == generation.ledger, "duplicate: repaired ledger matches the original in order");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted && clean.complete, "duplicate: repaired ledger replays cleanly");
    }

    void test_corrupt_input()
    {
        std::vector<tr::RosterEntry> const roster = make_roster(8);
        std::vector<tb::CandidateId> const ids = roster_ids(roster);
        GenerationResult const generation = generate_ledger(roster, 77,
                                                            [](tb::SeriesView const &view, int game_index)
                                                            {
                                                                return honest_winner(77, view, game_index);
                                                            });
        check(generation.champion != tb::kNoCandidate, "corrupt: reference tournament completed");

        {
            std::vector<tr::GameRecord> tampered = generation.ledger;
            tampered.front().game_id = tampered.front().game_id + 1;
            trp::RepairResult const result = trp::repair_ledger(make_request(roster, 77, tampered, {}));
            check(!result.ok && !result.error.empty(),
                  "corrupt: game id disagreeing with series and index is rejected");
        }
        {
            std::vector<tr::GameRecord> tampered = generation.ledger;
            tr::GameRecord &bad = tampered.front();
            bad.series_id = 100000;
            bad.game_id = tr::game_id_for(100000, bad.game_index);
            trp::RepairResult const result = trp::repair_ledger(make_request(roster, 77, tampered, {}));
            check(!result.ok && !result.error.empty(), "corrupt: out of range series id is rejected");
        }
        ReplayResult const clean = replay_ledger(ids, generation.ledger);
        check(clean.all_accepted && clean.complete, "corrupt: the reference ledger itself replays cleanly");
    }
}

int run_audit_tests();

int run_repair_tests()
{
    test_identity();
    test_mid_series_void();
    test_clinch_moves_earlier();
    test_advancement_divergence();
    test_pending_series_is_deferred();
    test_duplicate_index_is_dropped();
    test_corrupt_input();
    std::println("repair: {} checks, {} failures", g_checks, g_failures);
    return g_failures;
}

int main()
{
    int const audit_failures = run_audit_tests();
    int const repair_failures = run_repair_tests();
    int const total = audit_failures + repair_failures;
    if (total == 0)
    {
        std::println("ALL CHECKS PASSED");
    }
    else
    {
        std::println("{} CHECK FAILURES", total);
    }
    return total == 0 ? 0 : 1;
}
