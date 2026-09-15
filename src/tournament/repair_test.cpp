#include <algorithm>
#include <cstdint>
#include <functional>
#include <print>
#include <string>
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

    std::vector<tr::RosterEntry> make_roster()
    {
        std::vector<tr::RosterEntry> roster;
        for (tb::CandidateId id = 1000; id < 1008; ++id)
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
        for (tr::GameRecord const &record : tr::canonical_ledger(ledger))
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
        std::vector<tr::RosterEntry> const roster = make_roster();
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
        check(result.repaired_ledger == tr::canonical_ledger(generation.ledger),
              "identity: repaired ledger matches the input");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "identity: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "identity: replay crowns the same champion");
    }

    void test_mid_series_void()
    {
        std::vector<tr::RosterEntry> const roster = make_roster();
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
        check(result.repaired_ledger.size() == generation.ledger.size(), "void mid-series: ledger length is preserved");
        check(result.repaired_ledger == tr::canonical_ledger(generation.ledger),
              "void mid-series: the re-run record matches the original");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "void mid-series: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "void mid-series: replay crowns the same champion");
    }

    void test_clinch_moves_earlier()
    {
        std::vector<tr::RosterEntry> const roster = make_roster();
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
        check(result.repaired_ledger.size() == generation.ledger.size() - 1, "clinch: one record left the ledger");
        ReplayResult const clean = replay_ledger(ids, result.repaired_ledger);
        check(clean.all_accepted, "clinch: every replayed record is accepted");
        check(clean.complete && clean.champion == generation.champion, "clinch: replay crowns the same champion");
    }

    void test_corrupt_input()
    {
        std::vector<tr::RosterEntry> const roster = make_roster();
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
