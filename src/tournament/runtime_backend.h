#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "tuning/domain.h"
#include "tuning/match.h"

namespace tournament_runtime
{
    class RuntimeBackend
    {
    public:
        using RunGames = std::function<std::vector<tuning::GameOutcome>(
            std::vector<tuning::BatchGame> const &, tuning::RunConfig const &)>;

        RuntimeBackend(tuning::ParamSchema schema, RunGames run_games)
            : schema_(schema)
            , run_games_(std::move(run_games))
        {
        }

        tuning::ParamSchema schema() const
        {
            return schema_;
        }

        bool validate(std::vector<double> const &theta) const
        {
            return tuning::validate_theta(schema_, theta);
        }

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const &games,
                                                   tuning::RunConfig const &config) const
        {
            return run_games_(games, config);
        }

    private:
        tuning::ParamSchema schema_;
        RunGames run_games_;
    };
}
