#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tournament/bracket.h"
#include "tournament/scheduler.h"
#include "tuning/domain.h"
#include "tuning/match.h"

namespace tournament_runner
{
    using Bracket = tournament_bracket::Bracket;
    using CandidateId = tournament_bracket::CandidateId;
    using GameWinner = tournament_bracket::GameWinner;
    using SeriesView = tournament_bracket::SeriesView;

    struct RosterEntry
    {
        CandidateId id = 0;
        std::vector<double> theta;
    };

    struct SeatAssignment
    {
        CandidateId side_a = tournament_bracket::kNoCandidate;
        CandidateId side_b = tournament_bracket::kNoCandidate;
        bool side_a_is_player_one = true;

        bool operator==(SeatAssignment const &) const = default;
    };

    struct GameRecord
    {
        std::uint64_t game_id = 0;
        int series_id = 0;
        int game_index = 0;
        SeatAssignment seat{};
        std::uint64_t seed_player_one = 0;
        std::uint64_t seed_player_two = 0;
        GameWinner winner = GameWinner::Draw;
        tuning::WinReason reason = tuning::WinReason::Unknown;
        int rounds = 0;

        bool operator==(GameRecord const &) const = default;
    };

    enum class ErrorCode : int
    {
        None = 0,
        InvalidRoster,
        InvalidRunConfig,
        BackendRejectedTheta,
        StalledTournament,
        GameLimitExceeded,
        DrawLimitExceeded,
        BackendFailed,
        BackendIncomplete,
        BackendMalformedOutcome,
        BracketRejected,
        GameIndexOverflow,
        LedgerCorrupt,
        SeedMismatch,
        BackendStopped,
    };

    struct RunnerError
    {
        ErrorCode code = ErrorCode::None;
        int series_id = -1;
        std::uint64_t game_id = 0;
        std::string detail;
    };

    struct RunLimits
    {
        int wave_limit = 0;
        std::int64_t max_games = 0;
        std::int64_t max_draws = 0;
    };

    struct WaveStats
    {
        std::int64_t games = 0;
        std::int64_t draws = 0;
        int waves = 0;
    };

    struct RunResult
    {
        RunnerError error{};
        WaveStats stats{};
        bool complete = false;
        CandidateId champion = tournament_bracket::kNoCandidate;
    };

    std::uint64_t game_id_for(int series_id, int game_index);
    void decode_game_id(std::uint64_t game_id, int &series_id, int &game_index);
    std::uint64_t series_scenario_tag(int series_id, int game_index);
    bool side_a_is_player_one(int game_index);
    SeatAssignment seat_for(int game_index, CandidateId side_a, CandidateId side_b);
    std::pair<std::uint64_t, std::uint64_t> player_seeds_for(std::uint64_t generation_seed, int series_id, int game_index);
    GameWinner normalize_outcome(int outcome_winner, bool side_a_is_player_one);
    tuning::WinReason normalize_reason(tuning::WinReason reason, bool side_a_is_player_one);
    bool reason_matches_winner(tuning::WinReason reason, GameWinner winner);
    std::vector<tournament_scheduler::SeriesDemand> plan_demands(std::vector<SeriesView> const &ready_views);
    std::vector<GameRecord> canonical_ledger(std::vector<GameRecord> const &records);
    std::uint64_t ledger_checksum(std::vector<GameRecord> const &records);

    template<class Backend>
    requires tuning::MatchBackend<Backend>
    class TournamentRunner
    {
    public:
        TournamentRunner(Backend backend, std::vector<RosterEntry> roster, std::uint64_t generation_seed,
                         tuning::RunConfig run_config, RunLimits limits,
                         std::vector<GameRecord> prior_ledger = {})
            : backend_(std::move(backend))
            , roster_(std::move(roster))
            , generation_seed_(generation_seed)
            , run_config_(run_config)
            , limits_(limits)
        {
            initialize(std::move(prior_ledger));
        }

        RunResult run()
        {
            RunResult result;
            for (;;)
            {
                RunResult const step = run_next_wave();
                if (step.error.code != ErrorCode::None)
                {
                    return step;
                }
                result.stats.games += step.stats.games;
                result.stats.draws += step.stats.draws;
                result.stats.waves += step.stats.waves;
                if (step.complete)
                {
                    result.complete = true;
                    result.champion = step.champion;
                    return result;
                }
            }
        }

