#include "tournament/repair.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tournament_repair
{
    namespace
    {
        using Bracket = tournament_bracket::Bracket;
        using CandidateId = tournament_bracket::CandidateId;
        using ReportStatus = tournament_bracket::ReportStatus;
        using SeriesView = tournament_bracket::SeriesView;
        using GameRecord = tournament_runner::GameRecord;
        using RosterEntry = tournament_runner::RosterEntry;

        std::string roster_problem(std::vector<RosterEntry> const &roster)
        {
            if (roster.empty())
            {
                return "roster is empty";
            }
            std::unordered_set<CandidateId> seen;
            for (RosterEntry const &entry : roster)
            {
                if (entry.id == tournament_bracket::kNoCandidate)
                {
                    return "roster contains the reserved candidate id";
                }
                if (entry.theta.empty())
                {
                    return "roster entry " + std::to_string(entry.id) + " has an empty theta";
                }
                if (!seen.insert(entry.id).second)
                {
                    return "roster contains duplicate candidate id " + std::to_string(entry.id);
                }
            }
            return {};
        }

        struct HoleGame
        {
            bool ok = false;
            std::string error;
            GameRecord record;
        };

        HoleGame run_hole_game(Bracket &bracket, RepairRequest const &request,
                               std::unordered_map<CandidateId, std::vector<double>> const &theta_by_id, int series_id)
        {
            HoleGame outcome;
            SeriesView const view = bracket.series(series_id);
            if (view.status != tournament_bracket::SeriesStatus::Ready)
            {
                outcome.error = "series is not ready while filling a hole";
                return outcome;
            }
            int const game_index = view.games_played;
            if (game_index < 0 || static_cast<std::uint64_t>(game_index) > 0xFFFFFFFFULL)
            {
                outcome.error = "game index exceeds composite id range";
                return outcome;
            }
            GameRecord record;
            record.series_id = series_id;
            record.game_index = game_index;
            record.game_id = tournament_runner::game_id_for(series_id, game_index);
            record.seat = tournament_runner::seat_for(game_index, view.side_a, view.side_b);
            auto const seeds = tournament_runner::player_seeds_for(request.generation_seed, series_id, game_index);
            record.seed_player_one = seeds.first;
            record.seed_player_two = seeds.second;
            CandidateId const player_one = record.seat.side_a_is_player_one ? record.seat.side_a : record.seat.side_b;
            CandidateId const player_two = record.seat.side_a_is_player_one ? record.seat.side_b : record.seat.side_a;
            auto const theta_a = theta_by_id.find(player_one);
            auto const theta_b = theta_by_id.find(player_two);
            if (theta_a == theta_by_id.end() || theta_b == theta_by_id.end())
            {
                outcome.error = "roster has no theta for a bracket candidate";
                return outcome;
            }
            tuning::BatchGame game;
            game.id = record.game_id;
            game.theta_a = theta_a->second;
            game.theta_b = theta_b->second;
            game.seed_a = seeds.first;
            game.seed_b = seeds.second;
            std::vector<tuning::GameOutcome> const outcomes = request.re_run({game}, request.config);
            if (outcomes.size() != 1)
            {
                outcome.error = "re-run outcome count does not match the requested count";
                return outcome;
            }
            tuning::GameOutcome const &rerun = outcomes.front();
            if (rerun.id != record.game_id)
            {
                outcome.error = "re-run outcome id does not match the requested game";
                return outcome;
            }
            if (rerun.winner < -1 || rerun.winner > 1 || rerun.rounds < 0)
            {
                outcome.error = "re-run outcome is outside the supported domain";
                return outcome;
            }
            record.winner = tournament_runner::normalize_outcome(rerun.winner, record.seat.side_a_is_player_one);
            record.reason = tournament_runner::normalize_reason(rerun.reason, record.seat.side_a_is_player_one);
            record.rounds = rerun.rounds;
            if (!tournament_runner::reason_matches_winner(record.reason, record.winner))
            {
                outcome.error = "re-run outcome reason does not match winner";
                return outcome;
            }
            if (bracket.report_game(series_id, game_index, record.winner) != ReportStatus::Accepted)
            {
                outcome.error = "bracket rejected the re-run game " + std::to_string(record.game_id);
                return outcome;
            }
            outcome.ok = true;
            outcome.record = record;
            return outcome;
        }
    }

    RepairResult repair_ledger(RepairRequest request)
    {
        RepairResult result;
        if (!request.re_run)
        {
            result.error = "re-run function is not set";
            return result;
        }
        std::string const roster_fault = roster_problem(request.roster);
        if (!roster_fault.empty())
        {
            result.error = roster_fault;
            return result;
        }
        std::vector<CandidateId> ids;
        std::unordered_map<CandidateId, std::vector<double>> theta_by_id;
        ids.reserve(request.roster.size());
        for (RosterEntry const &entry : request.roster)
        {
            ids.push_back(entry.id);
            theta_by_id.emplace(entry.id, entry.theta);
        }
        Bracket bracket = Bracket::create(ids);
        if (!bracket.valid())
        {
            result.error = "bracket creation failed";
            return result;
        }
        std::vector<GameRecord> const ordered = tournament_runner::canonical_ledger(request.ledger);
        std::unordered_set<std::uint64_t> const voided(request.voided_game_ids.begin(), request.voided_game_ids.end());
        std::size_t cursor = 0;
        while (cursor < ordered.size())
        {
            GameRecord const &record = ordered[cursor];
            if (voided.find(record.game_id) != voided.end())
            {
                ++result.voided_games;
                ++cursor;
                continue;
            }
            if (record.series_id < 0 || record.series_id >= bracket.series_count())
            {
                result.error = "series id out of range for game " + std::to_string(record.game_id);
                return result;
            }
            if (record.game_index < 0
                || record.game_id != tournament_runner::game_id_for(record.series_id, record.game_index))
            {
                result.error = "game id does not match series and index for game " + std::to_string(record.game_id);
                return result;
            }
            ReportStatus status = bracket.report_game(record.series_id, record.game_index, record.winner);
            for (;;)
            {
                if (status == ReportStatus::Accepted)
                {
                    result.repaired_ledger.push_back(record);
                    ++cursor;
                    break;
                }
                if (status == ReportStatus::NotReady)
                {
                    ++result.dropped_games;
                    ++cursor;
                    break;
                }
                if (status != ReportStatus::GameIndexMismatch)
                {
                    result.error = "bracket rejected game " + std::to_string(record.game_id) + " in series "
                        + std::to_string(record.series_id);
                    return result;
                }
                SeriesView const view = bracket.series(record.series_id);
                if (record.game_index < view.games_played)
                {
                    result.error = "duplicate game index in series " + std::to_string(record.series_id) + " for game "
                        + std::to_string(record.game_id);
                    return result;
                }
                HoleGame const hole = run_hole_game(bracket, request, theta_by_id, record.series_id);
                if (!hole.ok)
                {
                    result.error = hole.error;
                    return result;
                }
                result.repaired_ledger.push_back(hole.record);
                ++result.re_run_games;
                status = bracket.report_game(record.series_id, record.game_index, record.winner);
            }
        }
        result.repaired_ledger = tournament_runner::canonical_ledger(result.repaired_ledger);
        result.ok = true;
        return result;
    }
}
