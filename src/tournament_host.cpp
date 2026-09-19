#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <set>
#include <signal.h>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "tournament/audit.h"
#include "tournament/ban_file.h"
#include "tournament/bytes.h"
#include "tournament/config_file.h"
#include "tournament/engine_identity.h"
#include "tournament/net_transport.h"
#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/remote_backend.h"
#include "tournament/repair.h"
#include "tournament/runner.h"
#include "tournament/toj_conformance.h"
#include "tournament/transport.h"
#include "tuning/domain.h"
#include "tuning/engine_match.h"
#include "tuning/match.h"
#include "tuning/toj_adapter.h"

namespace tournament_host
{
    std::atomic<bool> g_stop_requested{false};

    extern "C" void request_stop_on_signal(int)
    {
        g_stop_requested.store(true, std::memory_order_relaxed);
    }

    void install_signal_handlers()
    {
        struct sigaction action;
        std::memset(&action, 0, sizeof action);
        action.sa_handler = request_stop_on_signal;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
    }

    namespace taud = tournament_audit;
    namespace tbr = tournament_bracket;
    namespace tnet = tournament_net;
    namespace tprov = tournament_provenance;
    namespace treg = tournament_registry;
    namespace trem = tournament_remote;
    namespace trep = tournament_repair;
    namespace trun = tournament_runner;
    namespace tt = tournament_transport;
    namespace tw = tournament_wire;

    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;

    struct HostConfig
    {
        std::string address = "0.0.0.0";
        std::uint16_t port = 47001;
        std::string certificate = "tournament_host.cert";
        std::string private_key = "tournament_host.key";
        std::string devices_file;
        std::string ban_file = "tournament_bans.txt";
        int roster_size = 4;
        std::uint64_t generation_seed = 4242;
        double audit_rate = 1.0;
        int games_per_assignment = 4;
        std::uint64_t lease_ms = 30000;
        int series_cap = 2;
        std::uint64_t wait_clients_ms = 120000;
        int threads = 1;
        int iterations = 40;
        int max_rounds = 600;
        std::int64_t max_games = 10000;
        std::int64_t max_draws = 10000;
        int audit_workers = 2;
        std::string trust_file = "tournament_trust.txt";
        std::string unban_key;
        std::uint64_t reconnect_grace_ms = 60000;
    };

    void print_usage(char const *program)
    {
        std::println("Usage:");
        std::println("  {} [flags]", program);
        std::println("  {} --address 0.0.0.0 --port 47001 --certificate tournament_host.cert --private-key tournament_host.key", program);
        std::println("Flags: --address --port --certificate --private-key --roster-size --generation-seed --audit-rate");
        std::println("  --games-per-assignment --lease-ms --series-cap --wait-clients-ms --threads --iterations");
        std::println("  --max-rounds --max-games --max-draws");
        std::println("  --devices-file P restricts enrollment to the device ids and keys listed in P");
        std::println("  --ban-file P persists banned device keys across restarts (default tournament_bans.txt, empty string disables)");
        std::println("Defaults: address 0.0.0.0, port 47001, certificate tournament_host.cert, private key tournament_host.key,");
        std::println("  roster size 4, generation seed 4242, audit rate 1.0, games per assignment 4, lease 30000 ms,");
        std::println("  series cap 2, client wait 120000 ms, threads 1, iterations 40, max rounds 600, max games 10000,");
        std::println("  max draws 10000");
        std::println("Config: every flag above can live in a json file; tournament_host.json in the working directory is read automatically, --config P reads another file, command line values override the file");
        std::println("Config keys: address, port, certificate, private_key, devices_file, ban_file, roster_size, generation_seed, audit_rate, games_per_assignment, lease_ms, series_cap, wait_clients_ms, threads, iterations, max_rounds, max_games, max_draws, trust_file, reconnect_grace_ms");
        std::println("Flags: --trust-file P persists device audit trust across restarts (default tournament_trust.txt, empty string disables)");
        std::println("Flags: --audit-workers N bounds the local audit re-run lanes (default 2, each lane runs single-threaded re-plays)");
        std::println("Flags: --unban K removes the ban record and the trust record for the 64 hex character device public key K from the ban and trust files, then exits");
        std::println("Flags: --reconnect-grace-ms N waits up to N ms for enrolled devices to reconnect before a round fails (default 60000, 0 fails immediately)");
    }