        RunResult run_next_wave()
        {
            RunResult result;
            if (error_.code != ErrorCode::None)
            {
                result.error = error_;
                return result;
            }
            if (bracket_ && bracket_->complete())
            {
                result.complete = true;
                result.champion = bracket_->champion();
                return result;
            }
            std::vector<int> const ready = bracket_->ready_series();
            std::vector<SeriesView> views;
            views.reserve(ready.size());
            for (int id : ready)
            {
                views.push_back(bracket_->series(id));
            }
            std::vector<tournament_scheduler::SeriesDemand> const demands = plan_demands(views);
            if (demands.empty())
            {
                error_ = RunnerError{ErrorCode::StalledTournament, -1, 0, "no runnable series demand"};
                result.error = error_;
                return result;
            }
            std::int64_t const remaining = limits_.max_games > 0
                ? limits_.max_games - totals_.games
                : std::numeric_limits<std::int64_t>::max();
            if (remaining <= 0)
            {
                error_ = RunnerError{ErrorCode::GameLimitExceeded, -1, 0, "game limit reached"};
                result.error = error_;
                return result;
            }
            std::int64_t const configured = limits_.wave_limit > 0
                ? limits_.wave_limit
                : std::numeric_limits<std::int64_t>::max();
            std::int64_t const bounded = std::min(configured, remaining);
            int const wave_limit = static_cast<int>(std::min<std::int64_t>(bounded, std::numeric_limits<int>::max()));
            std::vector<tournament_scheduler::WaveSlot> const wave
                = tournament_scheduler::build_wave(demands, wave_limit);
            if (wave.empty())
            {
                error_ = RunnerError{ErrorCode::StalledTournament, -1, 0, "wave construction produced no games"};
                result.error = error_;
                return result;
            }
            if (!execute_wave(wave, views, result))
            {
                result.error = error_;
                return result;
            }
            if (bracket_ && bracket_->complete())
            {
                result.complete = true;
                result.champion = bracket_->champion();
            }
            return result;
        }

        bool ok() const
        {
            return error_.code == ErrorCode::None;
        }

        RunnerError const &error() const
        {
            return error_;
        }

        bool complete() const
        {
            return bracket_ && bracket_->valid() && bracket_->complete();
        }

        CandidateId champion() const
        {
            return bracket_ ? bracket_->champion() : tournament_bracket::kNoCandidate;
        }

        std::vector<CandidateId> standings() const
        {
            return bracket_ ? bracket_->standings() : std::vector<CandidateId>{};
        }

        Bracket const &bracket() const
        {
            return *bracket_;
        }

        std::vector<GameRecord> const &ledger() const
        {
            return ledger_;
        }

        std::uint64_t checksum() const
        {
            return tournament_runner::ledger_checksum(ledger_);
        }

        std::vector<tournament_bracket::ReplayEntry> replay_entries() const
        {
            return bracket_->replay_entries();
        }

        std::int64_t total_games() const
        {
            return totals_.games;
        }

        std::int64_t total_draws() const
        {
            return totals_.draws;
        }

        int total_waves() const
        {
            return totals_.waves;
        }

    private:
        struct Totals
        {
            std::int64_t games = 0;
            std::int64_t draws = 0;
            int waves = 0;
        };

        std::vector<double> const &theta_of(CandidateId id) const
        {
            for (RosterEntry const &entry : roster_)
            {
                if (entry.id == id)
                {
                    return entry.theta;
                }
            }
            static std::vector<double> const empty;
            return empty;
        }

