#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/transport.h"
#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/match.h"

namespace tournament_remote
{
    using tournament_wire::DeviceId;

    class DeviceTiming
    {
    public:
        void seed_ms_per_game(double ms_per_game);
        void record_dispatch(DeviceId device, std::uint64_t games);
        void record_delivery(DeviceId device, std::uint64_t games, std::uint64_t elapsed_ms);
        void record_timeout(DeviceId device, bool peer_active, std::uint64_t waited_ms);
        void record_reset(DeviceId device);
        std::uint64_t wait_hint_ms(DeviceId device, std::uint64_t games, std::uint64_t lease_ms) const;

    private:
        struct DeviceState
        {
            double ms_per_game = 0.0;
            std::uint64_t outstanding_games = 0;
            std::uint64_t last_wait_ms = 0;
            bool silent = false;
        };

        mutable std::mutex mutex_;
        double seeded_ms_per_game_ = 0.0;
        std::map<DeviceId, DeviceState> devices_;
    };

    struct RemoteConfig
    {
        int games_per_assignment = 4;
        std::uint64_t lease_ms = 30000;
        int max_assignment_rounds = 4;
        int per_series_device_cap = 2;
        std::uint64_t nonce_seed = 0x5177ED5EEDC0FFEEULL;
        double audit_rate = 0.25;
        int audit_workers = 2;
        std::shared_ptr<DeviceTiming> timing;
        std::function<void(std::string const &)> log;
        std::function<std::vector<tuning::GameOutcome>(std::vector<tuning::BatchGame> const &,
                                                       tuning::RunConfig const &)> auditor;
        std::function<void(DeviceId)> on_liar;
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

        std::vector<tournament_wire::GameId> audited_game_ids() const;

    private:
        tournament_wire::Nonce next_nonce() const;

        tuning::ParamSchema schema_;
        std::shared_ptr<tournament_transport::Transport> transport_;
        std::shared_ptr<tournament_registry::DeviceRegistry> registry_;
        std::shared_ptr<tournament_provenance::ProvenanceLedger> provenance_;
        std::shared_ptr<tournament_transport::Clock> clock_;
        RemoteConfig config_;
        std::shared_ptr<DeviceTiming> timing_;
        mutable std::shared_ptr<std::atomic<std::uint64_t>> nonce_counter_;
        mutable std::shared_ptr<std::unordered_set<tournament_wire::GameId>> audited_ids_;
    };
}
