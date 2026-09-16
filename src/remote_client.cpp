#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include "tournament/net_transport.h"
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
    constexpr std::uint64_t kBackoffStepsMs[] = {1000, 2000, 4000, 8000, 15000, 30000};

    struct ClientConfig
    {
        std::string host;
        std::uint16_t port = 0;
        std::uint64_t device_id = 0;
        std::string key_file;
        std::string fingerprint;
        bool quiet = false;
        bool generate_key = false;
    };

    void print_usage(char const *program)
    {
        std::println(stderr, "Usage:");
        std::println(stderr, "  {} --generate-key --key-file PATH", program);
        std::println(stderr, "  {} --host HOST --port PORT --device-id N --key-file PATH --fingerprint FP [--quiet]", program);
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

    int run_assignment_loop(tnet::ClientConnection &connection, TojBackend const &backend,
                            tw::KeyPair const &keys, bool quiet)
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
                batch.outcomes.push_back(tw::to_wire(outcome));
            }
            tw::SignedResult const signed_result{batch, tw::sign_result(keys, batch)};
            std::string send_detail;
            if (!connection.send_result(signed_result, send_detail))
            {
                std::println(stderr, "result send failed: {}", send_detail);
                return 0;
            }
            if (!quiet)
            {
                std::println(stderr, "assignment nonce={} games={} ok", batch.nonce, batch.outcomes.size());
            }
        }
    }

    int run_client(ClientConfig const &cli)
    {
        tw::KeyPair keys;
        if (!load_key_pair(cli.key_file, keys))
        {
            return 1;
        }
        tuning::ParamSchema const schema = tuning_toj::TojAdapter::schema();
        tw::HelloMessage hello;
        hello.device = cli.device_id;
        hello.public_key = keys.public_key;
        hello.protocol = tw::protocol_version;
        hello.adapter_id = std::string(schema.adapter_id);
        hello.schema_hash = tuning::schema_hash(schema);
        auto shared_context = tuning_toj::TojAdapter::make_shared_context();
        if (!shared_context)
        {
            std::println(stderr, "cannot prepare shared TOJ context");
            return 1;
        }
        TojBackend const backend(shared_context);
        std::size_t backoff_step = 0;
        for (;;)
        {
            tnet::ClientConnection connection(cli.host, cli.port, cli.fingerprint);
            std::string detail;
            tnet::ClientConnection::HelloStatus status = tnet::ClientConnection::HelloStatus::Failed;
            if (connection.connected())
            {
                status = connection.send_hello(hello, detail);
            }
            else
            {
                detail = "tls connect failed";
            }
            if (status == tnet::ClientConnection::HelloStatus::Rejected)
            {
                std::println(stderr, "host refused this device: {}", detail);
                return 1;
            }
            if (status != tnet::ClientConnection::HelloStatus::Accepted)
            {
                std::println(stderr, "connect to {}:{} failed: {}", cli.host, cli.port, detail);
                sleep_backoff(backoff_step);
                continue;
            }
            backoff_step = 0;
            int const loop_result = run_assignment_loop(connection, backend, keys, cli.quiet);
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
    bool have_host = false;
    bool have_port = false;
    bool have_device = false;
    bool have_key = false;
    bool have_fingerprint = false;
    for (int i = 1; i < argc; ++i)
    {
        char const *flag = argv[i];
        if (std::strcmp(flag, "--generate-key") == 0)
        {
            cli.generate_key = true;
        }
        else if (std::strcmp(flag, "--quiet") == 0)
        {
            cli.quiet = true;
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
        return remote_client::run_generate_key(cli.key_file);
    }
    if (!have_host || !have_port || !have_device || !have_key || !have_fingerprint)
    {
        std::println(stderr, "run mode requires --host --port --device-id --key-file --fingerprint");
        remote_client::print_usage(program);
        return 1;
    }
    return remote_client::run_client(cli);
}
