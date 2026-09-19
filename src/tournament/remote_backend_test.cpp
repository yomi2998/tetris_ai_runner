#include "tournament/audit.h"
#include "tournament/bracket.h"
#include "tournament/loopback.h"
#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/remote_backend.h"
#include "tournament/repair.h"
#include "tournament/runner.h"
#include "tournament/transport.h"
#include "tournament/wire.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace tw = tournament_wire;
    namespace tt = tournament_transport;
    namespace tl = tournament_loopback;
    namespace treg = tournament_registry;
    namespace tprov = tournament_provenance;
    namespace taud = tournament_audit;
    namespace trep = tournament_repair;
    namespace trun = tournament_runner;
    namespace trem = tournament_remote;

    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;
    using ContextPtr = std::shared_ptr<m_tetris::TetrisContext>;

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

    tuning::RunConfig fast_config()
    {
        tuning::RunConfig config;
        config.threads = 1;
        config.iterations_per_move = 8;
        config.max_rounds = 80;
        return config;
    }

    std::vector<double> theta_for(int index)
    {
        auto const schema = tuning_toj::TojAdapter::schema();
        std::vector<double> theta(schema.defaults.begin(), schema.defaults.end());
        for (double &value : theta)
        {
            value *= 1.0 + 0.05 * index;
        }
        return theta;
    }

    std::vector<trun::RosterEntry> make_roster(int count)
    {
        std::vector<trun::RosterEntry> roster;
        for (int i = 0; i < count; ++i)
        {
            roster.push_back(trun::RosterEntry{static_cast<trun::CandidateId>(i + 1), theta_for(i)});
        }
        return roster;
    }

    bool outcomes_identical(std::vector<tuning::GameOutcome> const &a, std::vector<tuning::GameOutcome> const &b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (tw::encode_outcome(tw::to_wire(a[i])) != tw::encode_outcome(tw::to_wire(b[i])))
            {
                return false;
            }
        }
        return true;
    }

    struct DeviceHarness
    {
        tw::DeviceId id = 0;
        tw::KeyPair keys{};
    };

    DeviceHarness make_device(tw::DeviceId id)
    {
        return DeviceHarness{id, tw::generate_keypair()};
    }

    struct Cluster
    {
        std::shared_ptr<tl::LoopbackTransport> transport;
        std::shared_ptr<treg::DeviceRegistry> registry;
        std::shared_ptr<tprov::ProvenanceLedger> provenance;
        std::shared_ptr<tt::ManualClock> clock;
        trem::RemoteBackend backend;

        Cluster(std::vector<DeviceHarness> devices, trem::RemoteConfig remote_config)
            : transport(std::make_shared<tl::LoopbackTransport>())
            , registry(std::make_shared<treg::DeviceRegistry>())
            , provenance(std::make_shared<tprov::ProvenanceLedger>())
            , clock(std::make_shared<tt::ManualClock>())
            , backend(tuning_toj::TojAdapter::schema(), transport, registry, provenance, clock, remote_config)
        {
            for (DeviceHarness const &device : devices)
            {
                registry->enroll(device.id, device.keys.public_key);
            }
        }
    };

    std::optional<tw::SignedResult> run_assignment_honestly(tw::KeyPair const &keys,
                                                            tw::AssignmentBatch const &assignment,
                                                            TojBackend const &engine)
    {
        std::vector<tuning::BatchGame> games;
        for (tw::WireGame const &game : assignment.games)
        {
            games.push_back(tw::from_wire(game));
        }
        std::vector<tuning::GameOutcome> outcomes = engine.run_games(games, assignment.config);
        tw::ResultBatch result;
        result.nonce = assignment.nonce;
        result.device = assignment.device;
        for (tuning::GameOutcome const &outcome : outcomes)
        {
            result.outcomes.push_back(tw::to_wire(outcome));
        }
        tw::Signature const signature = tw::sign_result(keys, result);
        return tw::SignedResult{std::move(result), std::move(signature)};
    }

    tl::DeviceHandler honest_handler(tw::KeyPair keys, TojBackend engine)
    {
        return [keys, engine](tw::AssignmentBatch const &assignment)
        {
            return run_assignment_honestly(keys, assignment, engine);
        };
    }

    tl::DeviceHandler flip_liar_handler(tw::KeyPair keys, TojBackend engine)
    {
        return [keys, engine](tw::AssignmentBatch const &assignment)
        {
            std::optional<tw::SignedResult> held = run_assignment_honestly(keys, assignment, engine);
            if (!held.has_value())
            {
                return held;
            }
            for (tw::WireOutcome &outcome : held->batch.outcomes)
            {
                if (outcome.winner == 1)
                {
                    outcome.winner = -1;
                    outcome.reason = tuning::WinReason::BSurvivor;
                }
                else if (outcome.winner == -1)
                {
                    outcome.winner = 1;
                    outcome.reason = tuning::WinReason::ASurvivor;
                }
                else
                {
                    outcome.winner = 1;
                    outcome.reason = outcome.capped ? tuning::WinReason::ACapApl
                                                    : tuning::WinReason::ABothDeadApl;
                }
            }
            held->signature = tw::sign_result(keys, held->batch);
            return held;
        };
    }

    tw::WireOutcome fabricated_outcome(tw::GameId id, int winner)
    {
        tw::WireOutcome outcome;
        outcome.id = id;
        outcome.winner = winner;
        outcome.reason = winner == 1 ? tuning::WinReason::ASurvivor : tuning::WinReason::BSurvivor;
        outcome.rounds = 12;
        return outcome;
    }

    tl::DeviceHandler fabricated_handler(tw::KeyPair keys,
                                         int winner,
                                         std::shared_ptr<tt::ManualClock> clock = nullptr)
    {
        return [keys, winner, clock](tw::AssignmentBatch const &assignment)
        {
            if (clock)
            {
                clock->advance(1);
            }
            tw::ResultBatch result;
            result.nonce = assignment.nonce;
            result.device = assignment.device;
            for (tw::WireGame const &game : assignment.games)
            {
                result.outcomes.push_back(fabricated_outcome(game.id, winner));
            }
            tw::Signature const signature = tw::sign_result(keys, result);
            return tw::SignedResult{std::move(result), std::move(signature)};
        };
    }

    std::vector<tuning::BatchGame> fabricated_games(std::uint64_t count)
    {
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= count; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_for(0);
            game.theta_b = theta_for(1);
            game.seed_a = tuning::derive_game_seed(999, id, 0);
            game.seed_b = tuning::derive_game_seed(999, id, 1);
            games.push_back(std::move(game));
        }
        return games;
    }

    bool fabricated_results_match(std::vector<tuning::GameOutcome> const &results,
                                  tprov::ProvenanceLedger const &provenance,
                                  int winner_for_device_one)
    {
        for (tprov::ProvenanceRecord const *entry : provenance.entries())
        {
            std::size_t const index = static_cast<std::size_t>(entry->game_id - 1);
            if (index >= results.size())
            {
                return false;
            }
            int const expected_winner = entry->device == 1 ? winner_for_device_one : -winner_for_device_one;
            if (tw::encode_outcome(tw::to_wire(results[index]))
                != tw::encode_outcome(fabricated_outcome(entry->game_id, expected_winner)))
            {
                return false;
            }
        }
        return true;
    }

    void test_honest_batch_matches_local()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 6; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(777, id, 0);
            game.seed_b = tuning::derive_game_seed(777, id, 1);
            games.push_back(std::move(game));
        }
        std::vector<DeviceHarness> devices{make_device(1), make_device(2), make_device(3)};
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, honest_handler(devices[0].keys, engine));
        cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
        cluster.transport->add_device(3, honest_handler(devices[2].keys, engine));

        std::vector<tuning::GameOutcome> const remote
            = cluster.backend.run_games(games, fast_config());
        std::vector<tuning::GameOutcome> const local = engine.run_games(games, fast_config());
        check(outcomes_identical(remote, local), "honest cluster outcomes identical to local engine");
        check(cluster.provenance->size() == 6, "honest cluster records full provenance");
        bool reported_matches = true;
        for (std::size_t i = 0; i < local.size(); ++i)
        {
            tprov::ProvenanceRecord const *entry = cluster.provenance->find(games[i].id);
            reported_matches = reported_matches && entry != nullptr
                && tw::encode_outcome(entry->reported) == tw::encode_outcome(tw::to_wire(local[i]));
        }
        check(reported_matches, "honest cluster provenance matches local outcomes");
    }

    void test_tournament_invariance()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<trun::RosterEntry> const roster = make_roster(4);
        std::uint64_t const generation_seed = 4242;
        tuning::RunConfig const config = fast_config();
        trun::RunLimits limits;
        limits.wave_limit = 16;
        limits.max_games = 1000;
        limits.max_draws = 1000;

        std::vector<DeviceHarness> devices{make_device(1), make_device(2), make_device(3)};
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 2;
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, honest_handler(devices[0].keys, engine));
        cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
        cluster.transport->add_device(3, honest_handler(devices[2].keys, engine));

        trun::TournamentRunner<trem::RemoteBackend> remote_runner(
            cluster.backend, roster, generation_seed, config, limits);
        trun::RunResult const remote_result = remote_runner.run();
        check(remote_result.error.code == trun::ErrorCode::None, "remote tournament completes without error");
        check(remote_result.complete, "remote tournament reaches a champion");

        trun::TournamentRunner<TojBackend> local_runner(engine, roster, generation_seed, config, limits);
        trun::RunResult const local_result = local_runner.run();
        check(local_result.complete, "local tournament reaches a champion");

        check(remote_runner.checksum() == local_runner.checksum(),
              "remote tournament ledger checksum equals local");
        check(remote_result.champion == local_result.champion, "remote champion equals local champion");
        check(remote_runner.total_games() == local_runner.total_games(), "remote game count equals local");
    }

    void test_liar_detection_and_repair()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        taud::ReRun const engine_rerun = [engine](std::vector<tuning::BatchGame> const &games,
                                                  tuning::RunConfig const &config)
        {
            return engine.run_games(games, config);
        };
        std::vector<trun::RosterEntry> const roster = make_roster(4);
        std::uint64_t const generation_seed = 4242;
        tuning::RunConfig const config = fast_config();
        trun::RunLimits limits;
        limits.wave_limit = 16;
        limits.max_games = 1000;
        limits.max_draws = 1000;

        std::vector<DeviceHarness> devices{make_device(1), make_device(2), make_device(3)};
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 2;
        remote_config.log = [](std::string const &message)
        {
            std::println("DEBUG {}", message);
        };
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, honest_handler(devices[0].keys, engine));
        cluster.transport->add_device(2, flip_liar_handler(devices[1].keys, engine));
        cluster.transport->add_device(3, honest_handler(devices[2].keys, engine));

        trun::TournamentRunner<trem::RemoteBackend> remote_runner(
            cluster.backend, roster, generation_seed, config, limits);
        trun::RunResult const remote_result = remote_runner.run();
        check(remote_result.error.code == trun::ErrorCode::None, "lying tournament still completes");

        std::vector<taud::AuditTarget> const targets
            = taud::select_targets(*cluster.provenance, 1.0, {}, 99);
        check(targets.size() == cluster.provenance->size(), "full-rate audit samples every game");
        taud::AuditReport const report = taud::audit_records(*cluster.provenance, targets, config, engine_rerun);
        std::vector<tw::DeviceId> const failed = report.failed_devices();
        check(failed.size() == 1 && failed[0] == 2, "audit isolates the lying device");
        bool liar_failed_games = false;
        for (taud::AuditVerdict const &verdict : report.verdicts)
        {
            if (verdict.target.device == 2 && !verdict.passed)
            {
                liar_failed_games = true;
            }
            if (verdict.target.device != 2)
            {
                check(verdict.passed, "honest devices pass the audit");
            }
        }
        check(liar_failed_games, "lying device has failed verdicts");

        cluster.registry->blacklist(2);
        std::vector<tw::GameId> const voided = cluster.provenance->games_of_device(2);
        check(!voided.empty(), "liar has accepted games to void");

        trep::RepairRequest request;
        request.roster = roster;
        request.generation_seed = generation_seed;
        request.config = config;
        request.ledger = remote_runner.ledger();
        request.voided_game_ids = voided;
        request.re_run = engine_rerun;
        trep::RepairResult const repaired = trep::repair_ledger(std::move(request));
        check(repaired.ok, "repair walk succeeds: " + (repaired.ok ? std::string("ok") : repaired.error));
        check(repaired.voided_games == static_cast<int>(voided.size()), "repair voids every liar game");
        check(repaired.re_run_games >= 1, "repair re-runs at least one hole game");

        trun::TournamentRunner<TojBackend> local_runner(engine, roster, generation_seed, config, limits);
        trun::RunResult const local_result = local_runner.run();
        check(local_result.complete, "honest reference tournament completes");

        std::unordered_map<std::uint64_t, trun::GameRecord> honest_by_id;
        for (trun::GameRecord const &record : local_runner.ledger())
        {
            honest_by_id[record.game_id] = record;
        }
        bool repaired_records_honest = true;
        for (trun::GameRecord const &record : repaired.repaired_ledger)
        {
            auto const it = honest_by_id.find(record.game_id);
            if (it == honest_by_id.end() || !(it->second == record))
            {
                repaired_records_honest = false;
            }
        }
        check(repaired_records_honest, "every repaired record matches the honest ledger");

        trun::TournamentRunner<TojBackend> resume_runner(engine, roster, generation_seed, config, limits,
                                                         repaired.repaired_ledger);
        trun::RunResult const resume_result = resume_runner.run();
        check(resume_result.error.code == trun::ErrorCode::None,
              "resumed tournament after repair completes without error");
        check(resume_result.complete, "resumed tournament after repair reaches a champion");
        check(resume_runner.checksum() == local_runner.checksum(),
              "resumed ledger checksum equals the honest tournament ledger");
        check(resume_result.champion == local_result.champion, "resumed champion equals the honest champion");
    }

    void test_dropper_and_late_results()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        auto make_batch = [&theta_a, &theta_b]()
        {
            std::vector<tuning::BatchGame> games;
            for (std::uint64_t id = 1; id <= 2; ++id)
            {
                tuning::BatchGame game;
                game.id = id;
                game.theta_a = theta_a;
                game.theta_b = theta_b;
                game.seed_a = tuning::derive_game_seed(555, id, 0);
                game.seed_b = tuning::derive_game_seed(555, id, 1);
                games.push_back(std::move(game));
            }
            return games;
        };
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 100;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;

        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        Cluster silent_cluster(devices, remote_config);
        silent_cluster.transport->add_device(
            1, [](tw::AssignmentBatch const &) { return std::optional<tw::SignedResult>{}; });
        silent_cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
        std::vector<tuning::BatchGame> const batch = make_batch();
        std::vector<tuning::GameOutcome> const silent_result
            = silent_cluster.backend.run_games(batch, fast_config());
        check(outcomes_identical(silent_result, engine.run_games(batch, fast_config())),
              "silent dropper is reassigned and results stay correct");
        check(silent_cluster.provenance->games_of_device(1).empty(), "silent dropper has nothing accepted");
        check(silent_cluster.provenance->games_of_device(2).size() == 2, "honest device covers dropped games");
        check(silent_cluster.registry->stats(1) != nullptr && silent_cluster.registry->stats(1)->games_dropped >= 1,
              "dropper drop stats recorded");

        std::shared_ptr<trem::DeviceTiming> const late_timing = std::make_shared<trem::DeviceTiming>();
        late_timing->record_delivery(2, 1, 200);
        trem::RemoteConfig late_config = remote_config;
        late_config.timing = late_timing;
        Cluster late_cluster(devices, late_config);
        std::shared_ptr<tt::ManualClock> const clock = late_cluster.clock;
        tw::KeyPair const late_keys = devices[0].keys;
        late_cluster.transport->add_device(
            1, [late_keys, engine, clock](tw::AssignmentBatch const &assignment)
            {
                clock->advance(101);
                return run_assignment_honestly(late_keys, assignment, engine);
            });
        late_cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
        std::vector<tuning::GameOutcome> const late_result
            = late_cluster.backend.run_games(batch, fast_config());
        check(outcomes_identical(late_result, engine.run_games(batch, fast_config())),
              "late but valid results are discarded and reassigned");
        check(late_cluster.provenance->games_of_device(1).empty(), "late device has nothing accepted");
        check(late_cluster.provenance->games_of_device(2).size() == 2, "honest device covers late games");
        check(late_cluster.registry->stats(1) != nullptr && late_cluster.registry->stats(1)->games_dropped >= 1,
              "late device drop stats recorded");
    }

    void test_protocol_tamper_rejections()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> batch;
        for (std::uint64_t id = 1; id <= 2; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(888, id, 0);
            game.seed_b = tuning::derive_game_seed(888, id, 1);
            batch.push_back(std::move(game));
        }
        std::vector<tuning::GameOutcome> const local = engine.run_games(batch, fast_config());

        auto make_tamper_cluster = [&](std::vector<DeviceHarness> &devices_out)
        {
            devices_out = std::vector<DeviceHarness>{make_device(1), make_device(2)};
            trem::RemoteConfig remote_config;
            remote_config.games_per_assignment = 2;
            remote_config.lease_ms = 1000000;
            remote_config.max_assignment_rounds = 4;
            remote_config.per_series_device_cap = 8;
            return Cluster(devices_out, remote_config);
        };

        auto run_tampered = [&](std::function<void(tw::ResultBatch &)> mutate, std::string const &name)
        {
            std::vector<DeviceHarness> devices;
            Cluster cluster = make_tamper_cluster(devices);
            tw::KeyPair const bad_keys = devices[0].keys;
            cluster.transport->add_device(
                1, [bad_keys, engine, mutate](tw::AssignmentBatch const &assignment)
                {
                    std::optional<tw::SignedResult> held
                        = run_assignment_honestly(bad_keys, assignment, engine);
                    if (held.has_value())
                    {
                        mutate(held->batch);
                        held->signature = tw::sign_result(bad_keys, held->batch);
                    }
                    return held;
                });
            cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
            std::vector<tuning::GameOutcome> const result
                = cluster.backend.run_games(batch, fast_config());
            check(outcomes_identical(result, local), name + ": results still correct via honest device");
            check(cluster.provenance->games_of_device(1).empty(), name + ": tampered device accepted nothing");
            treg::DeviceStats const *stats = cluster.registry->stats(1);
            check(stats != nullptr && stats->games_dropped >= 1, name + ": tampered device drop stats recorded");
        };

        run_tampered([](tw::ResultBatch &held)
        {
            held.nonce += 1;
        }, "wrong nonce echo");

        run_tampered([](tw::ResultBatch &held)
        {
            held.device = 999;
        }, "wrong device echo");

        run_tampered([](tw::ResultBatch &held)
        {
            if (!held.outcomes.empty())
            {
                held.outcomes[0].id += 1000;
            }
        }, "outcome id mismatch");

        run_tampered([](tw::ResultBatch &held)
        {
            if (!held.outcomes.empty())
            {
                held.outcomes.pop_back();
            }
        }, "outcome count mismatch");

        run_tampered([](tw::ResultBatch &held)
        {
            if (!held.outcomes.empty())
            {
                held.outcomes[0].winner = 7;
                held.outcomes[0].reason = tuning::WinReason::ASurvivor;
            }
        }, "winner out of domain");

        run_tampered([](tw::ResultBatch &held)
        {
            if (!held.outcomes.empty())
            {
                held.outcomes[0].winner = 1;
                held.outcomes[0].reason = tuning::WinReason::BSurvivor;
            }
        }, "reason inconsistent with winner");

        std::vector<DeviceHarness> wrong_key_devices;
        Cluster wrong_key_cluster = make_tamper_cluster(wrong_key_devices);
        tw::KeyPair const wrong_key_keys = wrong_key_devices[0].keys;
        wrong_key_cluster.transport->add_device(
            1, [wrong_key_keys, engine](tw::AssignmentBatch const &assignment)
            {
                std::optional<tw::SignedResult> held
                    = run_assignment_honestly(wrong_key_keys, assignment, engine);
                if (held.has_value())
                {
                    tw::KeyPair const other = tw::generate_keypair();
                    held->signature = tw::sign_result(other, held->batch);
                }
                return held;
            });
        wrong_key_cluster.transport->add_device(2, honest_handler(wrong_key_devices[1].keys, engine));
        std::vector<tuning::GameOutcome> const wrong_key_result
            = wrong_key_cluster.backend.run_games(batch, fast_config());
        check(outcomes_identical(wrong_key_result, local), "wrong signing key: results still correct");
        check(wrong_key_cluster.provenance->games_of_device(1).empty(),
              "wrong signing key: device accepted nothing");
    }

    void test_failure_modes()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 2;
        remote_config.per_series_device_cap = 8;

        Cluster empty_cluster({}, remote_config);
        tuning::BatchGame game;
        game.id = 1;
        game.theta_a = theta_for(0);
        game.theta_b = theta_for(1);
        game.seed_a = 1;
        game.seed_b = 2;
        bool threw = false;
        try
        {
            empty_cluster.backend.run_games({game}, fast_config());
        }
        catch (std::runtime_error const &)
        {
            threw = true;
        }
        check(threw, "no active devices throws runtime error");

        std::vector<DeviceHarness> devices{make_device(1)};
        Cluster stubborn_cluster(devices, remote_config);
        stubborn_cluster.transport->add_device(
            1, [keys = devices[0].keys](tw::AssignmentBatch const &)
            {
                tw::KeyPair const other = tw::generate_keypair();
                tw::ResultBatch result;
                result.nonce = 0;
                result.device = 0;
                return tw::SignedResult{std::move(result), tw::sign_result(other, result)};
            });
        threw = false;
        try
        {
            stubborn_cluster.backend.run_games({game}, fast_config());
        }
        catch (std::runtime_error const &)
        {
            threw = true;
        }
        check(threw, "exhausted assignment rounds throws runtime error");
    }

    void test_round_budget_scales_with_demand()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 10; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(321, id, 0);
            game.seed_b = tuning::derive_game_seed(321, id, 1);
            games.push_back(std::move(game));
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 64;

        std::vector<DeviceHarness> devices{make_device(1)};
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, honest_handler(devices[0].keys, engine));
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(outcomes_identical(results, engine.run_games(games, fast_config())),
              "ten games on one capacity 1 device complete with correct results");
        check(cluster.provenance->size() == 10, "demand beyond the configured round floor still completes");
        check(cluster.registry->stats(1) != nullptr && cluster.registry->stats(1)->games_dropped == 0,
              "honest single device records no drops across the scaled rounds");
    }

    void test_slow_sibling_does_not_expire_fast_results()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 4; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(777, id, 0);
            game.seed_b = tuning::derive_game_seed(777, id, 1);
            games.push_back(std::move(game));
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 100;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        std::vector<std::string> log_lines;
        remote_config.log = [&log_lines](std::string const &message)
        {
            log_lines.push_back(message);
        };

        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        Cluster cluster(devices, remote_config);
        std::shared_ptr<tt::ManualClock> const clock = cluster.clock;
        tw::KeyPair const slow_keys = devices[0].keys;
        tw::KeyPair const fast_keys = devices[1].keys;
        auto fast_delivered = std::make_shared<std::atomic<bool>>(false);
        cluster.transport->add_device(
            1, [slow_keys, engine, clock, fast_delivered](tw::AssignmentBatch const &assignment)
            {
                while (!fast_delivered->load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                clock->advance(100000);
                return run_assignment_honestly(slow_keys, assignment, engine);
            });
        cluster.transport->add_device(
            2, [fast_keys, engine, fast_delivered](tw::AssignmentBatch const &assignment)
            {
                std::optional<tw::SignedResult> const held
                    = run_assignment_honestly(fast_keys, assignment, engine);
                fast_delivered->store(true);
                return held;
            });
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(outcomes_identical(results, engine.run_games(games, fast_config())),
              "a slow sibling no longer expires the fast device's on-time results");
        check(cluster.provenance->games_of_device(2).size() == 4,
              "the fast device covers every game including the slow device's share");
        check(cluster.provenance->games_of_device(1).empty(),
              "the beyond-window device has nothing accepted");
        check(cluster.registry->stats(1) != nullptr && cluster.registry->stats(1)->games_dropped >= 2,
              "the beyond-window device records drops");
        bool window_logged = false;
        for (std::string const &line : log_lines)
        {
            window_logged = window_logged || line.find("past the") != std::string::npos;
        }
        check(window_logged, "window expiry drops are logged with arrival and window timings");
    }

    void test_streaming_audit_catches_liar_at_acceptance()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 4; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(911, id, 0);
            game.seed_b = tuning::derive_game_seed(911, id, 1);
            games.push_back(std::move(game));
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        remote_config.audit_rate = 1.0;
        remote_config.auditor = [&engine](std::vector<tuning::BatchGame> const &audit_games,
                                          tuning::RunConfig const &audit_config)
        {
            return engine.run_games(audit_games, audit_config);
        };
        std::vector<tw::DeviceId> caught;
        remote_config.on_liar = [&caught](tw::DeviceId device)
        {
            caught.push_back(device);
        };

        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, flip_liar_handler(devices[0].keys, engine));
        cluster.transport->add_device(2, honest_handler(devices[1].keys, engine));
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(outcomes_identical(results, engine.run_games(games, fast_config())),
              "streaming audit: liar games reassigned with correct results");
        check(cluster.provenance->games_of_device(1).empty(),
              "streaming audit: liar has nothing accepted");
        check(cluster.provenance->games_of_device(2).size() == 4,
              "streaming audit: honest device covers every game");
        check(caught.size() == 1 && caught[0] == 1,
              "streaming audit: the liar catch is reported exactly once");
        check(cluster.backend.audited_game_ids().size() == 4,
              "streaming audit: audited game ids cover the liar and honest flights");
        treg::DeviceStats const *liar_stats = cluster.registry->stats(1);
        treg::DeviceStats const *honest_stats = cluster.registry->stats(2);
        check(liar_stats != nullptr && liar_stats->audits_failed >= 1,
              "streaming audit: liar failure recorded in device stats");
        check(honest_stats != nullptr && honest_stats->audits_passed >= 1,
              "streaming audit: honest passes recorded in device stats");
    }

    void test_streaming_audit_passes_honest_devices()
    {
        ContextPtr context = tuning_toj::TojAdapter::make_shared_context();
        TojBackend const engine{context};
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 4; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(913, id, 0);
            game.seed_b = tuning::derive_game_seed(913, id, 1);
            games.push_back(std::move(game));
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        remote_config.audit_rate = 1.0;
        remote_config.auditor = [&engine](std::vector<tuning::BatchGame> const &audit_games,
                                          tuning::RunConfig const &audit_config)
        {
            return engine.run_games(audit_games, audit_config);
        };

        std::vector<DeviceHarness> devices{make_device(1)};
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(1, honest_handler(devices[0].keys, engine));
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(outcomes_identical(results, engine.run_games(games, fast_config())),
              "full rate streaming audit keeps honest results correct");
        check(cluster.provenance->size() == 4, "every honest game is accepted under the audit");
        check(cluster.backend.audited_game_ids().size() == 4, "every game is recorded as audited");
        treg::DeviceStats const *stats = cluster.registry->stats(1);
        check(stats != nullptr && stats->games_dropped == 0 && stats->audits_passed >= 1,
              "honest device records no drops and at least one audit pass");
    }

    void test_silent_device_exhaustion_counts_rounds()
    {
        std::vector<double> const theta_a = theta_for(0);
        std::vector<double> const theta_b = theta_for(1);
        std::vector<tuning::BatchGame> games;
        for (std::uint64_t id = 1; id <= 2; ++id)
        {
            tuning::BatchGame game;
            game.id = id;
            game.theta_a = theta_a;
            game.theta_b = theta_b;
            game.seed_a = tuning::derive_game_seed(654, id, 0);
            game.seed_b = tuning::derive_game_seed(654, id, 1);
            games.push_back(std::move(game));
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 2;
        remote_config.per_series_device_cap = 8;

        std::vector<DeviceHarness> devices{make_device(1)};
        Cluster cluster(devices, remote_config);
        cluster.transport->add_device(
            1, [](tw::AssignmentBatch const &) { return std::optional<tw::SignedResult>{}; });
        std::string message;
        try
        {
            cluster.backend.run_games(games, fast_config());
        }
        catch (std::runtime_error const &error)
        {
            message = error.what();
        }
        check(message.find("exhausted 3 assignment rounds") != std::string::npos,
              "round budget is the floor plus the flight arithmetic: " + message);
    }

    trem::RemoteConfig fabricated_remote_config()
    {
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 1;
        remote_config.lease_ms = 1000000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        return remote_config;
    }

    void test_concurrency_capacity_respected()
    {
        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        Cluster cluster(devices, fabricated_remote_config());
        cluster.registry->set_concurrency(1, 2);
        cluster.registry->set_concurrency(2, 1);
        cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1, cluster.clock));
        cluster.transport->add_device(2, fabricated_handler(devices[1].keys, -1, cluster.clock));

        std::vector<tuning::BatchGame> const games = fabricated_games(4);
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(results.size() == 4, "capacity cluster returns every game result");
        check(cluster.provenance->size() == 4, "capacity cluster records full provenance");
        check(cluster.provenance->games_of_device(1) == std::vector<tw::GameId>{1, 3, 4},
              "device advertising 2 takes flights 1 and 3 plus the deferred flight");
        check(cluster.provenance->games_of_device(2) == std::vector<tw::GameId>{2},
              "device advertising 1 receives exactly one flight while alternatives are capped");
        check(fabricated_results_match(results, *cluster.provenance, 1),
              "fabricated results match the attributed device outcome");

        tprov::ProvenanceRecord const *deferred = cluster.provenance->find(4);
        tprov::ProvenanceRecord const *first_round = cluster.provenance->find(1);
        check(deferred != nullptr && first_round != nullptr
                && deferred->accepted_at_ms > first_round->accepted_at_ms,
              "fourth chunk deferred to a later round instead of overrunning capacity 1");

        treg::DeviceStats const *stats_one = cluster.registry->stats(1);
        treg::DeviceStats const *stats_two = cluster.registry->stats(2);
        check(stats_one != nullptr && stats_one->games_accepted == 3 && stats_one->games_dropped == 0,
              "capacity 2 device accepted 3 games without drops");
        check(stats_two != nullptr && stats_two->games_accepted == 1 && stats_two->games_dropped == 0,
              "capacity 1 device accepted 1 game without drops");
    }

    void test_unspecified_concurrency_acts_as_one()
    {
        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        Cluster cluster(devices, fabricated_remote_config());
        cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
        cluster.transport->add_device(2, fabricated_handler(devices[1].keys, -1));

        std::vector<tuning::BatchGame> const games = fabricated_games(2);
        std::vector<tuning::GameOutcome> const results = cluster.backend.run_games(games, fast_config());
        check(cluster.provenance->size() == 2, "unspecified capacity cluster records both games");
        check(cluster.provenance->games_of_device(1) == std::vector<tw::GameId>{1}
                && cluster.provenance->games_of_device(2) == std::vector<tw::GameId>{2},
              "unspecified capacity spreads two single-game flights across both devices");
        check(fabricated_results_match(results, *cluster.provenance, 1),
              "unspecified capacity results match the attributed device outcome");
    }

    std::uint64_t ms_since(std::chrono::steady_clock::time_point const &began)
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - began)
                .count());
    }

    void test_device_timing_capacity_backoff()
    {
        trem::DeviceTiming timing;
        check(timing.effective_capacity(1, 8) == 8, "an idle device starts at its declared capacity");
        timing.record_timeout(1, 0, true, 1000);
        check(timing.effective_capacity(1, 8) == 4, "a timeout halves the device capacity");
        timing.record_timeout(1, 0, true, 1000);
        check(timing.effective_capacity(1, 8) == 2, "a second timeout halves the capacity again");
        timing.record_timeout(1, 0, true, 1000);
        check(timing.effective_capacity(1, 8) == 1, "capacity backoff stops at one outstanding flight");
        timing.record_timeout(1, 0, true, 1000);
        check(timing.effective_capacity(1, 8) == 1, "repeated timeouts never take capacity below one");
        timing.record_delivery(1, 1, 200);
        check(timing.effective_capacity(1, 8) == 2, "a delivered flight recovers one capacity step");
        timing.record_delivery(1, 1, 200);
        check(timing.effective_capacity(1, 8) == 3, "the next delivery recovers one more step");
        for (int step = 0; step < 8; ++step)
        {
            timing.record_delivery(1, 1, 200);
        }
        check(timing.effective_capacity(1, 8) == 8, "capacity recovery stops at the declared capacity");
        check(timing.effective_capacity(1, 3) == 3, "a lowered declared capacity clamps the stored value");
        timing.record_timeout(1, 0, true, 1000);
        check(timing.effective_capacity(1, 3) == 1, "halving an odd declared capacity floors at one");
        timing.record_reset(1);
        check(timing.effective_capacity(1, 3) == 3, "a reconnect reset restores the full declared capacity");
        check(timing.effective_capacity(2, 5) == 5, "an unseen device is seeded with its declared capacity");
        check(timing.effective_capacity(3, 0) == 1, "a zero declared capacity is treated as one flight");
    }

    void test_device_timing_timeout_releases_outstanding_games()
    {
        trem::DeviceTiming timing;
        timing.seed_ms_per_game(100.0);
        timing.record_dispatch(1, 8);
        std::uint64_t const backlog_ms = timing.wait_hint_ms(1, 2, 1000);
        std::uint64_t const idle_ms = timing.wait_hint_ms(2, 2, 1000);
        check(backlog_ms > idle_ms, "outstanding games widen the wait window of a busy device");
        timing.record_timeout(1, 3, true, 500);
        std::uint64_t const partial_ms = timing.wait_hint_ms(1, 2, 1000);
        check(partial_ms < backlog_ms && partial_ms > idle_ms,
              "a timeout releases exactly the timed out games from the backlog");
        timing.record_timeout(1, 5, true, 500);
        check(timing.wait_hint_ms(1, 2, 1000) == idle_ms,
              "timing out the rest of the backlog returns the device to its idle window");
        timing.record_dispatch(1, 6);
        timing.record_timeout(1, 20, true, 500);
        check(timing.wait_hint_ms(1, 2, 1000) == idle_ms,
              "a timeout larger than the backlog cannot drive it negative");
        timing.record_dispatch(1, 6);
        timing.record_reset(1);
        check(timing.wait_hint_ms(1, 2, 1000) == idle_ms,
              "a reconnect reset drops the outstanding games of the lost connection");
    }

    void test_reconnect_grace_waits_for_blipped_devices()
    {
        std::vector<DeviceHarness> devices{make_device(1)};
        trem::RemoteConfig config = fabricated_remote_config();
        config.games_per_assignment = 2;
        config.reconnect_grace_ms = 8000;
        config.timing = std::make_shared<trem::DeviceTiming>();
        Cluster cluster(devices, config);
        tw::KeyPair const keys_one = devices[0].keys;
        std::shared_ptr<tl::LoopbackTransport> const transport = cluster.transport;
        transport->add_device(1, fabricated_handler(keys_one, 1));
        transport->remove_device(1);
        std::thread reconnect([transport, keys_one]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            transport->add_device(1, fabricated_handler(keys_one, 1));
        });
        auto const began = std::chrono::steady_clock::now();
        std::string failure;
        std::vector<tuning::GameOutcome> results;
        try
        {
            results = cluster.backend.run_games(fabricated_games(2), fast_config());
        }
        catch (std::runtime_error const &error)
        {
            failure = error.what();
        }
        reconnect.join();
        std::uint64_t const waited_ms = ms_since(began);
        check(failure.empty(), "a blipped device that returns within the grace does not fail the run: " + failure);
        check(waited_ms >= 200, "run_games holds the pending games while it waits for a reconnect");
        check(cluster.provenance->size() == 2, "the returned device plays every pending game");
        check(results.size() == 2 && fabricated_results_match(results, *cluster.provenance, 1),
              "results after a reconnect match the device that played them");
    }

    void test_reconnect_grace_expiry_names_the_grace_window()
    {
        std::vector<DeviceHarness> devices{make_device(1)};
        trem::RemoteConfig config = fabricated_remote_config();
        config.games_per_assignment = 2;
        config.reconnect_grace_ms = 500;
        config.timing = std::make_shared<trem::DeviceTiming>();
        Cluster cluster(devices, config);
        std::string failure;
        auto const began = std::chrono::steady_clock::now();
        try
        {
            cluster.backend.run_games(fabricated_games(2), fast_config());
        }
        catch (std::runtime_error const &error)
        {
            failure = error.what();
        }
        check(failure.find("no connected devices within") != std::string::npos,
              "run_games gives up once the reconnect grace expires: " + failure);
        check(failure.find("no connected devices within 500 ms") != std::string::npos,
              "the expired grace error names the configured grace in milliseconds");
        check(ms_since(began) >= 500, "the grace is waited out in full before the run is abandoned");
        check(cluster.provenance->size() == 0, "an abandoned grace window attributes no games");
    }

    void test_empty_enrolled_set_throws_without_a_grace_wait()
    {
        std::vector<DeviceHarness> devices{make_device(1)};
        trem::RemoteConfig config = fabricated_remote_config();
        config.games_per_assignment = 2;
        config.reconnect_grace_ms = 20000;
        config.timing = std::make_shared<trem::DeviceTiming>();
        Cluster cluster({}, config);
        cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
        std::string failure;
        auto const began = std::chrono::steady_clock::now();
        try
        {
            cluster.backend.run_games(fabricated_games(2), fast_config());
        }
        catch (std::runtime_error const &error)
        {
            failure = error.what();
        }
        check(failure.find("no active devices") != std::string::npos,
              "an empty enrolled set fails with the no active devices error: " + failure);
        check(ms_since(began) < 250, "an empty enrolled set is not held by the reconnect grace");
        check(cluster.provenance->size() == 0, "a device that was never enrolled plays no games");
    }

    void test_dispatch_skips_devices_inactive_in_the_registry()
    {
        std::vector<DeviceHarness> devices{make_device(1), make_device(2)};
        trem::RemoteConfig config = fabricated_remote_config();
        config.games_per_assignment = 2;
        config.timing = std::make_shared<trem::DeviceTiming>();
        Cluster cluster(devices, config);
        cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
        cluster.transport->add_device(2, fabricated_handler(devices[1].keys, -1));
        cluster.registry->blacklist(2);
        std::vector<tuning::GameOutcome> const results
            = cluster.backend.run_games(fabricated_games(2), fast_config());
        check(cluster.transport->devices().size() == 2, "both devices stay connected to the transport");
        check(cluster.provenance->games_of_device(2).empty(),
              "a connected device that is no longer active receives no games");
        check(cluster.provenance->games_of_device(1) == std::vector<tw::GameId>{1, 2},
              "the active device of the intersection covers every pending game");
        check(results.size() == 2 && fabricated_results_match(results, *cluster.provenance, 1),
              "intersection only dispatch keeps the attributed results consistent");
        treg::DeviceStats const *dropped = cluster.registry->stats(2);
        check(dropped != nullptr && dropped->games_accepted == 0 && dropped->games_dropped == 0,
              "the deactivated device records neither acceptances nor drops");
    }

    void test_short_tail_spreads_one_game_per_device()
    {
        std::vector<DeviceHarness> devices{make_device(1), make_device(2), make_device(3), make_device(4),
                                           make_device(5), make_device(6)};
        trem::RemoteConfig config = fabricated_remote_config();
        config.games_per_assignment = 4;
        config.timing = std::make_shared<trem::DeviceTiming>();
        Cluster cluster(devices, config);
        cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
        for (std::size_t index = 1; index < devices.size(); ++index)
        {
            cluster.transport->add_device(devices[index].id, fabricated_handler(devices[index].keys, -1));
        }
        std::vector<tuning::GameOutcome> const results
            = cluster.backend.run_games(fabricated_games(3), fast_config());
        std::size_t reached_devices = 0;
        std::size_t largest_device_share = 0;
        for (std::size_t index = 0; index < devices.size(); ++index)
        {
            treg::DeviceStats const *stats = cluster.registry->stats(devices[index].id);
            std::size_t const accepted = stats == nullptr ? 0 : static_cast<std::size_t>(stats->games_accepted);
            reached_devices = reached_devices + (accepted > 0 ? 1 : 0);
            largest_device_share = std::max(largest_device_share, accepted);
        }
        check(cluster.provenance->size() == 3, "a tail shorter than the fleet still plays every game");
        check(reached_devices == 3, "a three game tail reaches three different devices");
        check(largest_device_share == 1, "no device absorbs the whole tail while peers sit idle");
        check(results.size() == 3 && fabricated_results_match(results, *cluster.provenance, 1),
              "the spread tail attributes each game to the device that played it");
    }

    void test_degenerate_capacity_liveness()
    {
        {
            std::vector<DeviceHarness> devices{make_device(1)};
            Cluster cluster(devices, fabricated_remote_config());
            cluster.registry->set_concurrency(1, 1);
            cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
            std::vector<tuning::BatchGame> const games = fabricated_games(4);
            std::vector<tuning::GameOutcome> const results
                = cluster.backend.run_games(games, fast_config());
            check(results.size() == 4 && cluster.provenance->size() == 4,
                  "single capacity 1 device completes four chunks within four rounds");
            check(cluster.provenance->games_of_device(1) == std::vector<tw::GameId>{1, 2, 3, 4},
                  "single device owns every degenerate attribution");
            check(fabricated_results_match(results, *cluster.provenance, 1),
                  "degenerate four chunk run keeps fabricated outcomes intact");
        }
        {
            std::vector<DeviceHarness> devices{make_device(1)};
            Cluster cluster(devices, fabricated_remote_config());
            cluster.registry->set_concurrency(1, 1);
            cluster.transport->add_device(1, fabricated_handler(devices[0].keys, 1));
            std::vector<tuning::BatchGame> const games = fabricated_games(5);
            std::vector<tuning::GameOutcome> const results
                = cluster.backend.run_games(games, fast_config());
            check(results.size() == 5 && cluster.provenance->size() == 5,
                  "more chunks than rounds still completes via the final fallback tier");
            check(cluster.registry->stats(1) != nullptr
                    && cluster.registry->stats(1)->games_accepted == 5,
                  "final fallback tier accepted every degenerate chunk");
        }
    }
}

int main()
{
    test_honest_batch_matches_local();
    test_tournament_invariance();
    test_liar_detection_and_repair();
    test_dropper_and_late_results();
    test_protocol_tamper_rejections();
    test_failure_modes();
    test_round_budget_scales_with_demand();
    test_slow_sibling_does_not_expire_fast_results();
    test_streaming_audit_catches_liar_at_acceptance();
    test_streaming_audit_passes_honest_devices();
    test_silent_device_exhaustion_counts_rounds();
    test_concurrency_capacity_respected();
    test_unspecified_concurrency_acts_as_one();
    test_device_timing_capacity_backoff();
    test_device_timing_timeout_releases_outstanding_games();
    test_reconnect_grace_waits_for_blipped_devices();
    test_reconnect_grace_expiry_names_the_grace_window();
    test_empty_enrolled_set_throws_without_a_grace_wait();
    test_dispatch_skips_devices_inactive_in_the_registry();
    test_short_tail_spreads_one_game_per_device();
    test_degenerate_capacity_liveness();
    std::println("remote backend integration: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
