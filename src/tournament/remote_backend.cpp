#include "tournament/remote_backend.h"

#include <algorithm>
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
        , nonce_counter_(std::make_shared<std::atomic<std::uint64_t>>(0))
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
        std::unordered_map<tournament_wire::DeviceId, std::unordered_map<int, int>> series_load;
        for (tournament_provenance::ProvenanceRecord const *entry : provenance_->entries())
        {
            ++series_load[entry->device][series_of(entry->game_id)];
        }

        std::size_t const chunk_size = static_cast<std::size_t>(config_.games_per_assignment);
        int round = 0;
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
            if (round > config_.max_assignment_rounds)
            {
                throw std::runtime_error("remote execution exhausted "
                    + std::to_string(config_.max_assignment_rounds) + " assignment rounds with "
                    + std::to_string(pending.size()) + " unresolved games");
            }
            std::vector<tournament_wire::DeviceId> devices = registry_->active_devices();
            if (devices.empty())
            {
                throw std::runtime_error("no active devices for remote execution");
            }

            struct Flight
            {
                std::vector<std::size_t> indices;
                tournament_wire::AssignmentBatch assignment;
                tournament_wire::DeviceId device = 0;
                tournament_transport::Delivery delivery{};
                std::uint64_t assigned_at_ms = 0;
            };
            std::vector<Flight> flights;
            for (std::size_t start = 0; start < pending.size(); start += chunk_size)
            {
                Flight flight;
                for (std::size_t k = start; k < pending.size() && k < start + chunk_size; ++k)
                {
                    flight.indices.push_back(pending[k]);
                }
                flights.push_back(std::move(flight));
            }

            std::size_t cursor = 0;
            for (Flight &flight : flights)
            {
                auto unfailed = [&](tournament_wire::DeviceId device)
                {
                    for (std::size_t index : flight.indices)
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
                    for (std::size_t index : flight.indices)
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
                std::size_t chosen = devices.size();
                for (std::size_t step = 0; step < devices.size(); ++step)
                {
                    std::size_t const candidate = (cursor + step) % devices.size();
                    if (unfailed(devices[candidate]) && within_series_cap(devices[candidate]))
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
                        if (unfailed(devices[candidate]))
                        {
                            chosen = candidate;
                            break;
                        }
                    }
                }
                if (chosen == devices.size())
                {
                    chosen = cursor % devices.size();
                }
                cursor = (chosen + 1) % devices.size();
                flight.device = devices[chosen];
                flight.assignment.nonce = next_nonce();
                flight.assignment.device = flight.device;
                flight.assignment.config = config;
                for (std::size_t index : flight.indices)
                {
                    flight.assignment.games.push_back(tournament_wire::to_wire(games[index]));
                }
            }

            std::vector<std::thread> workers;
            workers.reserve(flights.size());
            for (Flight &flight : flights)
            {
                flight.assigned_at_ms = clock_->now_ms();
                workers.emplace_back([this, &flight]()
                {
                    flight.delivery = transport_->request(flight.device, flight.assignment);
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
                std::uint64_t const now = clock_->now_ms();
                bool const expired = now > flight.assigned_at_ms + config_.lease_ms;
                if (accepted && !expired)
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
                        record.accepted_at_ms = now;
                        provenance_->record(std::move(record));
                        results[index] = tournament_wire::from_wire(outcome);
                        done[index] = true;
                        ++series_load[flight.device][series_of(games[index].id)];
                    }
                    registry_->record_accepted(flight.device, attempted);
                }
                else
                {
                    registry_->record_dropped(flight.device, attempted);
                    for (std::size_t index : flight.indices)
                    {
                        failed_on[games[index].id].insert(flight.device);
                    }
                }
            }
        }
        return results;
    }
}
