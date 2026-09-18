#include "tournament/remote_backend.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "tournament/runner.h"

namespace tournament_remote
{
    namespace
    {
        int series_of(tournament_wire::GameId game_id)
        {
            int series_id = 0;
            int game_index = 0;
            tournament_runner::decode_game_id(game_id, series_id, game_index);
            return series_id;
        }

        bool outcome_shape_valid(tournament_wire::WireOutcome const &outcome)
        {
            if (outcome.winner < -1 || outcome.winner > 1)
            {
                return false;
            }
            if (outcome.rounds < 0)
            {
                return false;
            }
            tournament_bracket::GameWinner const winner
                = tournament_runner::normalize_outcome(outcome.winner, true);
            return tournament_runner::reason_matches_winner(outcome.reason, winner);
        }

        std::string delivery_status_name(tournament_transport::DeliveryStatus status)
        {
            switch (status)
            {
            case tournament_transport::DeliveryStatus::Delivered:
                return "result failed verification";
            case tournament_transport::DeliveryStatus::Unreachable:
                return "device unreachable";
            case tournament_transport::DeliveryStatus::Timeout:
                return "no result before the wait window closed";
            case tournament_transport::DeliveryStatus::Malformed:
                return "malformed reply";
            }
            return "unknown delivery status";
        }
    }

    void DeviceTiming::seed_ms_per_game(double ms_per_game)
    {
        if (ms_per_game <= 0.0 || !std::isfinite(ms_per_game))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        seeded_ms_per_game_ = ms_per_game;
    }

