#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/transport.h"
#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/match.h"

namespace tournament_remote
{
    struct RemoteConfig
    {
        int games_per_assignment = 4;
        std::uint64_t lease_ms = 30000;
        int max_assignment_rounds = 4;
        int per_series_device_cap = 2;
        std::uint64_t nonce_seed = 0x5177ED5EEDC0FFEEULL;
    };

    class RemoteBackend
    {
    public:
        RemoteBackend(tuning::ParamSchema schema,
                      std::shared_ptr<tournament_transport::Transport> transport,
                      std::shared_ptr<tournament_registry::DeviceRegistry> registry,
                      std::shared_ptr<tournament_provenance::ProvenanceLedger> provenance,
                      std::shared_ptr<tournament_transport::Clock> clock,
                      RemoteConfig config = RemoteConfig());

        tuning::ParamSchema schema() const;
        bool validate(std::vector<double> const &theta) const;

        std::vector<tuning::GameOutcome> run_games(std::vector<tuning::BatchGame> const &games,
                                                   tuning::RunConfig const &config) const;

    private:
        tournament_wire::Nonce next_nonce() const;

        tuning::ParamSchema schema_;
        std::shared_ptr<tournament_transport::Transport> transport_;
        std::shared_ptr<tournament_registry::DeviceRegistry> registry_;
        std::shared_ptr<tournament_provenance::ProvenanceLedger> provenance_;
        std::shared_ptr<tournament_transport::Clock> clock_;
        RemoteConfig config_;
        mutable std::shared_ptr<std::atomic<std::uint64_t>> nonce_counter_;
    };
}
