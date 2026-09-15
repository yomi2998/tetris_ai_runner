#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "tournament/runner.h"
#include "tuning/match.h"

namespace tournament_repair
{
    using ReRun = std::function<std::vector<tuning::GameOutcome>(std::vector<tuning::BatchGame> const &, tuning::RunConfig const &)>;

    struct RepairRequest
    {
        std::vector<tournament_runner::RosterEntry> roster;
        std::uint64_t generation_seed = 0;
        tuning::RunConfig config{};
        std::vector<tournament_runner::GameRecord> ledger;
        std::vector<std::uint64_t> voided_game_ids;
        ReRun re_run;
    };

    struct RepairResult
    {
        bool ok = false;
        std::string error;
        std::vector<tournament_runner::GameRecord> repaired_ledger;
        int re_run_games = 0;
        int dropped_games = 0;
        int voided_games = 0;
    };

    RepairResult repair_ledger(RepairRequest request);
}
