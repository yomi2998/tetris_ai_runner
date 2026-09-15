#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

#include "tournament/audit.h"

namespace
{
    namespace ta = tournament_audit;
    namespace tp = tournament_provenance;
    namespace tw = tournament_wire;
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

    tp::ProvenanceRecord make_record(tw::GameId game_id, tw::DeviceId device)
    {
        int const winner = game_id % 3 == 0 ? 0 : game_id % 2 == 0 ? 1 : -1;
        tp::ProvenanceRecord record;
        record.game_id = game_id;
        record.device = device;
        record.nonce = 7000 + game_id;
        record.game.id = game_id;
        record.game.theta_a = {1.0, 2.0};
        record.game.theta_b = {3.0, 4.0};
        record.game.seed_a = 100 + game_id;
        record.game.seed_b = 200 + game_id;
        record.reported.id = game_id;
        record.reported.winner = winner;
        record.reported.dead_a = winner != 0;
        record.reported.dead_b = winner != 0;
        record.reported.capped = winner == 0;
        record.reported.rounds = static_cast<int>(game_id) * 3 + 1;
        record.reported.app_a = 1.25;
        record.reported.app_b = 2.5;
        record.reported.apl_a = 0.5;
        record.reported.apl_b = 0.75;
        record.reported.reason = winner > 0 ? tu::WinReason::ASurvivor
            : winner < 0 ? tu::WinReason::BSurvivor
                         : tu::WinReason::CapDraw;
        record.assigned_at_ms = game_id * 10;
        record.accepted_at_ms = game_id * 10 + 5;
        return record;
    }

    tp::ProvenanceLedger sample_ledger(std::map<tw::GameId, tw::WireOutcome> &reported_by_id)
    {
        tp::ProvenanceLedger ledger;
        for (tw::GameId game_id = 1; game_id <= 20; ++game_id)
        {
            tp::ProvenanceRecord record = make_record(game_id, game_id % 2 == 0 ? 11 : 22);
            reported_by_id.emplace(game_id, record.reported);
            ledger.record(std::move(record));
        }
        return ledger;
    }

    ta::ReRun echo_re_run(std::map<tw::GameId, tw::WireOutcome> const &reported_by_id)
    {
        return [&reported_by_id](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            outcomes.reserve(games.size());
            for (tu::BatchGame const &game : games)
            {
                outcomes.push_back(tw::from_wire(reported_by_id.at(game.id)));
            }
            return outcomes;
        };
    }