        void initialize(std::vector<GameRecord> prior)
        {
            if (!tuning::valid_run_config(run_config_))
            {
                error_ = RunnerError{ErrorCode::InvalidRunConfig, -1, 0, "run config rejected"};
                return;
            }
            std::vector<CandidateId> ids;
            ids.reserve(roster_.size());
            for (RosterEntry const &entry : roster_)
            {
                if (entry.id == tournament_bracket::kNoCandidate)
                {
                    error_ = RunnerError{ErrorCode::InvalidRoster, -1, 0, "reserved candidate id"};
                    return;
                }
                if (!backend_.validate(entry.theta))
                {
                    error_ = RunnerError{ErrorCode::BackendRejectedTheta, -1, 0, "roster theta rejected by backend"};
                    return;
                }
                ids.push_back(entry.id);
            }
            std::vector<CandidateId> sorted = ids;
            std::sort(sorted.begin(), sorted.end());
            if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end())
            {
                error_ = RunnerError{ErrorCode::InvalidRoster, -1, 0, "duplicate candidate id"};
                return;
            }
            bracket_.emplace(Bracket::create(ids));
            if (!bracket_->valid())
            {
                error_ = RunnerError{ErrorCode::InvalidRoster, -1, 0, "bracket creation failed"};
                return;
            }
            for (GameRecord const &record : prior)
            {
                if (!apply_prior_record(record))
                {
                    return;
                }
            }
        }

