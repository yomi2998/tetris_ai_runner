#include "tournament_runner.h"

#include <algorithm>
#include <utility>

namespace tournament_runner
{
    namespace
    {
        constexpr std::uint64_t kIndexBits = 32;
        constexpr std::uint64_t kIndexMask = (1ULL << kIndexBits) - 1;
        constexpr std::uint64_t kScenarioDomainBit = 1ULL << 63;
    }

    std::uint64_t game_id_for(int series_id, int game_index)
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(series_id)) << kIndexBits)
             | static_cast<std::uint64_t>(static_cast<std::uint32_t>(game_index));
    }

    void decode_game_id(std::uint64_t game_id, int &series_id, int &game_index)
    {
        series_id = static_cast<int>(game_id >> kIndexBits);
        game_index = static_cast<int>(game_id & kIndexMask);
    }

    std::uint64_t series_scenario_tag(int series_id, int game_index)
    {
        return game_id_for(series_id, game_index / 2) | kScenarioDomainBit;
    }

    bool side_a_is_player_one(int game_index)
    {
        return game_index % 2 == 0;
    }

    SeatAssignment seat_for(int game_index, CandidateId side_a, CandidateId side_b)
    {
        SeatAssignment seat;
        seat.side_a = side_a;
        seat.side_b = side_b;
        seat.side_a_is_player_one = side_a_is_player_one(game_index);
        return seat;
    }

    std::pair<std::uint64_t, std::uint64_t> player_seeds_for(std::uint64_t generation_seed, int series_id, int game_index)
    {
        std::uint64_t const scenario = series_scenario_tag(series_id, game_index);
        std::uint64_t const first = tuning::derive_game_seed(generation_seed, scenario, 0);
        std::uint64_t const second = tuning::derive_game_seed(generation_seed, scenario, 1);
        if (side_a_is_player_one(game_index))
        {
            return {first, second};
        }
        return {second, first};
    }

    GameWinner normalize_outcome(int outcome_winner, bool side_a_is_player_one)
    {
        if (outcome_winner == 0)
        {
            return GameWinner::Draw;
        }
        bool const player_one_won = outcome_winner > 0;
        bool const side_a_won = side_a_is_player_one ? player_one_won : !player_one_won;
        return side_a_won ? GameWinner::SideA : GameWinner::SideB;
    }

    tuning::WinReason normalize_reason(tuning::WinReason reason, bool side_a_is_player_one)
    {
        if (side_a_is_player_one)
        {
            return reason;
        }
        switch (reason)
        {
        case tuning::WinReason::ASurvivor:
            return tuning::WinReason::BSurvivor;
        case tuning::WinReason::BSurvivor:
            return tuning::WinReason::ASurvivor;
        case tuning::WinReason::ACapApl:
            return tuning::WinReason::BCapApl;
        case tuning::WinReason::BCapApl:
            return tuning::WinReason::ACapApl;
        case tuning::WinReason::ABothDeadApl:
            return tuning::WinReason::BBothDeadApl;
        case tuning::WinReason::BBothDeadApl:
            return tuning::WinReason::ABothDeadApl;
        default:
            return reason;
        }
    }

    bool reason_matches_winner(tuning::WinReason reason, GameWinner winner)
    {
        switch (winner)
        {
        case GameWinner::SideA:
            return reason == tuning::WinReason::ASurvivor || reason == tuning::WinReason::ACapApl
                || reason == tuning::WinReason::ABothDeadApl;
        case GameWinner::SideB:
            return reason == tuning::WinReason::BSurvivor || reason == tuning::WinReason::BCapApl
                || reason == tuning::WinReason::BBothDeadApl;
        case GameWinner::Draw:
            return reason == tuning::WinReason::BothDeadDraw || reason == tuning::WinReason::CapDraw;
        }
        return false;
    }

    std::vector<tournament_scheduler::SeriesDemand> plan_demands(std::vector<SeriesView> const &ready_views)
    {
        std::vector<tournament_scheduler::SeriesDemand> demands;
        demands.reserve(ready_views.size());
        for (SeriesView const &view : ready_views)
        {
            int const capacity = tournament_scheduler::first_to_capacity(view.format.first_to, view.games_a, view.games_b);
            if (capacity <= 0)
            {
                continue;
            }
            demands.push_back(tournament_scheduler::SeriesDemand{static_cast<std::uint64_t>(view.id), capacity});
        }
        return demands;
    }

    std::vector<GameRecord> canonical_ledger(std::vector<GameRecord> const &records)
    {
        std::vector<GameRecord> ordered = records;
        std::sort(ordered.begin(), ordered.end(),
                  [](GameRecord const &a, GameRecord const &b)
                  {
                      return a.game_id < b.game_id;
                  });
        return ordered;
    }

    std::uint64_t ledger_checksum(std::vector<GameRecord> const &records)
    {
        std::uint64_t hash = tuning::kFnvOffsetBasis;
        for (GameRecord const &record : canonical_ledger(records))
        {
            hash = tuning::fnv1a_u64(hash, record.game_id);
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(record.series_id));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(record.game_index));
            hash = tuning::fnv1a_u64(hash, record.seat.side_a);
            hash = tuning::fnv1a_u64(hash, record.seat.side_b);
            hash = tuning::fnv1a_byte(hash, record.seat.side_a_is_player_one ? 1u : 0u);
            hash = tuning::fnv1a_u64(hash, record.seed_player_one);
            hash = tuning::fnv1a_u64(hash, record.seed_player_two);
            hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(static_cast<int>(record.winner)));
            hash = tuning::fnv1a_byte(hash, static_cast<unsigned char>(static_cast<int>(record.reason)));
            hash = tuning::fnv1a_u64(hash, static_cast<std::uint64_t>(record.rounds));
        }
        return hash;
    }
}
