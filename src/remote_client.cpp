#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tournament/config_file.h"
#include "tournament/engine_identity.h"
#include "tournament/net_transport.h"
#include "tournament/toj_conformance.h"
#include "tournament/wire.h"
#include "tuning/engine_match.h"
#include "tuning/toj_adapter.h"

namespace remote_client
{
    namespace tw = tournament_wire;
    namespace tnet = tournament_net;
    using TojBackend = tuning::EngineMatchBackend<tuning_toj::TojAdapter>;

    constexpr std::size_t kKeyComponentSize = 32;
    constexpr std::size_t kKeyFileSize = 64;
    constexpr std::uint64_t kAssignmentTimeoutMs = 600000;
    constexpr std::uint32_t kMaxConcurrentAssignments = 64;
    constexpr std::uint64_t kBackoffStepsMs[] = {1000, 2000, 4000, 8000, 15000, 30000};

    struct ClientConfig
    {
        std::string host;
        std::uint16_t port = 0;
        std::uint64_t device_id = 0;
        std::uint32_t max_concurrent_assignments = 1;
        std::string key_file;
        std::string fingerprint;
        bool quiet = false;
        bool flip_outcomes = false;
        bool generate_key = false;
        bool websocket = false;
        bool ca_verified = false;
        std::uint64_t heartbeat_ms = 30000;
    };

    void print_usage(char const *program)
    {
        std::println(stderr, "Usage:");
        std::println(stderr, "  {} --generate-key --key-file PATH", program);
        std::println(stderr, "  {} --host HOST --port PORT --device-id N --key-file PATH --fingerprint FP [--assignments N] [--quiet] [--flip-outcomes]", program);
        std::println(stderr, "  {} --host HOST --port PORT --device-id N --key-file PATH --ws [--ca] [--assignments N] [--quiet]", program);
        std::println(stderr, "  --assignments N is the number of assignments the client processes concurrently and advertises to the host (default 1, 1..64)");
        std::println(stderr, "  --flip-outcomes is test-only: it flips every game outcome before signing");
        std::println(stderr, "  --ws speaks WebSocket instead of the raw protocol, for hosts reached through an HTTPS proxy such as Cloudflare");
        std::println(stderr, "  --ca with --ws verifies the server certificate against system roots for the hostname, for proxied connections; without it --ws pins the host fingerprint like the raw protocol");
        std::println(stderr, "  --config P reads run settings from a json file (remote_client.json in the working directory is read automatically), command line values override the file");
        std::println(stderr, "  config keys: host, port, device_id, key_file, fingerprint, assignments, quiet, flip_outcomes, websocket, ca, heartbeat_ms");
        std::println(stderr, "  --heartbeat-ms N sends a websocket ping every N ms while idle (default 30000, 0 disables) so proxies keep the connection open");
    }