    void DeviceTiming::record_dispatch(DeviceId device, std::uint64_t games)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_[device].outstanding_games += games;
    }

    void DeviceTiming::record_delivery(DeviceId device, std::uint64_t games, std::uint64_t elapsed_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DeviceState &state = devices_[device];
        state.silent = false;
        state.last_wait_ms = 0;
        state.outstanding_games = state.outstanding_games > games ? state.outstanding_games - games : 0;
        if (games == 0 || elapsed_ms == 0)
        {
            return;
        }
        double const sample = static_cast<double>(elapsed_ms) / static_cast<double>(games);
        if (!std::isfinite(sample) || sample <= 0.0)
        {
            return;
        }
        state.ms_per_game = state.ms_per_game > 0.0 ? state.ms_per_game * 0.5 + sample * 0.5 : sample;
    }

    void DeviceTiming::record_timeout(DeviceId device, bool peer_active, std::uint64_t waited_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DeviceState &state = devices_[device];
        if (peer_active)
        {
            state.silent = false;
            state.last_wait_ms = waited_ms;
        }
        else
        {
            state.silent = true;
            state.last_wait_ms = 0;
        }
    }

    void DeviceTiming::record_reset(DeviceId device)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DeviceState &state = devices_[device];
        state.outstanding_games = 0;
        state.last_wait_ms = 0;
        state.silent = false;
    }

    std::uint64_t DeviceTiming::wait_hint_ms(DeviceId device, std::uint64_t games,
                                             std::uint64_t lease_ms) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        double ms_per_game = seeded_ms_per_game_;
        std::uint64_t outstanding = 0;
        std::uint64_t last_wait = 0;
        bool silent = false;
        auto const it = devices_.find(device);
        if (it != devices_.end())
        {
            if (it->second.ms_per_game > 0.0)
            {
                ms_per_game = it->second.ms_per_game;
            }
            outstanding = it->second.outstanding_games;
            last_wait = it->second.last_wait_ms;
            silent = it->second.silent;
        }
        if (silent)
        {
            return lease_ms;
        }
        std::uint64_t wait = lease_ms;
        if (ms_per_game > 0.0)
        {
            double const needed = ms_per_game * static_cast<double>(games + outstanding) * 2.0 + 60000.0;
            if (needed < 3600000.0)
            {
                wait = static_cast<std::uint64_t>(needed);
            }
            else
            {
                wait = 3600000;
            }
        }
        else if (last_wait > 0)
        {
            wait = last_wait > 1800000 ? 3600000 : last_wait * 2;
        }
        return std::min<std::uint64_t>(std::max<std::uint64_t>(wait, lease_ms), 3600000);
    }

    RemoteBackend::RemoteBackend(tuning::ParamSchema schema,
                                 std::shared_ptr<tournament_transport::Transport> transport,
                                 std::shared_ptr<tournament_registry::DeviceRegistry> registry,
                                 std::shared_ptr<tournament_provenance::ProvenanceLedger> provenance,
                                 std::shared_ptr<tournament_transport::Clock> clock,
                                 RemoteConfig config)
        : schema_(schema)
        , transport_(std::move(transport))
        , registry_(std::move(registry))
        , provenance_(std::move(provenance))
        , clock_(std::move(clock))
        , config_(config)
        , timing_(config.timing ? config.timing : std::make_shared<DeviceTiming>())
        , nonce_counter_(std::make_shared<std::atomic<std::uint64_t>>(0))
        , audited_ids_(std::make_shared<std::unordered_set<tournament_wire::GameId>>())
    {
        if (!transport_ || !registry_ || !provenance_ || !clock_)
        {
            throw std::invalid_argument("remote backend requires transport, registry, provenance and clock");
        }
        if (config_.games_per_assignment <= 0 || config_.max_assignment_rounds <= 0 || config_.lease_ms == 0)
        {
            throw std::invalid_argument("remote backend config values must be positive");
        }
    }

    tuning::ParamSchema RemoteBackend::schema() const
    {
        return schema_;
    }

    bool RemoteBackend::validate(std::vector<double> const &theta) const
    {
        return tuning::validate_theta(schema_, theta);
    }

    tournament_wire::Nonce RemoteBackend::next_nonce() const
    {
        std::uint64_t const counter = nonce_counter_->fetch_add(1, std::memory_order_relaxed);
        return tuning::mix64(config_.nonce_seed ^ tuning::mix64(counter + 1));
    }

    std::vector<tournament_wire::GameId> RemoteBackend::audited_game_ids() const
    {
        std::vector<tournament_wire::GameId> ids(audited_ids_->begin(), audited_ids_->end());
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    std::vector<tuning::GameOutcome> RemoteBackend::run_games(std::vector<tuning::BatchGame> const &games,
                                                              tuning::RunConfig const &config) const
    {
        if (!tuning::valid_run_config(config))
        {
            throw std::invalid_argument("invalid run config");
        }
        if (games.empty())
        {
            return {};
        }
        for (std::size_t i = 0; i < games.size(); ++i)
        {
            if (!validate(games[i].theta_a) || !validate(games[i].theta_b))
            {
                throw std::invalid_argument("invalid theta for game index " + std::to_string(i));
            }
        }

        std::size_t const total = games.size();
        std::vector<tuning::GameOutcome> results(total);
        std::vector<bool> done(total, false);
        std::unordered_map<tournament_wire::GameId, std::unordered_set<tournament_wire::DeviceId>> failed_on;
        std::unordered_set<tournament_wire::DeviceId> caught_devices;
        std::unordered_map<tournament_wire::DeviceId, std::unordered_map<int, int>> series_load;
        for (tournament_provenance::ProvenanceRecord const *entry : provenance_->entries())
        {
            ++series_load[entry->device][series_of(entry->game_id)];
        }

        std::size_t const chunk_size = static_cast<std::size_t>(config_.games_per_assignment);
        int round = 0;
        int rounds_limit = 0;
        for (;;)
        {
            std::vector<std::size_t> pending;
            pending.reserve(total);
            for (std::size_t i = 0; i < total; ++i)
            {
                if (!done[i])
                {
                    pending.push_back(i);
                }
            }
            if (pending.empty())
            {
                break;
            }
            ++round;
            std::vector<tournament_wire::DeviceId> devices = registry_->active_devices();
            if (devices.empty())
            {
                throw std::runtime_error("no active devices for remote execution");
            }
            std::uint64_t capacity_total = 0;
            for (tournament_wire::DeviceId device : devices)
            {
                capacity_total += std::max<std::uint32_t>(1u, registry_->concurrency(device));
            }
            if (rounds_limit == 0)
            {
                std::uint64_t const flights_needed = (static_cast<std::uint64_t>(total) + chunk_size - 1)
                    / chunk_size;
                std::uint64_t const capacity = std::max<std::uint64_t>(capacity_total, 1);
                rounds_limit = config_.max_assignment_rounds
                    + static_cast<int>((flights_needed + capacity - 1) / capacity);
            }
            if (round > rounds_limit)
            {
                throw std::runtime_error("remote execution exhausted "
                    + std::to_string(rounds_limit) + " assignment rounds with "
                    + std::to_string(pending.size()) + " unresolved games");
            }
            if (config_.log)
            {
                config_.log("round " + std::to_string(round) + ": " + std::to_string(pending.size())
                    + " games pending across " + std::to_string(devices.size()) + " device(s)");
            }

            struct Flight
            {
                std::vector<std::size_t> indices;
                tournament_wire::AssignmentBatch assignment;
                tournament_wire::DeviceId device = 0;
                tournament_transport::Delivery delivery{};
                std::uint64_t assigned_at_ms = 0;
                std::uint64_t completed_at_ms = 0;
                std::uint64_t wait_ms = 0;
                bool audit_selected = false;
                bool audit_error = false;
                std::vector<tuning::GameOutcome> audit_outcomes;
            };
            std::vector<std::uint32_t> capacities(devices.size(), 1);
            for (std::size_t i = 0; i < devices.size(); ++i)
            {
                capacities[i] = std::max<std::uint32_t>(1u, registry_->concurrency(devices[i]));
            }
            std::vector<std::uint32_t> flights_used(devices.size(), 0);

            std::vector<Flight> flights;
            std::size_t cursor = 0;
            for (std::size_t start = 0; start < pending.size(); start += chunk_size)
            {
                std::vector<std::size_t> indices;
                for (std::size_t k = start; k < pending.size() && k < start + chunk_size; ++k)
                {
                    indices.push_back(pending[k]);
                }
                auto unfailed = [&](tournament_wire::DeviceId device)
                {
                    for (std::size_t index : indices)
                    {
                        auto const it = failed_on.find(games[index].id);
                        if (it != failed_on.end() && it->second.count(device) != 0)
                        {
                            return false;
                        }
                    }
                    return true;
                };
                auto within_series_cap = [&](tournament_wire::DeviceId device)
                {
                    auto const device_it = series_load.find(device);
                    if (device_it == series_load.end())
                    {
                        return true;
                    }
                    for (std::size_t index : indices)
                    {
                        auto const series_it = device_it->second.find(series_of(games[index].id));
                        if (series_it != device_it->second.end()
                            && series_it->second >= config_.per_series_device_cap)
                        {
                            return false;
                        }
                    }
                    return true;
                };
                auto within_flight_cap = [&](std::size_t candidate)
                {
                    return flights_used[candidate] < capacities[candidate];
                };
                std::size_t chosen = devices.size();
                for (std::size_t step = 0; step < devices.size(); ++step)
                {
                    std::size_t const candidate = (cursor + step) % devices.size();
                    if (unfailed(devices[candidate]) && within_series_cap(devices[candidate])
                        && within_flight_cap(candidate))
                    {
                        chosen = candidate;
                        break;
                    }
                }
                if (chosen == devices.size())
                {
                    for (std::size_t step = 0; step < devices.size(); ++step)
                    {
                        std::size_t const candidate = (cursor + step) % devices.size();
                        if (unfailed(devices[candidate]) && within_flight_cap(candidate))
                        {
                            chosen = candidate;
                            break;
                        }
                    }
                }
                if (chosen == devices.size())
                {
                    std::size_t unfailed_candidate = devices.size();
                    for (std::size_t step = 0; step < devices.size(); ++step)
                    {
                        std::size_t const candidate = (cursor + step) % devices.size();
                        if (unfailed(devices[candidate]))
                        {
                            unfailed_candidate = candidate;
                            break;
                        }
                    }
                    if (unfailed_candidate == devices.size())
                    {
                        chosen = cursor % devices.size();
                    }
                    else if (round == rounds_limit)
                    {
                        chosen = unfailed_candidate;
                    }
                    else
                    {
                        continue;
                    }
                }
                cursor = (chosen + 1) % devices.size();
                ++flights_used[chosen];
                Flight flight;
                flight.indices = std::move(indices);
                flight.device = devices[chosen];
                flight.assignment.nonce = next_nonce();
                flight.assignment.device = flight.device;
                flight.assignment.config = config;
                for (std::size_t index : flight.indices)
                {
                    flight.assignment.games.push_back(tournament_wire::to_wire(games[index]));
                }
                flight.audit_selected = config_.auditor != nullptr;
                if (flight.audit_selected)
                {
                    tournament_registry::DeviceStats const *stats = registry_->stats(flight.device);
                    double const rate = (stats != nullptr && stats->audits_passed > 0)
                        ? config_.audit_rate
                        : 1.0;
                    if (rate <= 0.0)
                    {
                        flight.audit_selected = false;
                    }
                    else if (rate < 1.0)
                    {
                        std::mt19937_64 engine(flight.assignment.nonce ^ 0xA0D17EED5ULL);
                        std::uint64_t const threshold = static_cast<std::uint64_t>(rate * 1000000.0);
                        flight.audit_selected = engine() % 1000000ULL < threshold;
                    }
                }
                flight.wait_ms = timing_->wait_hint_ms(flight.device, flight.indices.size(), config_.lease_ms);
                timing_->record_dispatch(flight.device, flight.indices.size());
                flights.push_back(std::move(flight));
            }

            std::vector<std::thread> workers;
            workers.reserve(flights.size());
            for (Flight &flight : flights)
            {
                flight.assigned_at_ms = clock_->now_ms();
                workers.emplace_back([this, &flight, &config]()
                {
                    flight.delivery = transport_->request(flight.device, flight.assignment, flight.wait_ms);
                    flight.completed_at_ms = clock_->now_ms();
                    if (flight.audit_selected
                        && flight.delivery.status == tournament_transport::DeliveryStatus::Delivered)
                    {
                        try
                        {
                            std::vector<tuning::BatchGame> audit_games;
                            audit_games.reserve(flight.assignment.games.size());
                            for (tournament_wire::WireGame const &game : flight.assignment.games)
                            {
                                audit_games.push_back(tournament_wire::from_wire(game));
                            }
                            flight.audit_outcomes = config_.auditor(audit_games, config);
                            if (flight.audit_outcomes.size() != flight.assignment.games.size())
                            {
                                flight.audit_outcomes.clear();
                                flight.audit_error = true;
                            }
                        }
                        catch (std::exception const &)
                        {
                            flight.audit_outcomes.clear();
                            flight.audit_error = true;
                        }
                    }
                });
            }
            for (std::thread &worker : workers)
            {
                worker.join();
            }

            for (Flight &flight : flights)
            {
                int const attempted = static_cast<int>(flight.indices.size());
                bool accepted = flight.delivery.status == tournament_transport::DeliveryStatus::Delivered;
                if (accepted)
                {
                    tournament_wire::PublicKey const *public_key = registry_->public_key(flight.device);
                    accepted = public_key != nullptr
                        && tournament_wire::verify_result(*public_key, flight.delivery.result.batch,
                                                          flight.delivery.result.signature)
                        && flight.delivery.result.batch.nonce == flight.assignment.nonce
                        && flight.delivery.result.batch.device == flight.device
                        && flight.delivery.result.batch.outcomes.size() == flight.assignment.games.size();
                }
                if (accepted)
                {
                    for (std::size_t i = 0; i < flight.delivery.result.batch.outcomes.size(); ++i)
                    {
                        tournament_wire::WireOutcome const &outcome = flight.delivery.result.batch.outcomes[i];
                        if (outcome.id != flight.assignment.games[i].id || !outcome_shape_valid(outcome))
                        {
                            accepted = false;
                            break;
                        }
                    }
                }
                bool audit_passed = false;
                bool audit_failed = false;
                if (flight.audit_selected && !flight.audit_error
                    && flight.delivery.status == tournament_transport::DeliveryStatus::Delivered
                    && flight.audit_outcomes.size() == flight.assignment.games.size())
                {
                    bool ids_match = true;
                    for (std::size_t i = 0; i < flight.audit_outcomes.size(); ++i)
                    {
                        if (flight.audit_outcomes[i].id != flight.assignment.games[i].id)
                        {
                            ids_match = false;
                            break;
                        }
                    }
                    if (!ids_match)
                    {
                        flight.audit_error = true;
                    }
                    else
                    {
                        audit_passed = true;
                        for (std::size_t i = 0; i < flight.audit_outcomes.size(); ++i)
                        {
                            if (tournament_wire::encode_outcome(flight.delivery.result.batch.outcomes[i])
                                != tournament_wire::encode_outcome(
                                    tournament_wire::to_wire(flight.audit_outcomes[i])))
                            {
                                audit_passed = false;
                                audit_failed = true;
                                break;
                            }
                        }
                    }
                }
                if (flight.audit_error && config_.log)
                {
                    config_.log("device " + std::to_string(flight.device) + " nonce "
                        + std::to_string(flight.assignment.nonce) + " audit re-run failed");
                }
                std::uint64_t const elapsed_ms = flight.completed_at_ms > flight.assigned_at_ms
                    ? flight.completed_at_ms - flight.assigned_at_ms
                    : 0;
                bool const expired = elapsed_ms > flight.wait_ms;
                bool const caught_lying = accepted && !expired && audit_failed;
                if (caught_lying)
                {
                    for (std::size_t index : flight.indices)
                    {
                        audited_ids_->insert(games[index].id);
                    }
                }
                if (flight.delivery.status == tournament_transport::DeliveryStatus::Delivered)
                {
                    if (accepted && !expired)
                    {
                        timing_->record_delivery(flight.device, static_cast<std::uint64_t>(attempted),
                                                 elapsed_ms);
                    }
                    else if (accepted && expired)
                    {
                        timing_->record_timeout(flight.device, true, flight.wait_ms);
                    }
                    else
                    {
                        timing_->record_reset(flight.device);
                    }
                }
                else if (flight.delivery.status == tournament_transport::DeliveryStatus::Timeout)
                {
                    timing_->record_timeout(flight.device, flight.delivery.peer_active, flight.wait_ms);
                }
                else
                {
                    timing_->record_reset(flight.device);
                }
                if (accepted && !expired && !caught_lying)
                {
                    for (std::size_t i = 0; i < flight.indices.size(); ++i)
                    {
                        tournament_wire::WireOutcome const &outcome = flight.delivery.result.batch.outcomes[i];
                        std::size_t const index = flight.indices[i];
                        tournament_provenance::ProvenanceRecord record;
                        record.game_id = games[index].id;
                        record.device = flight.device;
                        record.nonce = flight.assignment.nonce;
                        record.signature = flight.delivery.result.signature;
                        record.game = flight.assignment.games[i];
                        record.reported = outcome;
                        record.assigned_at_ms = flight.assigned_at_ms;
                        record.accepted_at_ms = flight.completed_at_ms;
                        provenance_->record(std::move(record));
                        results[index] = tournament_wire::from_wire(outcome);
                        done[index] = true;
                        ++series_load[flight.device][series_of(games[index].id)];
                    }
                    registry_->record_accepted(flight.device, attempted);
                    if (audit_passed)
                    {
                        registry_->record_audit(flight.device, true);
                        for (std::size_t index : flight.indices)
                        {
                            audited_ids_->insert(games[index].id);
                        }
                    }
                }
                else
                {
                    registry_->record_dropped(flight.device, attempted);
                    for (std::size_t index : flight.indices)
                    {
                        failed_on[games[index].id].insert(flight.device);
                    }
                    if (caught_lying)
                    {
                        registry_->record_audit(flight.device, false);
                        if (caught_devices.insert(flight.device).second && config_.on_liar)
                        {
                            config_.on_liar(flight.device);
                        }
                    }
                    if (config_.log)
                    {
                        std::string const why = caught_lying
                            ? std::string("result failed the streaming audit")
                            : (flight.delivery.status != tournament_transport::DeliveryStatus::Delivered
                                   ? delivery_status_name(flight.delivery.status)
                                   : (expired
                                         ? "result arrived " + std::to_string(elapsed_ms)
                                               + " ms after dispatch, past the "
                                               + std::to_string(flight.wait_ms) + " ms window"
                                         : delivery_status_name(flight.delivery.status)));
                        config_.log("device " + std::to_string(flight.device) + " nonce "
                            + std::to_string(flight.assignment.nonce) + " dropped: " + why);
                    }
                }
            }
        }
        return results;
    }
}