    bool apply_config_file(HostConfig &config, nlohmann::json const &values, std::string &error)
    {
        std::vector<std::string> const allowed{
            "address", "port", "certificate", "private_key", "devices_file", "ban_file",
            "roster_size", "generation_seed", "audit_rate", "games_per_assignment", "lease_ms",
            "series_cap", "wait_clients_ms", "threads", "iterations", "max_rounds", "max_games",
            "max_draws", "audit_workers", "trust_file", "reconnect_grace_ms",
        };
        if (!tournament_config::reject_unknown_keys(values, allowed, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "address", config.address, error))
        {
            return false;
        }
        if (values.contains("port"))
        {
            std::uint64_t port = 0;
            if (!tournament_config::get_u64(values, "port", port, error))
            {
                return false;
            }
            if (port > 65535)
            {
                error = "port must be between 0 and 65535";
                return false;
            }
            config.port = static_cast<std::uint16_t>(port);
        }
        if (!tournament_config::get_string(values, "certificate", config.certificate, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "private_key", config.private_key, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "devices_file", config.devices_file, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "ban_file", config.ban_file, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "roster_size", config.roster_size, error))
        {
            return false;
        }
        if (values.contains("roster_size"))
        {
            config.roster_size = std::clamp(config.roster_size, 2, 16);
        }
        if (!tournament_config::get_u64(values, "generation_seed", config.generation_seed, error))
        {
            return false;
        }
        if (!tournament_config::get_double(values, "audit_rate", config.audit_rate, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "games_per_assignment", config.games_per_assignment,
                                        error))
        {
            return false;
        }
        if (!tournament_config::get_u64(values, "lease_ms", config.lease_ms, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "series_cap", config.series_cap, error))
        {
            return false;
        }
        if (!tournament_config::get_u64(values, "wait_clients_ms", config.wait_clients_ms, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "threads", config.threads, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "iterations", config.iterations, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "max_rounds", config.max_rounds, error))
        {
            return false;
        }
        if (!tournament_config::get_i64(values, "max_games", config.max_games, error))
        {
            return false;
        }
        if (!tournament_config::get_i64(values, "max_draws", config.max_draws, error))
        {
            return false;
        }
        if (!tournament_config::get_int(values, "audit_workers", config.audit_workers, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "trust_file", config.trust_file, error))
        {
            return false;
        }
        return true;
    }