    void test_honest_results_pass()
    {
        std::map<tw::GameId, tw::WireOutcome> reported;
        tp::ProvenanceLedger const ledger = sample_ledger(reported);
        std::vector<ta::AuditTarget> const targets = ta::select_targets(ledger, 1.0, {}, 7);
        check(targets.size() == ledger.size(), "echo: every game is targeted at rate one");
        ta::AuditReport const report = ta::audit_records(ledger, targets, tu::RunConfig{}, echo_re_run(reported));
        check(report.verdicts.size() == targets.size(), "echo: one verdict per target");
        bool ordered = true;
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            ordered = ordered && report.verdicts[i].target == targets[i];
        }
        check(ordered, "echo: verdicts follow the sorted target order");
        bool all_pass = true;
        for (ta::AuditVerdict const &verdict : report.verdicts)
        {
            all_pass = all_pass && verdict.passed;
        }
        check(all_pass, "echo: every verdict passes");
        check(report.failed_devices().empty(), "echo: no failed devices");
    }

    void test_flipped_winner_fails()
    {
        std::map<tw::GameId, tw::WireOutcome> reported;
        tp::ProvenanceLedger const ledger = sample_ledger(reported);
        tw::GameId const sabotaged = 5;
        auto const flipped = [&reported, sabotaged](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            for (tu::BatchGame const &game : games)
            {
                tu::GameOutcome outcome = tw::from_wire(reported.at(game.id));
                if (game.id == sabotaged)
                {
                    outcome.winner = -outcome.winner;
                }
                outcomes.push_back(outcome);
            }
            return outcomes;
        };
        std::vector<ta::AuditTarget> const targets = ta::select_targets(ledger, 1.0, {}, 7);
        ta::AuditReport const report = ta::audit_records(ledger, targets, tu::RunConfig{}, flipped);
        ta::AuditVerdict const *bad = nullptr;
        std::size_t failed_count = 0;
        for (ta::AuditVerdict const &verdict : report.verdicts)
        {
            if (!verdict.passed)
            {
                ++failed_count;
                bad = &verdict;
            }
        }
        check(failed_count == 1 && bad != nullptr && bad->target.game == sabotaged,
              "flip: only the flipped game fails");
        check(bad != nullptr && bad->reported.winner == -bad->actual.winner,
              "flip: the failed verdict opposes the winner");
        std::vector<tw::DeviceId> const devices = report.failed_devices();
        check(devices.size() == 1 && devices[0] == 22, "flip: failed devices name exactly the lying device");
    }

    void test_rounds_change_fails()
    {
        std::map<tw::GameId, tw::WireOutcome> reported;
        tp::ProvenanceLedger const ledger = sample_ledger(reported);
        tw::GameId const sabotaged = 8;
        auto const stretched = [&reported, sabotaged](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            for (tu::BatchGame const &game : games)
            {
                tu::GameOutcome outcome = tw::from_wire(reported.at(game.id));
                if (game.id == sabotaged)
                {
                    outcome.rounds = outcome.rounds + 1;
                }
                outcomes.push_back(outcome);
            }
            return outcomes;
        };
        std::vector<ta::AuditTarget> const targets = ta::select_targets(ledger, 1.0, {}, 7);
        ta::AuditReport const report = ta::audit_records(ledger, targets, tu::RunConfig{}, stretched);
        ta::AuditVerdict const *bad = nullptr;
        std::size_t failed_count = 0;
        for (ta::AuditVerdict const &verdict : report.verdicts)
        {
            if (!verdict.passed)
            {
                ++failed_count;
                bad = &verdict;
            }
        }
        check(failed_count == 1 && bad != nullptr && bad->target.game == sabotaged,
              "rounds: only the stretched game fails");
        check(bad != nullptr && bad->reported.winner == bad->actual.winner,
              "rounds: the winner agrees on the failed verdict");
        check(bad != nullptr && bad->actual.rounds == bad->reported.rounds + 1,
              "rounds: the re-run played one round more");
        std::vector<tw::DeviceId> const devices = report.failed_devices();
        check(devices.size() == 1 && devices[0] == 11, "rounds: failed devices name exactly the lying device");
    }

    void test_select_targets()
    {
        std::map<tw::GameId, tw::WireOutcome> reported;
        tp::ProvenanceLedger const ledger = sample_ledger(reported);
        std::vector<tw::GameId> const forced{3, 7, 999};

        std::vector<ta::AuditTarget> const none = ta::select_targets(ledger, 0.0, forced, 5);
        check(none.size() == 2 && none[0].game == 3 && none[1].game == 7,
              "select: rate zero keeps only forced ids that exist");
        check(none[0].device == 22 && none[1].device == 22, "select: forced targets carry the ledger device");
        check(ta::select_targets(ledger, 0.0, {}, 5).empty(), "select: rate zero without forced ids is empty");

        std::vector<ta::AuditTarget> const all = ta::select_targets(ledger, 1.0, forced, 5);
        check(all.size() == ledger.size(), "select: rate one targets every record exactly once");
        bool const all_sorted = std::is_sorted(all.begin(), all.end(),
                                               [](ta::AuditTarget const &a, ta::AuditTarget const &b)
                                               {
                                                   return a.game < b.game;
                                               });
        bool const all_unique = std::adjacent_find(all.begin(), all.end(),
                                                   [](ta::AuditTarget const &a, ta::AuditTarget const &b)
                                                   {
                                                       return a.game == b.game;
                                                   })
            == all.end();
        bool devices_ok = true;
        for (ta::AuditTarget const &target : all)
        {
            tp::ProvenanceRecord const *record = ledger.find(target.game);
            devices_ok = devices_ok && record != nullptr && record->device == target.device;
        }
        check(all_sorted && all_unique && devices_ok, "select: rate one list is sorted, unique, and matches devices");

        std::vector<ta::AuditTarget> const first = ta::select_targets(ledger, 0.5, {}, 42);
        std::vector<ta::AuditTarget> const second = ta::select_targets(ledger, 0.5, {}, 42);
        check(first == second, "select: the same seed reproduces the same sample");
        check(!first.empty() && first.size() < ledger.size(), "select: rate half picks a strict subset");
        check(std::is_sorted(first.begin(), first.end(),
                             [](ta::AuditTarget const &a, ta::AuditTarget const &b)
                             {
                                 return a.game < b.game;
                             }),
              "select: sampled targets are sorted by game id");

        check(ta::select_targets(ledger, -3.0, {}, 42) == ta::select_targets(ledger, 0.0, {}, 42),
              "select: negative rates clamp to zero");
        check(ta::select_targets(ledger, 3.0, {}, 42).size() == ledger.size(),
              "select: rates above one clamp to everything");
    }

    void test_count_mismatch_throws()
    {
        std::map<tw::GameId, tw::WireOutcome> reported;
        tp::ProvenanceLedger const ledger = sample_ledger(reported);
        std::vector<ta::AuditTarget> const targets = ta::select_targets(ledger, 1.0, {}, 3);

        auto const short_re_run = [&reported](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            for (std::size_t i = 0; i + 1 < games.size(); ++i)
            {
                outcomes.push_back(tw::from_wire(reported.at(games[i].id)));
            }
            return outcomes;
        };
        bool threw = false;
        try
        {
            ta::audit_records(ledger, targets, tu::RunConfig{}, short_re_run);
        }
        catch (std::runtime_error const &)
        {
            threw = true;
        }
        check(threw, "count: a short re-run throws runtime_error");

        auto const foreign_re_run = [&reported](std::vector<tu::BatchGame> const &games, tu::RunConfig const &)
        {
            std::vector<tu::GameOutcome> outcomes;
            for (tu::BatchGame const &game : games)
            {
                tu::GameOutcome outcome = tw::from_wire(reported.at(game.id));
                if (outcomes.empty())
                {
                    outcome.id = outcome.id + 1000000ULL;
                }
                outcomes.push_back(outcome);
            }
            return outcomes;
        };
        threw = false;
        try
        {
            ta::audit_records(ledger, targets, tu::RunConfig{}, foreign_re_run);
        }
        catch (std::runtime_error const &)
        {
            threw = true;
        }
        check(threw, "count: a foreign re-run id throws runtime_error");
    }
}

int run_audit_tests()
{
    test_honest_results_pass();
    test_flipped_winner_fails();
    test_rounds_change_fails();
    test_select_targets();
    test_count_mismatch_throws();
    std::println("audit: {} checks, {} failures", g_checks, g_failures);
    return g_failures;
}