        bool apply_prior_record(GameRecord const &record)
        {
            if (record.series_id < 0 || record.series_id >= bracket_->series_count())
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "series id out of range"};
                return false;
            }
            if (record.game_id != game_id_for(record.series_id, record.game_index))
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "game id does not match series and index"};
                return false;
            }
            SeriesView const view = bracket_->series(record.series_id);
            if (view.status != tournament_bracket::SeriesStatus::Ready)
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "series is not ready for a replayed game"};
                return false;
            }
            if (record.seat.side_a != view.side_a || record.seat.side_b != view.side_b)
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "seat candidates do not match bracket"};
                return false;
            }
            if (record.seat != seat_for(record.game_index, view.side_a, view.side_b))
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "seat assignment is not the deterministic alternation"};
                return false;
            }
            auto const seeds = player_seeds_for(generation_seed_, record.series_id, record.game_index);
            if (seeds.first != record.seed_player_one || seeds.second != record.seed_player_two)
            {
                error_ = RunnerError{ErrorCode::SeedMismatch, record.series_id, record.game_id,
                                     "recorded seeds do not match deterministic derivation"};
                return false;
            }
            if (!reason_matches_winner(record.reason, record.winner) || record.rounds < 0)
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "recorded outcome is inconsistent"};
                return false;
            }
            if (bracket_->report_game(record.series_id, record.game_index, record.winner)
                != tournament_bracket::ReportStatus::Accepted)
            {
                error_ = RunnerError{ErrorCode::LedgerCorrupt, record.series_id, record.game_id,
                                     "bracket rejected replayed game"};
                return false;
            }
            ledger_.push_back(record);
            ++totals_.games;
            if (record.winner == GameWinner::Draw)
            {
                ++totals_.draws;
            }
            return true;
        }

        bool absorb_stopped_wave(std::vector<GameRecord> &planned,
                                 tuning::BackendStopped const &stop, RunResult &result)
        {
            std::unordered_map<std::uint64_t, std::size_t> by_game_id;
            by_game_id.reserve(planned.size());
            for (std::size_t i = 0; i < planned.size(); ++i)
            {
                by_game_id.emplace(planned[i].game_id, i);
            }
            std::int64_t draws = 0;
            std::vector<bool> filled(planned.size(), false);
            for (auto const &[game_id, outcome] : stop.completed())
            {
                auto const it = by_game_id.find(game_id);
                if (it == by_game_id.end())
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, -1, game_id,
                                         "stopped backend returned an unknown game id"};
                    return false;
                }
                if (filled[it->second])
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, planned[it->second].series_id,
                                         game_id, "stopped backend returned a duplicate game"};
                    return false;
                }
                GameRecord &record = planned[it->second];
                if (outcome.winner < -1 || outcome.winner > 1)
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, record.series_id,
                                         record.game_id, "outcome winner is outside the supported domain"};
                    return false;
                }
                if (outcome.rounds < 0)
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, record.series_id,
                                         record.game_id, "outcome rounds is negative"};
                    return false;
                }
                GameWinner const backend_winner = normalize_outcome(outcome.winner, true);
                if (!reason_matches_winner(outcome.reason, backend_winner))
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, record.series_id,
                                         record.game_id, "outcome reason does not match winner"};
                    return false;
                }
                record.winner = normalize_outcome(outcome.winner, record.seat.side_a_is_player_one);
                record.reason = normalize_reason(outcome.reason, record.seat.side_a_is_player_one);
                record.rounds = outcome.rounds;
                filled[it->second] = true;
            }
            std::map<int, std::vector<GameRecord const *>> by_series;
            for (GameRecord const &record : planned)
            {
                by_series[record.series_id].push_back(&record);
            }
            std::vector<GameRecord const *> report_order;
            for (auto &[series_id, records] : by_series)
            {
                std::sort(records.begin(), records.end(),
                          [](GameRecord const *a, GameRecord const *b)
                          {
                              return a->game_index < b->game_index;
                          });
                for (GameRecord const *record : records)
                {
                    std::size_t const position = static_cast<std::size_t>(record - planned.data());
                    if (!filled[position])
                    {
                        break;
                    }
                    report_order.push_back(record);
                }
            }
            std::sort(report_order.begin(), report_order.end(),
                      [](GameRecord const *a, GameRecord const *b)
                      {
                          if (a->series_id != b->series_id)
                          {
                              return a->series_id < b->series_id;
                          }
                          return a->game_index < b->game_index;
                      });
            for (GameRecord const *record : report_order)
            {
                if (bracket_->report_game(record->series_id, record->game_index, record->winner)
                    != tournament_bracket::ReportStatus::Accepted)
                {
                    error_ = RunnerError{ErrorCode::BracketRejected, record->series_id, record->game_id,
                                         "bracket rejected a stopped wave outcome"};
                    return false;
                }
                ledger_.push_back(*record);
                if (record->winner == GameWinner::Draw)
                {
                    ++draws;
                }
            }
            totals_.games += static_cast<std::int64_t>(report_order.size());
            totals_.draws += draws;
            error_ = RunnerError{ErrorCode::BackendStopped, -1, 0, stop.what()};
            result.error = error_;
            return false;
        }

        bool execute_wave(std::vector<tournament_scheduler::WaveSlot> const &wave,
                          std::vector<SeriesView> const &views, RunResult &result)
        {
            std::unordered_map<int, SeriesView const *> by_id;
            by_id.reserve(views.size());
            for (SeriesView const &view : views)
            {
                by_id.emplace(view.id, &view);
            }
            std::vector<tuning::BatchGame> games;
            std::vector<GameRecord> planned;
            games.reserve(wave.size());
            planned.reserve(wave.size());
            for (tournament_scheduler::WaveSlot const &slot : wave)
            {
                auto const it = by_id.find(static_cast<int>(slot.series_id));
                if (it == by_id.end())
                {
                    error_ = RunnerError{ErrorCode::StalledTournament, -1, 0, "wave references unknown series"};
                    return false;
                }
                SeriesView const &view = *it->second;
                int const game_index = view.games_played + slot.slot;
                if (game_index < 0 || static_cast<std::uint64_t>(game_index) > 0xFFFFFFFFULL)
                {
                    error_ = RunnerError{ErrorCode::GameIndexOverflow, view.id, 0, "game index exceeds composite id range"};
                    return false;
                }
                GameRecord record;
                record.series_id = view.id;
                record.game_index = game_index;
                record.game_id = game_id_for(view.id, game_index);
                record.seat = seat_for(game_index, view.side_a, view.side_b);
                auto const seeds = player_seeds_for(generation_seed_, view.id, game_index);
                record.seed_player_one = seeds.first;
                record.seed_player_two = seeds.second;
                CandidateId const player_one = record.seat.side_a_is_player_one ? record.seat.side_a : record.seat.side_b;
                CandidateId const player_two = record.seat.side_a_is_player_one ? record.seat.side_b : record.seat.side_a;
                tuning::BatchGame game;
                game.id = record.game_id;
                game.theta_a = theta_of(player_one);
                game.theta_b = theta_of(player_two);
                game.seed_a = seeds.first;
                game.seed_b = seeds.second;
                if (!backend_.validate(game.theta_a) || !backend_.validate(game.theta_b))
                {
                    error_ = RunnerError{ErrorCode::BackendRejectedTheta, view.id, record.game_id,
                                         "backend rejected theta for a bracket candidate"};
                    return false;
                }
                games.push_back(std::move(game));
                planned.push_back(record);
            }
            std::vector<tuning::GameOutcome> outcomes;
            try
            {
                outcomes = backend_.run_games(games, run_config_);
            }
            catch (tuning::BackendStopped const &stop)
            {
                return absorb_stopped_wave(planned, stop, result);
            }
            catch (std::exception const &e)
            {
                error_ = RunnerError{ErrorCode::BackendFailed, -1, 0, e.what()};
                return false;
            }
            catch (...)
            {
                error_ = RunnerError{ErrorCode::BackendFailed, -1, 0, "backend raised an unknown exception"};
                return false;
            }
            if (outcomes.size() != games.size())
            {
                error_ = RunnerError{ErrorCode::BackendIncomplete, -1, 0, "outcome count does not match request"};
                return false;
            }
            for (std::size_t i = 0; i < outcomes.size(); ++i)
            {
                if (outcomes[i].id != games[i].id)
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, planned[i].series_id, games[i].id,
                                         "outcome id does not match request order"};
                    return false;
                }
                if (outcomes[i].winner < -1 || outcomes[i].winner > 1)
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, planned[i].series_id, games[i].id,
                                         "outcome winner is outside the supported domain"};
                    return false;
                }
                if (outcomes[i].rounds < 0)
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, planned[i].series_id, games[i].id,
                                         "outcome rounds is negative"};
                    return false;
                }
                GameWinner const backend_winner = normalize_outcome(outcomes[i].winner, true);
                if (!reason_matches_winner(outcomes[i].reason, backend_winner))
                {
                    error_ = RunnerError{ErrorCode::BackendMalformedOutcome, planned[i].series_id, games[i].id,
                                         "outcome reason does not match winner"};
                    return false;
                }
                planned[i].winner = normalize_outcome(outcomes[i].winner, planned[i].seat.side_a_is_player_one);
                planned[i].reason = normalize_reason(outcomes[i].reason, planned[i].seat.side_a_is_player_one);
                planned[i].rounds = outcomes[i].rounds;
            }
            std::vector<GameRecord const *> report_order;
            report_order.reserve(planned.size());
            for (GameRecord const &record : planned)
            {
                report_order.push_back(&record);
            }
            std::sort(report_order.begin(), report_order.end(),
                      [](GameRecord const *a, GameRecord const *b)
                      {
                          if (a->series_id != b->series_id)
                          {
                              return a->series_id < b->series_id;
                          }
                          return a->game_index < b->game_index;
                      });
            for (GameRecord const *record : report_order)
            {
                if (bracket_->report_game(record->series_id, record->game_index, record->winner)
                    != tournament_bracket::ReportStatus::Accepted)
                {
                    error_ = RunnerError{ErrorCode::BracketRejected, record->series_id, record->game_id,
                                         "bracket rejected a scheduled outcome"};
                    return false;
                }
            }
            std::int64_t draws = 0;
            for (GameRecord const &record : planned)
            {
                ledger_.push_back(record);
                if (record.winner == GameWinner::Draw)
                {
                    ++draws;
                }
            }
            totals_.games += static_cast<std::int64_t>(planned.size());
            totals_.draws += draws;
            ++totals_.waves;
            result.stats.games = static_cast<std::int64_t>(planned.size());
            result.stats.draws = draws;
            result.stats.waves = 1;
            if (limits_.max_draws > 0 && totals_.draws > limits_.max_draws)
            {
                error_ = RunnerError{ErrorCode::DrawLimitExceeded, -1, 0, "draw limit reached"};
                return false;
            }
            return true;
        }

        Backend backend_;
        std::vector<RosterEntry> roster_;
        std::uint64_t generation_seed_;
        tuning::RunConfig run_config_;
        RunLimits limits_;
        std::optional<Bracket> bracket_;
        std::vector<GameRecord> ledger_;
        Totals totals_;
        RunnerError error_{};
    };
}