    bool apply_config_file(ClientConfig &cli, nlohmann::json const &values, std::string &error)
    {
        std::vector<std::string> const allowed{
            "host", "port", "device_id", "key_file", "fingerprint", "assignments", "quiet",
            "flip_outcomes", "websocket", "ca", "heartbeat_ms",
        };
        if (!tournament_config::reject_unknown_keys(values, allowed, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "host", cli.host, error))
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
            if (port < 1 || port > 65535)
            {
                error = "port must be between 1 and 65535";
                return false;
            }
            cli.port = static_cast<std::uint16_t>(port);
        }
        if (!tournament_config::get_u64(values, "device_id", cli.device_id, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "key_file", cli.key_file, error))
        {
            return false;
        }
        if (!tournament_config::get_string(values, "fingerprint", cli.fingerprint, error))
        {
            return false;
        }
        if (values.contains("assignments"))
        {
            std::uint64_t assignments = 0;
            if (!tournament_config::get_u64(values, "assignments", assignments, error))
            {
                return false;
            }
            if (assignments < 1 || assignments > kMaxConcurrentAssignments)
            {
                error = "assignments must be between 1 and 64";
                return false;
            }
            cli.max_concurrent_assignments = static_cast<std::uint32_t>(assignments);
        }
        if (!tournament_config::get_bool(values, "quiet", cli.quiet, error))
        {
            return false;
        }
        if (!tournament_config::get_bool(values, "flip_outcomes", cli.flip_outcomes, error))
        {
            return false;
        }
        if (!tournament_config::get_bool(values, "websocket", cli.websocket, error))
        {
            return false;
        }
        if (!tournament_config::get_bool(values, "ca", cli.ca_verified, error))
        {
            return false;
        }
        if (!tournament_config::get_u64(values, "heartbeat_ms", cli.heartbeat_ms, error))
        {
            return false;
        }
        return true;
    }

    int missing_value_error(char const *flag, char const *program)
    {
        std::println(stderr, "missing value for {}", flag);
        print_usage(program);
        return 1;
    }

    std::optional<std::uint64_t> parse_u64(char const *text)
    {
        std::string const value(text);
        if (value.empty() || value.front() < '0' || value.front() > '9')
        {
            return std::nullopt;
        }
        try
        {
            return std::stoull(value);
        }
        catch (std::exception const &)
        {
            return std::nullopt;
        }
    }

    int run_generate_key(std::string const &key_file)
    {
        tw::KeyPair const keys = tw::generate_keypair();
        if (keys.public_key.size() != kKeyComponentSize || keys.secret_key.size() != kKeyComponentSize)
        {
            std::println(stderr, "key generation produced unexpected key sizes");
            return 1;
        }
        std::vector<std::uint8_t> blob;
        blob.reserve(kKeyFileSize);
        blob.insert(blob.end(), keys.public_key.begin(), keys.public_key.end());
        blob.insert(blob.end(), keys.secret_key.begin(), keys.secret_key.end());
        std::ofstream output(key_file, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            std::println(stderr, "cannot open {} for writing", key_file);
            return 1;
        }
        output.write(reinterpret_cast<char const *>(blob.data()),
                     static_cast<std::streamsize>(blob.size()));
        output.close();
        if (!output.good())
        {
            std::println(stderr, "cannot write {}", key_file);
            return 1;
        }
        std::println(stderr, "wrote tournament device key file {}", key_file);
        return 0;
    }

    bool load_key_pair(std::string const &key_file, tw::KeyPair &keys)
    {
        std::ifstream input(key_file, std::ios::binary | std::ios::ate);
        if (!input.is_open())
        {
            std::println(stderr, "cannot open key file {}", key_file);
            return false;
        }
        std::streamoff const size = input.tellg();
        if (size != static_cast<std::streamoff>(kKeyFileSize))
        {
            std::println(stderr, "key file {} must hold exactly {} bytes", key_file, kKeyFileSize);
            return false;
        }
        input.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> raw(kKeyFileSize);
        input.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (static_cast<std::size_t>(input.gcount()) != kKeyFileSize)
        {
            std::println(stderr, "cannot read key file {}", key_file);
            return false;
        }
        keys.public_key.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(kKeyComponentSize));
        keys.secret_key.assign(raw.begin() + static_cast<std::ptrdiff_t>(kKeyComponentSize), raw.end());
        return true;
    }

    void sleep_backoff(std::size_t &step)
    {
        constexpr std::size_t last = sizeof(kBackoffStepsMs) / sizeof(kBackoffStepsMs[0]) - 1;
        std::size_t const index = std::min(step, last);
        std::this_thread::sleep_for(std::chrono::milliseconds(kBackoffStepsMs[index]));
        step = std::min(step + 1, last);
    }

    bool detail_indicates_closed(std::string const &detail)
    {
        return detail.find("closed") != std::string::npos
            || detail.rfind("not connected", 0) == 0;
    }

    tw::WireOutcome flip_wire_outcome(tw::WireOutcome const &outcome)
    {
        tw::WireOutcome flipped = outcome;
        if (outcome.winner > 0)
        {
            flipped.winner = -1;
            flipped.reason = tuning::WinReason::BSurvivor;
        }
        else if (outcome.winner < 0)
        {
            flipped.winner = 1;
            flipped.reason = tuning::WinReason::ASurvivor;
        }
        else
        {
            flipped.winner = 1;
            flipped.reason = outcome.capped ? tuning::WinReason::ACapApl : tuning::WinReason::ABothDeadApl;
        }
        return flipped;
    }

    int run_assignment_loop(tnet::ClientConnectionBase &connection, TojBackend const &backend,
                            tw::KeyPair const &keys, ClientConfig const &cli)
    {
        for (;;)
        {
            std::string detail;
            std::optional<tw::AssignmentBatch> assignment
                = connection.next_assignment(kAssignmentTimeoutMs, detail);
            if (!assignment)
            {
                if (detail.rfind("rejected: ", 0) == 0)
                {
                    std::println(stderr, "host refused this device: {}", detail);
                    return 1;
                }
                if (detail == "timed out waiting for frame")
                {
                    continue;
                }
                if (detail_indicates_closed(detail))
                {
                    std::println(stderr, "connection lost: {}", detail);
                    return 0;
                }
                std::println(stderr, "assignment wait failed: {}", detail);
                continue;
            }
            std::vector<tuning::BatchGame> games;
            games.reserve(assignment->games.size());
            for (tw::WireGame const &wire_game : assignment->games)
            {
                games.push_back(tw::from_wire(wire_game));
            }
            std::vector<tuning::GameOutcome> outcomes;
            try
            {
                outcomes = backend.run_games(games, assignment->config);
            }
            catch (std::exception const &error)
            {
                std::println(stderr, "assignment nonce={} failed: {}", assignment->nonce, error.what());
                return 1;
            }
            tw::ResultBatch batch;
            batch.nonce = assignment->nonce;
            batch.device = assignment->device;
            batch.outcomes.reserve(outcomes.size());
            for (tuning::GameOutcome const &outcome : outcomes)
            {
                tw::WireOutcome wire_outcome = tw::to_wire(outcome);
                batch.outcomes.push_back(cli.flip_outcomes ? flip_wire_outcome(wire_outcome)
                                                           : wire_outcome);
            }
            tw::SignedResult const signed_result{batch, tw::sign_result(keys, batch)};
            std::string send_detail;
            if (!connection.send_result(signed_result, send_detail))
            {
                std::println(stderr, "result send failed: {}", send_detail);
                return 0;
            }
            if (!cli.quiet)
            {
                std::println(stderr, "assignment nonce={} games={} ok", batch.nonce, batch.outcomes.size());
            }
        }
    }

    enum class LoopStop
    {
        None,
        Reconnect,
        Fatal,
    };

    struct AssignmentQueue
    {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<tw::AssignmentBatch> pending;
        LoopStop stop = LoopStop::None;
    };

    bool stop_requested(AssignmentQueue &queue)
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        return queue.stop != LoopStop::None;
    }

    bool request_stop(AssignmentQueue &queue, LoopStop reason)
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        if (queue.stop != LoopStop::None)
        {
            return false;
        }
        queue.stop = reason;
        queue.ready.notify_all();
        return true;
    }

    int run_assignment_loop_concurrent(tnet::ClientConnectionBase &connection, TojBackend const &backend,
                                       tw::KeyPair const &keys, ClientConfig const &cli)
    {
        AssignmentQueue queue;
        std::mutex connection_close_mutex;
        bool connection_closed = false;
        auto close_connection_once = [&]()
        {
            std::lock_guard<std::mutex> lock(connection_close_mutex);
            if (connection_closed)
            {
                return;
            }
            connection_closed = true;
            connection.close();
        };
        auto worker_main = [&]()
        {
            for (;;)
            {
                tw::AssignmentBatch assignment;
                {
                    std::unique_lock<std::mutex> lock(queue.mutex);
                    queue.ready.wait(lock, [&]()
                    {
                        return !queue.pending.empty() || queue.stop != LoopStop::None;
                    });
                    if (queue.stop != LoopStop::None)
                    {
                        return;
                    }
                    assignment = std::move(queue.pending.front());
                    queue.pending.pop_front();
                }
                std::vector<tuning::BatchGame> games;
                games.reserve(assignment.games.size());
                for (tw::WireGame const &wire_game : assignment.games)
                {
                    games.push_back(tw::from_wire(wire_game));
                }
                std::vector<tuning::GameOutcome> outcomes;
                try
                {
                    outcomes = backend.run_games(games, assignment.config);
                }
                catch (std::exception const &error)
                {
                    if (request_stop(queue, LoopStop::Fatal))
                    {
                        std::println(stderr, "assignment nonce={} failed: {}", assignment.nonce, error.what());
                        close_connection_once();
                    }
                    return;
                }
                if (stop_requested(queue))
                {
                    return;
                }
                tw::ResultBatch batch;
                batch.nonce = assignment.nonce;
                batch.device = assignment.device;
                batch.outcomes.reserve(outcomes.size());
                for (tuning::GameOutcome const &outcome : outcomes)
                {
                    tw::WireOutcome wire_outcome = tw::to_wire(outcome);
                    batch.outcomes.push_back(cli.flip_outcomes ? flip_wire_outcome(wire_outcome)
                                                               : wire_outcome);
                }
                tw::SignedResult const signed_result{batch, tw::sign_result(keys, batch)};
                std::string send_detail;
                if (!connection.send_result(signed_result, send_detail))
                {
                    if (request_stop(queue, LoopStop::Reconnect))
                    {
                        std::println(stderr, "result send failed: {}", send_detail);
                        close_connection_once();
                    }
                    return;
                }
                if (!cli.quiet)
                {
                    std::println(stderr, "assignment nonce={} games={} ok", batch.nonce, batch.outcomes.size());
                }
            }
        };
        std::vector<std::thread> workers;
        workers.reserve(cli.max_concurrent_assignments);
        for (std::uint32_t index = 0; index < cli.max_concurrent_assignments; ++index)
        {
            workers.emplace_back(worker_main);
        }
        for (;;)
        {
            if (stop_requested(queue))
            {
                break;
            }
            std::string detail;
            std::optional<tw::AssignmentBatch> assignment
                = connection.next_assignment(kAssignmentTimeoutMs, detail);
            if (!assignment)
            {
                if (stop_requested(queue))
                {
                    break;
                }
                if (detail.rfind("rejected: ", 0) == 0)
                {
                    if (request_stop(queue, LoopStop::Fatal))
                    {
                        std::println(stderr, "host refused this device: {}", detail);
                    }
                    break;
                }
                if (detail == "timed out waiting for frame")
                {
                    continue;
                }
                if (detail_indicates_closed(detail))
                {
                    if (request_stop(queue, LoopStop::Reconnect))
                    {
                        std::println(stderr, "connection lost: {}", detail);
                    }
                    break;
                }
                std::println(stderr, "assignment wait failed: {}", detail);
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(queue.mutex);
                if (queue.stop != LoopStop::None)
                {
                    break;
                }
                queue.pending.push_back(std::move(*assignment));
            }
            queue.ready.notify_one();
        }
        close_connection_once();
        for (std::thread &worker : workers)
        {
            worker.join();
        }
        return queue.stop == LoopStop::Fatal ? 1 : 0;
    }

    int run_client(ClientConfig const &cli)
    {
        tw::KeyPair keys;
        if (!load_key_pair(cli.key_file, keys))
        {
            return 1;
        }
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        auto shared_context = tuning_toj::TojAdapter::make_shared_context();
        if (!shared_context)
        {
            std::println(stderr, "cannot prepare shared TOJ context");
            return 1;
        }
        TojBackend const backend(shared_context);
        tw::HelloMessage hello;
        hello.device = cli.device_id;
        hello.public_key = keys.public_key;
        hello.protocol = tw::protocol_version;
        hello.adapter_id = std::string(schema.adapter_id);
        hello.schema_hash = tuning::schema_hash(schema);
        hello.engine_fingerprint = tournament_identity::toj_conformance_fingerprint(shared_context);
        hello.max_concurrent_assignments = cli.max_concurrent_assignments;
        std::size_t backoff_step = 0;
        for (;;)
        {
            std::unique_ptr<tnet::ClientConnectionBase> connection;
            if (cli.websocket)
            {
                if (cli.ca_verified)
                {
                    connection = std::make_unique<tnet::WsClientConnection>(cli.host, cli.port,
                                                                             std::string(), "/tournament",
                                                                             true,
                                                                             cli.heartbeat_ms);
                }
                else
                {
                    connection = std::make_unique<tnet::WsClientConnection>(cli.host, cli.port,
                                                                             cli.fingerprint, "/",
                                                                             false,
                                                                             cli.heartbeat_ms);
                }
            }
            else
            {
                connection = std::make_unique<tnet::ClientConnection>(cli.host, cli.port,
                                                                       cli.fingerprint);
            }
            std::string detail;
            tnet::ClientConnectionBase::HelloStatus status = tnet::ClientConnectionBase::HelloStatus::Failed;
            if (connection->connected())
            {
                status = connection->send_hello(hello, detail);
            }
            else
            {
                detail = cli.websocket ? "websocket connect failed" : "tls connect failed";
            }
            if (status == tnet::ClientConnectionBase::HelloStatus::Rejected)
            {
                std::println(stderr, "host refused this device: {}", detail);
                return 1;
            }
            if (status != tnet::ClientConnectionBase::HelloStatus::Accepted)
            {
                std::println(stderr, "connect to {}:{} failed: {}", cli.host, cli.port, detail);
                sleep_backoff(backoff_step);
                continue;
            }
            backoff_step = 0;
            int const loop_result = cli.max_concurrent_assignments > 1
                ? run_assignment_loop_concurrent(*connection, backend, keys, cli)
                : run_assignment_loop(*connection, backend, keys, cli);
            if (loop_result != 0)
            {
                return loop_result;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);
    char const *program = argc > 0 ? argv[0] : "remote_client";
    remote_client::ClientConfig cli;
    std::string config_path = "remote_client.json";
    bool config_explicit = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error("--config", program);
            }
            config_path = argv[++i];
            config_explicit = true;
        }
    }
    std::error_code config_probe;
    bool const config_present = std::filesystem::exists(config_path, config_probe);
    bool have_device = false;
    if (config_present || config_explicit)
    {
        std::string config_error;
        std::optional<tournament_config::ConfigFile> const loaded
            = tournament_config::load(config_path, config_error);
        if (!loaded.has_value()
            || !remote_client::apply_config_file(cli, loaded->values, config_error))
        {
            std::println(stderr, "config: {}", config_error);
            remote_client::print_usage(program);
            return 1;
        }
        have_device = loaded->values.contains("device_id");
        std::println("config: loaded {}", config_path);
    }
    bool have_host = !cli.host.empty();
    bool have_port = cli.port != 0;
    bool have_key = !cli.key_file.empty();
    bool have_fingerprint = !cli.fingerprint.empty();
    for (int i = 1; i < argc; ++i)
    {
        char const *flag = argv[i];
        if (std::strcmp(flag, "--config") == 0)
        {
            if (i + 1 < argc)
            {
                ++i;
            }
            continue;
        }
        if (std::strcmp(flag, "--generate-key") == 0)
        {
            cli.generate_key = true;
        }
        else if (std::strcmp(flag, "--quiet") == 0)
        {
            cli.quiet = true;
        }
        else if (std::strcmp(flag, "--ws") == 0)
        {
            cli.websocket = true;
        }
        else if (std::strcmp(flag, "--ca") == 0)
        {
            cli.ca_verified = true;
        }
        else if (std::strcmp(flag, "--flip-outcomes") == 0)
        {
            cli.flip_outcomes = true;
        }
        else if (std::strcmp(flag, "--host") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            cli.host = argv[++i];
            have_host = true;
        }
        else if (std::strcmp(flag, "--port") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            std::optional<std::uint64_t> parsed = remote_client::parse_u64(argv[++i]);
            if (!parsed || *parsed < 1 || *parsed > 65535)
            {
                std::println(stderr, "invalid port {}", argv[i]);
                return 1;
            }
            cli.port = static_cast<std::uint16_t>(*parsed);
            have_port = true;
        }
        else if (std::strcmp(flag, "--device-id") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            std::optional<std::uint64_t> parsed = remote_client::parse_u64(argv[++i]);
            if (!parsed)
            {
                std::println(stderr, "invalid device id {}", argv[i]);
                return 1;
            }
            cli.device_id = *parsed;
            have_device = true;
        }
        else if (std::strcmp(flag, "--key-file") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            cli.key_file = argv[++i];
            have_key = true;
        }
        else if (std::strcmp(flag, "--fingerprint") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            cli.fingerprint = argv[++i];
            have_fingerprint = true;
        }
        else if (std::strcmp(flag, "--assignments") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            std::optional<std::uint64_t> parsed = remote_client::parse_u64(argv[++i]);
            if (!parsed || *parsed < 1 || *parsed > remote_client::kMaxConcurrentAssignments)
            {
                std::println(stderr, "invalid assignment count {}", argv[i]);
                remote_client::print_usage(program);
                return 1;
            }
            cli.max_concurrent_assignments = static_cast<std::uint32_t>(*parsed);
        }
        else if (std::strcmp(flag, "--heartbeat-ms") == 0)
        {
            if (i + 1 >= argc)
            {
                return remote_client::missing_value_error(flag, program);
            }
            std::optional<std::uint64_t> parsed = remote_client::parse_u64(argv[++i]);
            if (!parsed)
            {
                std::println(stderr, "invalid heartbeat {}", argv[i]);
                remote_client::print_usage(program);
                return 1;
            }
            cli.heartbeat_ms = *parsed;
        }
        else
        {
            std::println(stderr, "unknown flag: {}", flag);
            remote_client::print_usage(program);
            return 1;
        }
    }
    if (cli.generate_key)
    {
        if (!have_key)
        {
            std::println(stderr, "--generate-key requires --key-file");
            remote_client::print_usage(program);
            return 1;
        }
        if (cli.flip_outcomes)
        {
            std::println(stderr, "--flip-outcomes is not valid with --generate-key");
            remote_client::print_usage(program);
            return 1;
        }
        return remote_client::run_generate_key(cli.key_file);
    }
    if (!have_host || !have_port || !have_device || !have_key)
    {
        std::println(stderr, "run mode requires host, port, device id and key file, from flags or the config file");
        remote_client::print_usage(program);
        return 1;
    }
    if (cli.ca_verified && !cli.websocket)
    {
        std::println(stderr, "--ca requires --ws");
        remote_client::print_usage(program);
        return 1;
    }
    if (!have_fingerprint && !(cli.websocket && cli.ca_verified))
    {
        std::println(stderr, "run mode requires --fingerprint, or --ws --ca for proxied connections");
        remote_client::print_usage(program);
        return 1;
    }
    return remote_client::run_client(cli);
}