    bool parse_u64(std::string const &text, std::uint64_t &out)
    {
        try
        {
            if (text.empty() || text.front() == '-')
            {
                return false;
            }
            std::size_t consumed = 0;
            unsigned long long value = std::stoull(text, &consumed);
            if (consumed != text.size())
            {
                return false;
            }
            out = static_cast<std::uint64_t>(value);
            return true;
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_int(std::string const &text, int &out)
    {
        try
        {
            if (text.empty())
            {
                return false;
            }
            std::size_t consumed = 0;
            int value = std::stoi(text, &consumed);
            if (consumed != text.size())
            {
                return false;
            }
            out = value;
            return true;
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_i64(std::string const &text, std::int64_t &out)
    {
        try
        {
            if (text.empty())
            {
                return false;
            }
            std::size_t consumed = 0;
            long long value = std::stoll(text, &consumed);
            if (consumed != text.size())
            {
                return false;
            }
            out = static_cast<std::int64_t>(value);
            return true;
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_double(std::string const &text, double &out)
    {
        try
        {
            if (text.empty())
            {
                return false;
            }
            std::size_t consumed = 0;
            double value = std::stod(text, &consumed);
            if (consumed != text.size())
            {
                return false;
            }
            out = value;
            return true;
        }
        catch (std::exception const &)
        {
            return false;
        }
    }

    bool parse_flags(int argc, char *argv[], HostConfig &config)
    {
        std::string const program = argc > 0 ? argv[0] : "tournament_host";
        auto fail = [&program](std::string const &message)
        {
            std::println(stderr, "{}", message);
            print_usage(program.c_str());
            return false;
        };
        for (int i = 1; i < argc; ++i)
        {
            std::string const flag = argv[i];
            if (flag == "--address")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --address requires a value");
                }
                config.address = argv[++i];
            }
            else if (flag == "--port")
            {
                std::uint64_t value = 0;
                if (i + 1 >= argc || !parse_u64(argv[i + 1], value) || value > 65535)
                {
                    return fail("flag --port requires an integer in 0..65535");
                }
                config.port = static_cast<std::uint16_t>(value);
                ++i;
            }
            else if (flag == "--certificate")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --certificate requires a value");
                }
                config.certificate = argv[++i];
            }
            else if (flag == "--private-key")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --private-key requires a value");
                }
                config.private_key = argv[++i];
            }
            else if (flag == "--devices-file")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --devices-file requires a value");
                }
                config.devices_file = argv[++i];
            }
            else if (flag == "--ban-file")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --ban-file requires a value");
                }
                config.ban_file = argv[++i];
            }
            else if (flag == "--roster-size")
            {
                int value = 0;
                if (i + 1 >= argc || !parse_int(argv[i + 1], value))
                {
                    return fail("flag --roster-size requires an integer");
                }
                config.roster_size = std::clamp(value, 2, 16);
                ++i;
            }
            else if (flag == "--generation-seed")
            {
                if (i + 1 >= argc || !parse_u64(argv[i + 1], config.generation_seed))
                {
                    return fail("flag --generation-seed requires an unsigned integer");
                }
                ++i;
            }
            else if (flag == "--audit-rate")
            {
                double value = 0.0;
                if (i + 1 >= argc || !parse_double(argv[i + 1], value) || !std::isfinite(value))
                {
                    return fail("flag --audit-rate requires a finite number");
                }
                config.audit_rate = value;
                ++i;
            }
            else if (flag == "--games-per-assignment")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.games_per_assignment))
                {
                    return fail("flag --games-per-assignment requires an integer");
                }
                ++i;
            }
            else if (flag == "--lease-ms")
            {
                if (i + 1 >= argc || !parse_u64(argv[i + 1], config.lease_ms))
                {
                    return fail("flag --lease-ms requires an unsigned integer");
                }
                ++i;
            }
            else if (flag == "--series-cap")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.series_cap))
                {
                    return fail("flag --series-cap requires an integer");
                }
                ++i;
            }
            else if (flag == "--wait-clients-ms")
            {
                if (i + 1 >= argc || !parse_u64(argv[i + 1], config.wait_clients_ms))
                {
                    return fail("flag --wait-clients-ms requires an unsigned integer");
                }
                ++i;
            }
            else if (flag == "--threads")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.threads))
                {
                    return fail("flag --threads requires an integer");
                }
                ++i;
            }
            else if (flag == "--iterations")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.iterations))
                {
                    return fail("flag --iterations requires an integer");
                }
                ++i;
            }
            else if (flag == "--max-rounds")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.max_rounds))
                {
                    return fail("flag --max-rounds requires an integer");
                }
                ++i;
            }
            else if (flag == "--max-games")
            {
                if (i + 1 >= argc || !parse_i64(argv[i + 1], config.max_games))
                {
                    return fail("flag --max-games requires an integer");
                }
                ++i;
            }
            else if (flag == "--max-draws")
            {
                if (i + 1 >= argc || !parse_i64(argv[i + 1], config.max_draws))
                {
                    return fail("flag --max-draws requires an integer");
                }
                ++i;
            }
            else if (flag == "--trust-file")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --trust-file requires a value");
                }
                config.trust_file = argv[++i];
            }
            else if (flag == "--audit-workers")
            {
                if (i + 1 >= argc || !parse_int(argv[i + 1], config.audit_workers)
                    || config.audit_workers <= 0)
                {
                    return fail("flag --audit-workers requires a positive integer");
                }
                ++i;
            }
            else if (flag == "--unban")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --unban requires a value");
                }
                config.unban_key = argv[++i];
            }
            else if (flag == "--reconnect-grace-ms")
            {
                std::uint64_t value = 0;
                if (i + 1 >= argc || !parse_u64(argv[i + 1], value))
                {
                    return fail("flag --reconnect-grace-ms requires a nonnegative integer");
                }
                config.reconnect_grace_ms = value;
                ++i;
            }
            else if (flag == "--config")
            {
                if (i + 1 >= argc)
                {
                    return fail("flag --config requires a value");
                }
                ++i;
            }
            else
            {
                return fail("unknown flag: " + flag);
            }
        }
        return true;
    }

    int run_unban(char const *key_hex, std::string const &ban_file, std::string const &trust_file)
    {
        std::optional<std::vector<std::uint8_t>> const decoded
            = tournament_bytes::decode_hex(std::string(key_hex));
        if (!decoded.has_value() || decoded->size() != 32)
        {
            std::println(stderr, "--unban expects a 64 hex character device public key");
            return 1;
        }
        tw::PublicKey const key(*decoded);
        if (ban_file.empty())
        {
            std::println(stderr, "--unban needs a ban file, but --ban-file is empty");
            return 1;
        }
        tournament_ban::BanFile store(ban_file);
        bool removed = false;
        std::string error;
        if (!store.remove(key, removed, error))
        {
            std::println(stderr, "cannot update {}: {}", ban_file, error);
            return 1;
        }
        if (removed)
        {
            std::println("removed ban record for {} from {}", key_hex, ban_file);
        }
        else
        {
            std::println("no ban record for {} in {}", key_hex, ban_file);
        }
        if (!trust_file.empty())
        {
            bool trust_removed = false;
            std::string trust_error;
            if (!treg::remove_trust_record(trust_file, key, trust_removed, trust_error))
            {
                std::println(stderr, "cannot update {}: {}", trust_file, trust_error);
                return 1;
            }
            if (trust_removed)
            {
                std::println("removed trust record for {} from {}", key_hex, trust_file);
            }
            else
            {
                std::println("no trust record for {} in {}", key_hex, trust_file);
            }
        }
        return 0;
    }

    bool file_exists(std::string const &path)
    {
        std::error_code error;
        return std::filesystem::exists(path, error);
    }

    tnet::NetConfig net_config_for(HostConfig const &cli)
    {
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        tnet::NetConfig config;
        config.listen_address = cli.address;
        config.port = cli.port;
        config.certificate_path = cli.certificate;
        config.private_key_path = cli.private_key;
        config.expected_adapter_id = std::string(tuning_toj::TojAdapter::kAdapterId);
        config.expected_schema_hash = tuning::schema_hash(schema);
        config.devices_file = cli.devices_file;
        auto probe_context = tuning_toj::TojAdapter::make_shared_context();
        config.expected_engine_fingerprint
            = tournament_identity::toj_conformance_fingerprint(probe_context);
        config.log = [](std::string const &message)
        {
            std::println("net: {}", message);
        };
        return config;
    }

    std::string join_ids(std::vector<std::uint64_t> const &ids)
    {
        std::string joined;
        for (std::uint64_t id : ids)
        {
            if (!joined.empty())
            {
                joined += ' ';
            }
            joined += std::to_string(id);
        }
        return joined;
    }

    int run_host(HostConfig const &cli)
    {
        install_signal_handlers();
        if (!file_exists(cli.certificate) || !file_exists(cli.private_key))
        {
            std::string error;
            if (!tnet::generate_self_signed_host_cert(cli.certificate, cli.private_key, error))
            {
                std::println(stderr, "certificate generation failed: {}", error);
                return 1;
            }
            std::println("generated {}", cli.certificate);
        }
        std::optional<std::string> const fingerprint = tnet::certificate_fingerprint(cli.certificate);
        if (!fingerprint.has_value())
        {
            std::println(stderr, "cannot read a certificate fingerprint from {}", cli.certificate);
            return 1;
        }

        auto registry = std::make_shared<treg::DeviceRegistry>();
        if (!cli.ban_file.empty())
        {
            tournament_ban::BanFile ban_store(cli.ban_file);
            std::vector<tournament_ban::BanRecord> ban_records;
            std::string ban_error;
            if (!ban_store.load(ban_records, ban_error))
            {
                std::println(stderr, "cannot load ban file: {}", ban_error);
                return 1;
            }
            for (tournament_ban::BanRecord const &record : ban_records)
            {
                registry->ban_key(record.public_key);
            }
            std::println("{} banned device key(s) loaded from {}", ban_records.size(), cli.ban_file);
            registry->set_ban_file(cli.ban_file);
            registry->set_log([](std::string const &message)
            {
                std::println("net: {}", message);
            });
        }
        if (!cli.trust_file.empty())
        {
            std::string trust_error;
            if (registry->load_trust(cli.trust_file, trust_error))
            {
                registry->set_trust_file(cli.trust_file);
                std::println("{} trusted device record(s) loaded from {}",
                             registry->trust_snapshot().size(), cli.trust_file);
            }
            else if (trust_error.find("cannot open") == std::string::npos)
            {
                std::println(stderr, "cannot load trust file: {}", trust_error);
                return 1;
            }
            else
            {
                registry->set_trust_file(cli.trust_file);
                std::println("starting a fresh trust file at {}", cli.trust_file);
            }
        }
        tnet::NetConfig const net_config = net_config_for(cli);
        auto transport = std::make_shared<tnet::HostTransport>(registry, net_config);
        std::string start_error;
        if (!transport->start(start_error))
        {
            std::println(stderr, "transport start failed: {}", start_error);
            return 1;
        }
        std::uint16_t const bound_port = transport->listening_port();
        std::println("listening on {}:{} fingerprint {}", cli.address, bound_port, *fingerprint);
        std::println("engine fingerprint {:016x}", net_config.expected_engine_fingerprint);
        std::println("start clients: remote_client --host {} --port {} --device-id N --key-file K --fingerprint {}",
            cli.address, bound_port, *fingerprint);

        std::uint64_t waited_ms = 0;
        while (registry->active_devices().empty() && waited_ms < cli.wait_clients_ms)
        {
            if (g_stop_requested.load(std::memory_order_relaxed))
            {
                std::println("stopped by signal before any client enrolled");
                transport->stop();
                return 0;
            }
            if (waited_ms % 1000 == 0)
            {
                std::println("waiting for clients, enrolled={}", registry->active_devices().size());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            waited_ms += 250;
        }
        if (registry->active_devices().empty())
        {
            std::println(stderr, "timed out after {} ms waiting for clients", cli.wait_clients_ms);
            transport->stop();
            return 1;
        }

        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        std::vector<trun::RosterEntry> roster;
        roster.reserve(static_cast<std::size_t>(cli.roster_size));
        for (int i = 0; i < cli.roster_size; ++i)
        {
            std::vector<double> theta(schema.defaults.begin(), schema.defaults.end());
            for (double &value : theta)
            {
                value *= 1.0 + 0.05 * static_cast<double>(i);
            }
            roster.push_back(trun::RosterEntry{static_cast<trun::CandidateId>(i + 1), std::move(theta)});
        }

        auto provenance = std::make_shared<tprov::ProvenanceLedger>();
        auto clock = std::make_shared<tt::SystemClock>();
        auto remote_timing = std::make_shared<trem::DeviceTiming>();
        std::shared_ptr<m_tetris::TetrisContext> const local_context
            = tuning_toj::TojAdapter::make_shared_context();
        if (!local_context)
        {
            std::println(stderr, "cannot prepare the local engine context");
            transport->stop();
            return 1;
        }
        TojBackend const local_engine{local_context};
        taud::ReRun const re_run = [&local_engine](std::vector<tuning::BatchGame> const &games,
                                                   tuning::RunConfig const &config)
        {
            return local_engine.run_games(games, config);
        };
        tuning::RunConfig run_config;
        run_config.threads = cli.threads;
        run_config.iterations_per_move = static_cast<std::size_t>(cli.iterations);
        run_config.max_rounds = cli.max_rounds;
        {
            TojBackend const calibrate_engine{local_context};
            tuning::ParamSchema const &schema = tuning_toj::TojAdapter::schema();
            tuning::BatchGame probe;
            probe.id = trun::game_id_for(1, 0);
            probe.theta_a.assign(schema.defaults.begin(), schema.defaults.end());
            probe.theta_b.assign(schema.defaults.begin(), schema.defaults.end());
            probe.seed_a = tuning::derive_game_seed(0xC0FFEEULL, probe.id, 0);
            probe.seed_b = tuning::derive_game_seed(0xC0FFEEULL, probe.id, 1);
            auto const began = std::chrono::steady_clock::now();
            calibrate_engine.run_games({probe}, run_config);
            auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - began).count();
            remote_timing->seed_ms_per_game(static_cast<double>(elapsed));
            std::println("calibrated {} ms per game, lease {} ms, {} games per assignment",
                         elapsed, cli.lease_ms, cli.games_per_assignment);
        }
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = cli.games_per_assignment;
        remote_config.lease_ms = cli.lease_ms;
        remote_config.reconnect_grace_ms = cli.reconnect_grace_ms;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = cli.series_cap;
        remote_config.timing = remote_timing;
        remote_config.log = [](std::string const &message)
        {
            std::println("net: {}", message);
        };
        remote_config.audit_rate = cli.audit_rate;
        remote_config.audit_workers = std::max(1, cli.audit_workers);
        remote_config.stop_requested = []()
        {
            return g_stop_requested.load(std::memory_order_relaxed);
        };
        remote_config.auditor = re_run;
        remote_config.on_liar = [registry, &cli](tw::DeviceId device)
        {
            registry->blacklist(device);
            tw::PublicKey const *banned_key = registry->public_key(device);
            if (banned_key != nullptr && !cli.ban_file.empty())
            {
                tournament_ban::BanRecord ban_record;
                ban_record.device = device;
                ban_record.public_key = *banned_key;
                ban_record.generation = 0;
                ban_record.failed_verdicts = 1;
                ban_record.caught_at_ms = tt::SystemClock().now_ms();
                tournament_ban::BanFile ban_store(cli.ban_file);
                std::string ban_error;
                if (!ban_store.append(ban_record, ban_error))
                {
                    std::println(stderr, "ban file append failed: {}", ban_error);
                }
                registry->ban_key(*banned_key);
            }
            std::println("net: device {} caught lying by the streaming audit and banned", device);
        };
        trem::RemoteBackend backend(schema, transport, registry, provenance, clock, remote_config);
        trun::RunLimits limits;
        limits.wave_limit = 16;
        limits.max_games = cli.max_games;
        limits.max_draws = cli.max_draws;

        trun::TournamentRunner<trem::RemoteBackend> runner(backend, roster, cli.generation_seed, run_config, limits);
        if (!runner.ok())
        {
            std::println(stderr, "tournament rejected: {}", runner.error().detail);
            transport->stop();
            return 1;
        }
        for (;;)
        {
            if (g_stop_requested.load(std::memory_order_relaxed))
            {
                std::println("stopped by signal at a wave boundary");
                transport->stop();
                return 0;
            }
            trun::RunResult const step = runner.run_next_wave();
            if (step.error.code == trun::ErrorCode::BackendStopped)
            {
                std::println("stopped by signal with {} of this wave's game(s) completed, no checkpoint to resume from",
                             runner.total_games());
                transport->stop();
                return 0;
            }
            if (step.error.code != trun::ErrorCode::None)
            {
                std::println(stderr, "tournament wave failed: {}", step.error.detail);
                transport->stop();
                return 1;
            }
            std::int64_t draws = 0;
            for (trun::GameRecord const &record : runner.ledger())
            {
                if (record.winner == tbr::GameWinner::Draw)
                {
                    ++draws;
                }
            }
            std::println("wave {} games {} draws {} ready {}", runner.total_waves(), runner.total_games(),
                draws, runner.bracket().ready_series().size());
            if (step.complete)
            {
                break;
            }
        }

        std::uint64_t champion_id = runner.champion();
        std::vector<trun::CandidateId> standings = runner.standings();
        std::int64_t total_games = runner.total_games();
        std::int64_t total_draws = runner.total_draws();
        std::uint64_t checksum = runner.checksum();
        std::vector<tw::DeviceId> blacklisted_devices;

        try
        {
            std::vector<tw::DeviceId> failed_devices;
            for (auto const *entry : provenance->entries())
            {
                if (registry->blacklisted(entry->device)
                    && std::find(failed_devices.begin(), failed_devices.end(), entry->device)
                        == failed_devices.end())
                {
                    failed_devices.push_back(entry->device);
                }
            }
            std::sort(failed_devices.begin(), failed_devices.end());
            std::println("streaming audit: {} games audited at acceptance, {} device(s) with games to void",
                backend.audited_game_ids().size(), failed_devices.size());

            if (!failed_devices.empty())
            {
                std::set<tw::GameId> voided_ids;
                for (tw::DeviceId device : failed_devices)
                {
                    for (tw::GameId game_id : provenance->games_of_device(device))
                    {
                        voided_ids.insert(game_id);
                    }
                }
                std::vector<tw::GameId> const voided(voided_ids.begin(), voided_ids.end());

                trep::RepairRequest request;
                request.roster = roster;
                request.generation_seed = cli.generation_seed;
                request.config = run_config;
                request.ledger = runner.ledger();
                request.voided_game_ids = voided;
                request.re_run = re_run;
                trep::RepairResult const repaired = trep::repair_ledger(std::move(request));
                if (!repaired.ok)
                {
                    std::println(stderr, "repair failed: {}", repaired.error);
                    transport->stop();
                    return 1;
                }

                trun::TournamentRunner<TojBackend> resumed(local_engine, roster, cli.generation_seed, run_config,
                    limits, repaired.repaired_ledger);
                if (!resumed.ok())
                {
                    std::println(stderr, "resumed tournament rejected: {}", resumed.error().detail);
                    transport->stop();
                    return 1;
                }
                trun::RunResult const resumed_result = resumed.run();
                if (resumed_result.error.code != trun::ErrorCode::None || !resumed_result.complete)
                {
                    std::println(stderr, "resumed tournament failed: {}", resumed_result.error.detail);
                    transport->stop();
                    return 1;
                }
                std::println("repair: voided={} re-ran={} dropped={} diverged={} resumed_games={}",
                    repaired.voided_games, repaired.re_run_games, repaired.dropped_games, 0,
                    repaired.repaired_ledger.size());

                champion_id = resumed.champion();
                standings = resumed.standings();
                total_games = resumed.total_games();
                total_draws = resumed.total_draws();
                checksum = resumed.checksum();
                blacklisted_devices = failed_devices;
            }
        }
        catch (std::exception const &error)
        {
            std::println(stderr, "audit or repair failed: {}", error.what());
            transport->stop();
            return 1;
        }

        std::println("champion {}", champion_id);
        std::println("standings {}", join_ids(standings));
        std::println("games {} draws {}", total_games, total_draws);
        std::println("ledger checksum {:016x}", checksum);
        std::vector<tw::DeviceId> const active_devices = registry->active_devices();
        std::set<tw::DeviceId> enrolled(active_devices.begin(), active_devices.end());
        enrolled.insert(blacklisted_devices.begin(), blacklisted_devices.end());
        for (tw::DeviceId device : enrolled)
        {
            treg::DeviceStats const *stats = registry->stats(device);
            if (stats == nullptr)
            {
                continue;
            }
            std::println("device {} accepted={} dropped={} audits_passed={} audits_failed={} blacklisted={}",
                device, stats->games_accepted, stats->games_dropped, stats->audits_passed,
                stats->audits_failed, stats->blacklisted ? 1 : 0);
        }

        transport->stop();
        return 0;
    }
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);
    tournament_host::HostConfig config;
    std::string config_path = "tournament_host.json";
    bool config_explicit = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 >= argc)
            {
                std::println(stderr, "flag --config requires a value");
                tournament_host::print_usage(argc > 0 ? argv[0] : "tournament_host");
                return 1;
            }
            config_path = argv[++i];
            config_explicit = true;
        }
    }
    std::error_code config_probe;
    if (std::filesystem::exists(config_path, config_probe) || config_explicit)
    {
        std::string config_error;
        std::optional<tournament_config::ConfigFile> const loaded
            = tournament_config::load(config_path, config_error);
        if (!loaded.has_value()
            || !tournament_host::apply_config_file(config, loaded->values, config_error))
        {
            std::println(stderr, "config: {}", config_error);
            return 1;
        }
        std::println("config: loaded {}", config_path);
    }
    if (!tournament_host::parse_flags(argc, argv, config))
    {
        return 1;
    }
    if (!config.unban_key.empty())
    {
        return tournament_host::run_unban(config.unban_key.c_str(), config.ban_file,
                                          config.trust_file);
    }
    return tournament_host::run_host(config);
}
