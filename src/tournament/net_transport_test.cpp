#include "tournament/bytes.h"
#include "tournament/net_transport.h"

#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/remote_backend.h"
#include "tournament/runner.h"
#include "tournament/transport.h"
#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/match.h"

#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    namespace tw = tournament_wire;
    namespace tt = tournament_transport;
    namespace treg = tournament_registry;
    namespace tprov = tournament_provenance;
    namespace tnet = tournament_net;
    namespace trem = tournament_remote;
    namespace trun = tournament_runner;

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

    std::filesystem::path const &temp_dir()
    {
        static std::filesystem::path const path = []()
        {
            auto const stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            auto const created = std::filesystem::temp_directory_path()
                / ("tournament_net_transport_test_" + std::to_string(stamp));
            std::filesystem::create_directories(created);
            return created;
        }();
        return path;
    }

    std::string read_file(std::filesystem::path const &path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    void write_file(std::filesystem::path const &path, std::string const &bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    tuning::ParamSchema test_schema()
    {
        static std::string const adapter = std::string("test_adapter");
        static std::vector<char const *> const names = {"weights_a", "weights_b"};
        static std::vector<double> const scales = {1.0, 1.0};
        static std::vector<double> const defaults = {0.5, 0.5};
        return tuning::ParamSchema{
            adapter,
            std::span<char const * const>(names.data(), names.size()),
            std::span<double const>(scales.data(), scales.size()),
            std::span<double const>(defaults.data(), defaults.size())};
    }

    std::vector<double> theta_a()
    {
        return {0.5, 0.5};
    }

    std::vector<double> theta_b()
    {
        return {0.75, 0.25};
    }

    tuning::RunConfig fast_config()
    {
        tuning::RunConfig config;
        config.threads = 1;
        config.iterations_per_move = 8;
        config.max_rounds = 80;
        return config;
    }

    tw::WireOutcome fabricate_outcome(tw::GameId game_id)
    {
        tw::WireOutcome outcome;
        outcome.id = game_id;
        outcome.winner = (game_id % 2 == 0) ? 1 : -1;
        outcome.reason = outcome.winner == 1 ? tuning::WinReason::ASurvivor : tuning::WinReason::BSurvivor;
        outcome.rounds = 10 + static_cast<int>(game_id) % 80;
        outcome.app_a = 1.25;
        outcome.app_b = 0.75;
        outcome.apl_a = 2.5;
        outcome.apl_b = 1.5;
        outcome.dead_a = false;
        outcome.dead_b = true;
        outcome.capped = false;
        return outcome;
    }

    std::vector<tuning::BatchGame> make_games(std::uint64_t series, std::uint64_t count, std::uint64_t seed_base)
    {
        std::vector<tuning::BatchGame> games;
        games.reserve(count);
        for (std::uint64_t i = 0; i < count; ++i)
        {
            tuning::BatchGame game;
            game.id = trun::game_id_for(static_cast<int>(series), static_cast<int>(i));
            game.theta_a = theta_a();
            game.theta_b = theta_b();
            game.seed_a = tuning::derive_game_seed(seed_base, game.id, 0);
            game.seed_b = tuning::derive_game_seed(seed_base, game.id, 1);
            games.push_back(std::move(game));
        }
        return games;
    }

    tw::AssignmentBatch make_assignment(tw::Nonce nonce, tw::DeviceId device, std::uint64_t series,
                                        std::uint64_t count)
    {
        tw::AssignmentBatch batch;
        batch.nonce = nonce;
        batch.device = device;
        batch.config = fast_config();
        for (tuning::BatchGame const &game : make_games(series, count, series * 1000 + 7))
        {
            batch.games.push_back(tw::to_wire(game));
        }
        return batch;
    }

    struct HostHarness
    {
        std::string name;
        std::shared_ptr<treg::DeviceRegistry> registry = std::make_shared<treg::DeviceRegistry>();
        std::shared_ptr<tprov::ProvenanceLedger> provenance = std::make_shared<tprov::ProvenanceLedger>();
        tnet::NetConfig config;
        std::shared_ptr<tnet::HostTransport> transport;
        std::string fingerprint;

        explicit HostHarness(std::string harness_name)
            : name(std::move(harness_name))
        {
        }

        bool start(tnet::NetConfig net_config)
        {
            config = net_config;
            std::string const cert_path = (temp_dir() / (name + "_host.pem")).string();
            std::string const key_path = (temp_dir() / (name + "_host_key.pem")).string();
            std::string error;
            if (!tnet::generate_self_signed_host_cert(cert_path, key_path, error))
            {
                std::println("certificate generation failed for {}: {}", name, error);
                return false;
            }
            config.certificate_path = cert_path;
            config.private_key_path = key_path;
            auto const pinned = tnet::certificate_fingerprint(cert_path);
            if (!pinned)
            {
                std::println("fingerprint derivation failed for {}", cert_path);
                return false;
            }
            fingerprint = *pinned;
            transport = std::make_shared<tnet::HostTransport>(registry, config);
            return transport->start(error);
        }
    };

    struct ClientHarness
    {
        tw::DeviceId id = 0;
        tw::KeyPair keys{};
        std::unique_ptr<tnet::ClientConnectionBase> conn;
        tnet::ClientConnection::HelloStatus status = tnet::ClientConnection::HelloStatus::Failed;
        std::string detail;
    };

    void send_intro_hello(ClientHarness &client, std::string const &adapter, std::uint32_t protocol,
                          std::uint64_t schema_hash, std::uint64_t engine_fingerprint, std::uint32_t concurrency)
    {
        tw::HelloMessage hello;
        hello.device = client.id;
        hello.public_key = client.keys.public_key;
        hello.protocol = protocol;
        hello.adapter_id = adapter;
        hello.schema_hash = schema_hash;
        hello.engine_fingerprint = engine_fingerprint;
        hello.max_concurrent_assignments = concurrency;
        client.status = client.conn->send_hello(hello, client.detail);
    }

    ClientHarness make_client(HostHarness &host, tw::DeviceId id, std::string const &adapter = "test_adapter",
                              std::uint32_t protocol = tw::protocol_version, std::uint64_t schema_hash = 0,
                              tw::KeyPair const *keys = nullptr, std::uint64_t engine_fingerprint = 0,
                              std::uint32_t concurrency = 0)
    {
        ClientHarness client;
        client.id = id;
        client.keys = keys != nullptr ? *keys : tw::generate_keypair();
        client.conn = std::make_unique<tnet::ClientConnection>("127.0.0.1", host.transport->listening_port(),
                                                               host.fingerprint);
        if (!client.conn->connected())
        {
            client.detail = "tls connection failed";
            return client;
        }
        send_intro_hello(client, adapter, protocol, schema_hash, engine_fingerprint, concurrency);
        return client;
    }

    ClientHarness make_ws_client(HostHarness &host, tw::DeviceId id, std::string const &adapter = "test_adapter",
                                 std::uint32_t protocol = tw::protocol_version, std::uint64_t schema_hash = 0,
                                 tw::KeyPair const *keys = nullptr, std::uint64_t engine_fingerprint = 0,
                                 std::uint32_t concurrency = 0)
    {
        ClientHarness client;
        client.id = id;
        client.keys = keys != nullptr ? *keys : tw::generate_keypair();
        client.conn = std::make_unique<tnet::WsClientConnection>("127.0.0.1", host.transport->listening_port(),
                                                                 host.fingerprint);
        if (!client.conn->connected())
        {
            client.detail = "websocket connection failed";
            return client;
        }
        send_intro_hello(client, adapter, protocol, schema_hash, engine_fingerprint, concurrency);
        return client;
    }

    bool devices_contain(tnet::HostTransport const &host, tw::DeviceId id)
    {
        for (int attempt = 0; attempt < 200; ++attempt)
        {
            std::vector<tw::DeviceId> const devices = host.devices();
            if (devices.size() == 1 && devices[0] == id)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    bool reply_with_fabricated(ClientHarness &client, tw::AssignmentBatch const &assignment, std::string &detail)
    {
        tw::ResultBatch batch;
        batch.nonce = assignment.nonce;
        batch.device = assignment.device;
        for (tw::WireGame const &game : assignment.games)
        {
            batch.outcomes.push_back(fabricate_outcome(game.id));
        }
        tw::SignedResult const signed_result{batch, tw::sign_result(client.keys, batch)};
        return client.conn->send_result(signed_result, detail);
    }

    void honest_worker(ClientHarness &client)
    {
        for (;;)
        {
            std::string detail;
            auto assignment = client.conn->next_assignment(2000, detail);
            if (!assignment)
            {
                return;
            }
            if (!reply_with_fabricated(client, *assignment, detail))
            {
                return;
            }
        }
    }

    bool outcomes_match_fabrication(std::vector<tuning::GameOutcome> const &results,
                                    std::vector<tuning::BatchGame> const &games)
    {
        if (results.size() != games.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            if (tw::encode_outcome(tw::to_wire(results[i])) != tw::encode_outcome(fabricate_outcome(games[i].id)))
            {
                return false;
            }
        }
        return true;
    }

    bool stop_completes(std::shared_ptr<tnet::HostTransport> const &transport, std::uint64_t budget_ms)
    {
        auto finished = std::make_shared<std::atomic<bool>>(false);
        std::thread stopper([transport, finished]()
        {
            transport->stop();
            finished->store(true);
        });
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budget_ms);
        while (!finished->load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        bool const completed = finished->load();
        if (completed)
        {
            stopper.join();
        }
        else
        {
            stopper.detach();
        }
        return completed;
    }

    struct ProbeFrame
    {
        int opcode = 0;
        bool fin = true;
        std::string payload;
    };

    struct WsProbe
    {
        int fd = -1;
        SSL_CTX *ctx = nullptr;
        SSL *ssl = nullptr;

        ~WsProbe()
        {
            if (ssl != nullptr)
            {
                SSL_shutdown(ssl);
                SSL_free(ssl);
            }
            if (ctx != nullptr)
            {
                SSL_CTX_free(ctx);
            }
            if (fd >= 0)
            {
                ::close(fd);
            }
        }

        bool connect(std::uint16_t port, std::string const &fingerprint)
        {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *listing = nullptr;
            if (getaddrinfo("127.0.0.1", std::to_string(port).c_str(), &hints, &listing) != 0)
            {
                return false;
            }
            for (addrinfo const *entry = listing; entry != nullptr && fd < 0; entry = entry->ai_next)
            {
                int candidate = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
                if (candidate < 0)
                {
                    continue;
                }
                if (::connect(candidate, entry->ai_addr, entry->ai_addrlen) == 0)
                {
                    fd = candidate;
                }
                else
                {
                    ::close(candidate);
                }
            }
            freeaddrinfo(listing);
            if (fd < 0)
            {
                return false;
            }
            timeval timeout{};
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
            ctx = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            ssl = SSL_new(ctx);
            if (ssl == nullptr || SSL_set_fd(ssl, fd) != 1 || SSL_connect(ssl) != 1)
            {
                return false;
            }
            X509 *certificate = SSL_get1_peer_certificate(ssl);
            if (certificate == nullptr)
            {
                return false;
            }
            unsigned char *der = nullptr;
            int const length = i2d_X509(certificate, &der);
            X509_free(certificate);
            if (length <= 0 || der == nullptr)
            {
                return false;
            }
            std::vector<std::uint8_t> digest(SHA256_DIGEST_LENGTH);
            SHA256(der, static_cast<std::size_t>(length), digest.data());
            OPENSSL_free(der);
            return tournament_bytes::encode_hex(digest) == fingerprint;
        }

        bool send(std::string const &bytes)
        {
            std::size_t sent = 0;
            while (sent < bytes.size())
            {
                int const written = SSL_write(ssl, bytes.data() + sent, static_cast<int>(bytes.size() - sent));
                if (written > 0)
                {
                    sent += static_cast<std::size_t>(written);
                    continue;
                }
                return false;
            }
            return true;
        }

        bool read_exact(char *data, std::size_t size)
        {
            std::size_t received = 0;
            while (received < size)
            {
                int const chunk = SSL_read(ssl, data + received, static_cast<int>(size - received));
                if (chunk > 0)
                {
                    received += static_cast<std::size_t>(chunk);
                    continue;
                }
                return false;
            }
            return true;
        }

        std::optional<std::string> read_http_head()
        {
            std::string head;
            char chunk[512] = {};
            while (head.find("\r\n\r\n") == std::string::npos)
            {
                if (head.size() > 8192)
                {
                    return std::nullopt;
                }
                int const received = SSL_read(ssl, chunk, sizeof chunk);
                if (received <= 0)
                {
                    return std::nullopt;
                }
                head.append(chunk, static_cast<std::size_t>(received));
            }
            return head;
        }

        std::optional<ProbeFrame> read_frame()
        {
            char header[2] = {};
            if (!read_exact(header, sizeof header))
            {
                return std::nullopt;
            }
            ProbeFrame frame;
            frame.fin = (header[0] & 0x80) != 0;
            frame.opcode = header[0] & 0x0F;
            bool const masked = (header[1] & 0x80) != 0;
            std::uint64_t length = static_cast<std::uint8_t>(header[1] & 0x7F);
            if (length == 126)
            {
                char extended[2] = {};
                if (!read_exact(extended, sizeof extended))
                {
                    return std::nullopt;
                }
                length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[0])) << 8)
                    | static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[1]));
            }
            else if (length == 127)
            {
                char extended[8] = {};
                if (!read_exact(extended, sizeof extended))
                {
                    return std::nullopt;
                }
                length = 0;
                for (char byte : extended)
                {
                    length = (length << 8) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte));
                }
            }
            std::uint8_t mask_key[4] = {};
            if (masked && !read_exact(reinterpret_cast<char *>(mask_key), sizeof mask_key))
            {
                return std::nullopt;
            }
            std::string payload(static_cast<std::size_t>(length), '\0');
            if (length > 0 && !read_exact(payload.data(), payload.size()))
            {
                return std::nullopt;
            }
            if (masked)
            {
                for (std::size_t i = 0; i < payload.size(); ++i)
                {
                    payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ mask_key[i % 4]);
                }
            }
            frame.payload = std::move(payload);
            return frame;
        }

        bool connection_dead()
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                if (!read_frame().has_value())
                {
                    return true;
                }
            }
            return false;
        }
    };

    std::string ws_client_frame(int opcode, std::string const &payload, bool mask, bool fin)
    {
        std::string frame;
        frame.push_back(static_cast<char>((fin ? 0x80 : 0) | opcode));
        std::uint8_t const mask_key[4] = {0x11, 0x22, 0x33, 0x44};
        char const mask_flag = mask ? static_cast<char>(0x80) : static_cast<char>(0);
        if (payload.size() < 126)
        {
            frame.push_back(static_cast<char>(mask_flag | static_cast<char>(payload.size())));
        }
        else if (payload.size() <= 0xFFFF)
        {
            frame.push_back(static_cast<char>(mask_flag | 126));
            frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
            frame.push_back(static_cast<char>(payload.size() & 0xFF));
        }
        else
        {
            frame.push_back(static_cast<char>(mask_flag | 127));
            for (int shift = 56; shift >= 0; shift -= 8)
            {
                frame.push_back(static_cast<char>((payload.size() >> shift) & 0xFF));
            }
        }
        if (mask)
        {
            frame.append(reinterpret_cast<char const *>(mask_key), sizeof mask_key);
        }
        frame.append(payload);
        if (mask)
        {
            char *body = frame.data() + (frame.size() - payload.size());
            for (std::size_t i = 0; i < payload.size(); ++i)
            {
                body[i] = static_cast<char>(static_cast<std::uint8_t>(body[i]) ^ mask_key[i % 4]);
            }
        }
        return frame;
    }

    std::string const ws_upgrade_request = "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    bool ws_upgrade(WsProbe &probe, HostHarness &host)
    {
        if (!probe.connect(host.transport->listening_port(), host.fingerprint))
        {
            return false;
        }
        if (!probe.send(ws_upgrade_request))
        {
            return false;
        }
        auto const head = probe.read_http_head();
        return head.has_value() && head->find(" 101") != std::string::npos;
    }

    void test_certificate_helpers()
    {
        std::string const cert_path = (temp_dir() / "helper_host.pem").string();
        std::string const key_path = (temp_dir() / "helper_host_key.pem").string();
        std::string error;
        check(tnet::generate_self_signed_host_cert(cert_path, key_path, error),
              "self-signed certificate generated: " + error);
        check(std::filesystem::exists(cert_path) && std::filesystem::exists(key_path),
              "certificate and key files written");
        check(read_file(cert_path).find("BEGIN CERTIFICATE") != std::string::npos,
              "certificate file holds PEM certificate");
        check(read_file(key_path).find("PRIVATE KEY") != std::string::npos, "key file holds PEM private key");
        check(tnet::generate_self_signed_host_cert(cert_path, key_path, error),
              "certificate regeneration overwrites existing files");
        auto const first = tnet::certificate_fingerprint(cert_path);
        auto const second = tnet::certificate_fingerprint(cert_path);
        check(first.has_value() && second.has_value() && *first == *second, "fingerprint stable across reads");
        bool hex_shape = first.has_value() && first->size() == 64;
        if (hex_shape)
        {
            for (char c : *first)
            {
                hex_shape = hex_shape && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
            }
        }
        check(hex_shape, "fingerprint is 64 lowercase hex characters");
        std::string const garbage_path = (temp_dir() / "garbage.pem").string();
        write_file(garbage_path, "this is not a certificate");
        check(!tnet::certificate_fingerprint(garbage_path).has_value(), "garbage certificate yields no fingerprint");
        check(!tnet::certificate_fingerprint((temp_dir() / "missing.pem").string()).has_value(),
              "missing certificate yields no fingerprint");
    }

    void test_hello_accept_and_devices()
    {
        HostHarness host("accept");
        check(host.start(tnet::NetConfig{}), "host started on ephemeral port");
        check(host.transport->listening_port() != 0, "ephemeral listening port assigned");
        auto client = make_client(host, 1);
        check(client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "hello accepted with pinned fingerprint: " + client.detail);
        check(client.conn->connected(), "client connection alive after accept");
        check(devices_contain(*host.transport, 1), "accepted device appears in transport devices()");
    }

    void test_wrong_fingerprint_fails()
    {
        HostHarness host("wrong_fp");
        check(host.start(tnet::NetConfig{}), "host started for fingerprint mismatch test");
        tnet::ClientConnection client("127.0.0.1", host.transport->listening_port(), std::string(64, 'a'));
        check(!client.connected(), "wrong fingerprint leaves client disconnected");
    }

    void test_protocol_mismatch_rejected()
    {
        HostHarness host("protocol");
        check(host.start(tnet::NetConfig{}), "host started for protocol mismatch test");
        auto client = make_client(host, 1, "test_adapter", 99, 0);
        check(client.status == tnet::ClientConnection::HelloStatus::Rejected,
              "protocol version 99 hello rejected over tls: " + client.detail);
    }

    void test_pin_enforcement()
    {
        HostHarness host("pins");
        tnet::NetConfig config;
        config.expected_adapter_id = "toj_adapter";
        config.expected_schema_hash = 123;
        check(host.start(config), "host started with pinned adapter id and schema hash");
        auto mismatched = make_client(host, 1, "test_adapter", tw::protocol_version, 0);
        check(mismatched.status == tnet::ClientConnection::HelloStatus::Rejected,
              "unpinned adapter and schema rejected: " + mismatched.detail);
        auto matching = make_client(host, 2, "toj_adapter", tw::protocol_version, 123);
        check(matching.status == tnet::ClientConnection::HelloStatus::Accepted,
              "matching pins accepted: " + matching.detail);
    }

    void test_duplicate_device_key_rules()
    {
        HostHarness host("dup");
        check(host.start(tnet::NetConfig{}), "host started for duplicate device test");
        auto first = make_client(host, 1);
        check(first.status == tnet::ClientConnection::HelloStatus::Accepted, "first enrollment accepted");
        check(devices_contain(*host.transport, 1), "device listed after first enrollment");
        first.conn->close();
        auto impostor = make_client(host, 1);
        check(impostor.status == tnet::ClientConnection::HelloStatus::Rejected,
              "different key for enrolled device rejected: " + impostor.detail);
        ClientHarness reconnect;
        reconnect.id = 1;
        reconnect.keys = first.keys;
        reconnect.conn = std::make_unique<tnet::ClientConnection>("127.0.0.1", host.transport->listening_port(),
                                                                  host.fingerprint);
        check(reconnect.conn->connected(), "reconnect tls connection established");
        tw::HelloMessage hello;
        hello.device = 1;
        hello.public_key = reconnect.keys.public_key;
        reconnect.status = reconnect.conn->send_hello(hello, reconnect.detail);
        check(reconnect.status == tnet::ClientConnection::HelloStatus::Accepted,
              "same key reconnect accepted: " + reconnect.detail);
        check(devices_contain(*host.transport, 1), "device listed after reconnect");
    }

    void test_replacement_logs_local_shutdown()
    {
        HostHarness host("stale_log");
        auto lines = std::make_shared<std::vector<std::string>>();
        auto log_mutex = std::make_shared<std::mutex>();
        tnet::NetConfig config;
        config.log = [lines, log_mutex](std::string const &message)
        {
            std::lock_guard<std::mutex> lock(*log_mutex);
            lines->push_back(message);
        };
        check(host.start(config), "host started for replacement logging test");
        auto keys = tw::generate_keypair();
        auto first = make_client(host, 1, "test_adapter", tw::protocol_version, 0, &keys);
        check(first.status == tnet::ClientConnection::HelloStatus::Accepted,
              "first connection enrolled: " + first.detail);
        auto second = make_client(host, 1, "test_adapter", tw::protocol_version, 0, &keys);
        check(second.status == tnet::ClientConnection::HelloStatus::Accepted,
              "second connection with the same key enrolled: " + second.detail);
        bool first_dead = false;
        bool replaced_logged = false;
        bool shutdown_logged = false;
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            if (!first_dead && !first.conn->connected())
            {
                first_dead = true;
            }
            std::lock_guard<std::mutex> lock(*log_mutex);
            for (std::string const &line : *lines)
            {
                replaced_logged = replaced_logged || line.find("replaced its previous connection")
                    != std::string::npos;
                shutdown_logged = shutdown_logged || line.find("local shutdown") != std::string::npos;
            }
            if (first_dead && replaced_logged && shutdown_logged)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(first_dead, "stale connection closed after replacement");
        check(replaced_logged, "replacement is logged");
        check(shutdown_logged, "replaced connection loss is classified as a local shutdown, not a peer close");
        second.conn->close();
    }

    void test_load_devices_file()
    {
        std::string const hex_one = tournament_bytes::encode_hex(tw::generate_keypair().public_key);
        std::string const hex_two = tournament_bytes::encode_hex(tw::generate_keypair().public_key);
        std::string error;
        std::filesystem::path const valid_path = temp_dir() / "devices_valid.txt";
        write_file(valid_path, "1 " + hex_one + "\n\n2 " + hex_two + "\n");
        auto const loaded = tnet::load_devices_file(valid_path.string(), error);
        auto const key_one = tournament_bytes::decode_hex(hex_one);
        auto const key_two = tournament_bytes::decode_hex(hex_two);
        bool const exact = loaded.has_value() && loaded->size() == 2 && key_one.has_value() && key_two.has_value()
            && (*loaded)[0].id == 1 && (*loaded)[0].public_key == *key_one
            && (*loaded)[1].id == 2 && (*loaded)[1].public_key == *key_two;
        check(exact, "valid devices file parses with exact key bytes");
        error.clear();
        bool const missing_ok = !tnet::load_devices_file((temp_dir() / "devices_missing.txt").string(), error)
                                     .has_value()
            && !error.empty();
        check(missing_ok, "missing devices file fails with an error: " + error);
        std::filesystem::path const malformed_path = temp_dir() / "devices_malformed.txt";
        write_file(malformed_path, "1 " + hex_one + "\nnot a device line\n");
        error.clear();
        auto const malformed = tnet::load_devices_file(malformed_path.string(), error);
        check(!malformed.has_value() && error.find("line 2") != std::string::npos,
              "malformed line error names the line number: " + error);
        std::string uppercase_hex = hex_two;
        uppercase_hex[0] = 'A';
        std::filesystem::path const uppercase_path = temp_dir() / "devices_uppercase.txt";
        write_file(uppercase_path, "3 " + uppercase_hex + "\n");
        error.clear();
        auto const uppercase = tnet::load_devices_file(uppercase_path.string(), error);
        check(!uppercase.has_value() && !error.empty(), "uppercase hex rejected: " + error);
        std::filesystem::path const short_path = temp_dir() / "devices_short_key.txt";
        write_file(short_path, "4 " + hex_one.substr(0, 62) + "\n");
        error.clear();
        auto const short_key = tnet::load_devices_file(short_path.string(), error);
        check(!short_key.has_value() && !error.empty(), "wrong key length rejected: " + error);
        std::filesystem::path const duplicate_path = temp_dir() / "devices_duplicate.txt";
        write_file(duplicate_path, "1 " + hex_one + "\n1 " + hex_two + "\n");
        error.clear();
        auto const duplicate = tnet::load_devices_file(duplicate_path.string(), error);
        check(!duplicate.has_value() && !error.empty(), "duplicate device id rejected: " + error);
        std::filesystem::path const empty_path = temp_dir() / "devices_empty.txt";
        write_file(empty_path, "");
        error.clear();
        auto const empty = tnet::load_devices_file(empty_path.string(), error);
        check(empty.has_value() && empty->empty(), "existing empty devices file loads as empty allowlist");
    }

    void test_allowlist_enrollment()
    {
        HostHarness host("allowlist");
        tw::KeyPair const allowed_keys = tw::generate_keypair();
        tw::KeyPair const impostor_keys = tw::generate_keypair();
        std::filesystem::path const devices_path = temp_dir() / "allowlist_devices.txt";
        write_file(devices_path, "1 " + tournament_bytes::encode_hex(allowed_keys.public_key) + "\n");
        tnet::NetConfig config;
        config.devices_file = devices_path.string();
        check(host.start(config), "host started with devices file allowlist");
        ClientHarness member;
        member.id = 1;
        member.keys = allowed_keys;
        member.conn = std::make_unique<tnet::ClientConnection>("127.0.0.1", host.transport->listening_port(),
                                                               host.fingerprint);
        tw::HelloMessage hello;
        hello.device = 1;
        hello.public_key = member.keys.public_key;
        member.status = member.conn->send_hello(hello, member.detail);
        check(member.status == tnet::ClientConnection::HelloStatus::Accepted,
              "allowlisted device enrolls: " + member.detail);
        check(devices_contain(*host.transport, 1), "allowlisted device appears in transport devices()");
        auto outsider = make_client(host, 2);
        check(outsider.status == tnet::ClientConnection::HelloStatus::Rejected
                  && outsider.detail == "device not in allowlist",
              "unlisted device rejected: " + outsider.detail);
        auto impostor = make_client(host, 1, "test_adapter", tw::protocol_version, 0, &impostor_keys);
        check(impostor.status == tnet::ClientConnection::HelloStatus::Rejected
                  && impostor.detail == "device key does not match allowlist",
              "allowlisted id with foreign key rejected: " + impostor.detail);
    }

    void test_engine_fingerprint_pin()
    {
        HostHarness host("engine_pin");
        tnet::NetConfig config;
        config.expected_engine_fingerprint = 4242;
        check(host.start(config), "host started with engine fingerprint pin");
        auto matching = make_client(host, 1, "test_adapter", tw::protocol_version, 0, nullptr, 4242);
        check(matching.status == tnet::ClientConnection::HelloStatus::Accepted,
              "hello with expected engine fingerprint accepted: " + matching.detail);
        auto mismatching = make_client(host, 2, "test_adapter", tw::protocol_version, 0, nullptr, 4243);
        check(mismatching.status == tnet::ClientConnection::HelloStatus::Rejected
                  && mismatching.detail == "engine fingerprint mismatch",
              "hello with wrong engine fingerprint rejected: " + mismatching.detail);
        auto zero = make_client(host, 3);
        check(zero.status == tnet::ClientConnection::HelloStatus::Rejected
                  && zero.detail == "engine fingerprint mismatch",
              "hello with zero engine fingerprint rejected under pin: " + zero.detail);
        HostHarness unpinned("engine_open");
        check(unpinned.start(tnet::NetConfig{}), "host started without engine fingerprint pin");
        auto relaxed_zero = make_client(unpinned, 1);
        check(relaxed_zero.status == tnet::ClientConnection::HelloStatus::Accepted,
              "unpinned host accepts fingerprint zero: " + relaxed_zero.detail);
        auto relaxed_nonzero = make_client(unpinned, 2, "test_adapter", tw::protocol_version, 0, nullptr, 7);
        check(relaxed_nonzero.status == tnet::ClientConnection::HelloStatus::Accepted,
              "unpinned host accepts any fingerprint: " + relaxed_nonzero.detail);
    }

    void test_blacklisted_reconnect_rejected()
    {
        HostHarness host("blacklist");
        check(host.start(tnet::NetConfig{}), "host started for blacklist reconnect test");
        auto first = make_client(host, 1);
        check(first.status == tnet::ClientConnection::HelloStatus::Accepted,
              "device enrolls before blacklisting: " + first.detail);
        check(host.registry->blacklist(1), "device blacklisted in registry");
        auto banned = make_client(host, 1);
        check(banned.status == tnet::ClientConnection::HelloStatus::Rejected
                  && banned.detail == "device is blacklisted",
              "blacklisted device reconnect rejected: " + banned.detail);
        auto const fresh_keys = tw::generate_keypair();
        check(host.registry->enroll(2, fresh_keys.public_key), "second device enrolls with a fresh key");
        check(host.registry->ban_key(first.keys.public_key), "first device key banned in registry");
        auto rotated = make_client(host, 5, "test_adapter", tw::protocol_version, 0,
                                   &first.keys);
        check(rotated.status == tnet::ClientConnection::HelloStatus::Rejected
                  && rotated.detail == "device key is banned",
              "banned key rejected under a different device id: " + rotated.detail);
        auto survivor = make_client(host, 2, "test_adapter", tw::protocol_version, 0,
                                    &fresh_keys);
        check(survivor.status == tnet::ClientConnection::HelloStatus::Accepted,
              "clean key still enrolls while another key is banned: " + survivor.detail);
    }

    void test_connection_cap()
    {
        HostHarness host("cap");
        tnet::NetConfig config;
        config.max_connections = 1;
        check(host.start(config), "host started with connection cap of one");
        auto first = make_client(host, 1);
        check(first.status == tnet::ClientConnection::HelloStatus::Accepted,
              "first client enrolls under cap: " + first.detail);
        tnet::ClientConnection second("127.0.0.1", host.transport->listening_port(), host.fingerprint);
        check(!second.connected(), "second client cannot connect when cap is full");
        std::string capped_detail;
        auto const capped_status = second.send_hello(tw::HelloMessage{}, capped_detail);
        check(capped_status == tnet::ClientConnection::HelloStatus::Failed,
              "capped client hello attempt fails: " + capped_detail);
        HostHarness clamped("cap_clamp");
        tnet::NetConfig clamped_config;
        clamped_config.max_connections = 0;
        check(clamped.start(clamped_config), "host started with nonpositive cap clamped to default");
        auto one = make_client(clamped, 1);
        auto two = make_client(clamped, 2);
        check(one.status == tnet::ClientConnection::HelloStatus::Accepted
                  && two.status == tnet::ClientConnection::HelloStatus::Accepted,
              "nonpositive cap clamped to default admits two clients: " + two.detail);
    }

    void test_concurrency_advertised()
    {
        HostHarness host("concurrency");
        check(host.start(tnet::NetConfig{}), "host started for concurrency advertisement test");
        auto silent = make_client(host, 1);
        check(silent.status == tnet::ClientConnection::HelloStatus::Accepted
                  && host.registry->concurrency(1) == 0,
              "client without advertisement leaves concurrency zero: " + silent.detail);
        auto capable = make_client(host, 2, "test_adapter", tw::protocol_version, 0, nullptr, 0, 3);
        check(capable.status == tnet::ClientConnection::HelloStatus::Accepted
                  && host.registry->concurrency(2) == 3,
              "advertised concurrency reaches the registry: " + capable.detail);
        auto reconnected = make_client(host, 2, "test_adapter", tw::protocol_version, 0,
                                       &capable.keys, 0, 5);
        check(reconnected.status == tnet::ClientConnection::HelloStatus::Accepted
                  && host.registry->concurrency(2) == 5,
              "reconnect updates the advertised concurrency: " + reconnected.detail);
    }

    void test_websocket_accept_token()
    {
        check(tnet::websocket_accept_token("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
              "websocket accept token matches the RFC 6455 handshake example");
        check(tnet::websocket_accept_token(std::string(24, 'A')) != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
              "websocket accept token depends on the request key");
    }

    void test_websocket_handshake_and_frames()
    {
        HostHarness host("ws_probe");
        check(host.start(tnet::NetConfig{}), "host started for websocket probe tests");
        WsProbe probe;
        check(probe.connect(host.transport->listening_port(), host.fingerprint), "probe tls connection established");
        check(probe.send(ws_upgrade_request), "probe sent websocket upgrade request");
        auto const head = probe.read_http_head();
        check(head.has_value() && head->find("HTTP/1.1 101") != std::string::npos,
              "upgrade response is a 101 switching protocols reply");
        check(head.has_value()
                  && head->find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos,
              "upgrade response carries the RFC 6455 accept token for the sample key");
        check(probe.send(ws_client_frame(9, "ping payload", true, true)), "probe sent masked ping frame");
        auto const pong = probe.read_frame();
        check(pong.has_value() && pong->opcode == 10 && pong->payload == "ping payload",
              "server answered masked ping with echoing pong");
        tw::HelloMessage hello;
        hello.device = 1;
        hello.public_key = tw::generate_keypair().public_key;
        std::string const framed = tw::frame_message(tw::MessageKind::Hello, tw::encode_hello(hello));
        std::string const first = framed.substr(0, 7);
        std::string const second = framed.substr(7);
        check(probe.send(ws_client_frame(2, first, true, false)), "probe sent masked binary first fragment");
        check(probe.send(ws_client_frame(9, "mid", true, true)), "probe sent masked ping between fragments");
        auto const mid_pong = probe.read_frame();
        check(mid_pong.has_value() && mid_pong->opcode == 10 && mid_pong->payload == "mid",
              "server answered ping interleaved mid-message");
        check(probe.send(ws_client_frame(0, second, true, true)), "probe sent masked continuation fragment");
        auto const accept_frame = probe.read_frame();
        check(accept_frame.has_value() && accept_frame->opcode == 2 && accept_frame->payload.size() == 5
                  && accept_frame->payload[4] == static_cast<char>(tw::MessageKind::Accept),
              "fragmented masked hello delivered as one message and accepted");
        check(devices_contain(*host.transport, 1), "hand-rolled websocket client enrolled on the host");
    }

    void test_websocket_unmasked_frame_rejected()
    {
        HostHarness host("ws_unmasked");
        check(host.start(tnet::NetConfig{}), "host started for unmasked frame test");
        WsProbe probe;
        check(ws_upgrade(probe, host), "probe completed websocket upgrade for unmasked test");
        tw::HelloMessage hello;
        hello.device = 1;
        hello.public_key = tw::generate_keypair().public_key;
        std::string const framed = tw::frame_message(tw::MessageKind::Hello, tw::encode_hello(hello));
        check(probe.send(ws_client_frame(2, framed, false, true)), "probe sent unmasked binary frame");
        check(probe.connection_dead(), "unmasked client frame closed the connection");
        check(host.transport->devices().empty(), "unmasked frame client never enrolled");
    }

    void test_websocket_oversized_frame_rejected()
    {
        HostHarness host("ws_oversized");
        check(host.start(tnet::NetConfig{}), "host started for oversized frame test");
        WsProbe probe;
        check(ws_upgrade(probe, host), "probe completed websocket upgrade for oversized test");
        std::string oversized;
        oversized.push_back(static_cast<char>(0x82));
        oversized.push_back(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            oversized.push_back(static_cast<char>((0x200000ULL >> shift) & 0xFF));
        }
        oversized.append("\x11\x22\x33\x44", 4);
        check(probe.send(oversized), "probe sent oversized frame header");
        check(probe.connection_dead(), "oversized frame closed the connection");
        check(host.transport->devices().empty(), "oversized frame client never enrolled");
    }

    void test_websocket_client_round_trip()
    {
        HostHarness host("ws_roundtrip");
        check(host.start(tnet::NetConfig{}), "host started for websocket round trip test");
        auto raw_client = make_client(host, 1);
        check(raw_client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "raw client completes hello on the shared listener: " + raw_client.detail);
        auto ws_client = make_ws_client(host, 2);
        check(ws_client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "websocket client completes hello on the same listener: " + ws_client.detail);
        bool both_listed = false;
        for (int attempt = 0; attempt < 200 && !both_listed; ++attempt)
        {
            std::vector<tw::DeviceId> const devices = host.transport->devices();
            both_listed = devices.size() == 2 && devices[0] == 1 && devices[1] == 2;
            if (!both_listed)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        check(both_listed, "listener serves raw and websocket flavors as separate devices");
        tw::AssignmentBatch const assignment = make_assignment(77, 2, 5, 2);
        std::thread worker([&]() { honest_worker(ws_client); });
        auto const delivery = host.transport->request(2, assignment, 5000);
        check(delivery.status == tt::DeliveryStatus::Delivered, "assignment delivered to websocket client");
        bool const echo_ok = delivery.status == tt::DeliveryStatus::Delivered
            && delivery.result.batch.nonce == 77
            && delivery.result.batch.device == 2
            && delivery.result.batch.outcomes.size() == 2
            && tw::verify_result(ws_client.keys.public_key, delivery.result.batch, delivery.result.signature);
        check(echo_ok, "websocket client returned matching signed result for masked frames");
        ws_client.conn->close();
        raw_client.conn->close();
        worker.join();
    }

    void test_remote_backend_flow_websocket()
    {
        HostHarness host("backend_ws");
        check(host.start(tnet::NetConfig{}), "host started for websocket backend flow");
        auto client1 = make_ws_client(host, 1);
        auto client2 = make_ws_client(host, 2);
        check(client1.status == tnet::ClientConnection::HelloStatus::Accepted
                  && client2.status == tnet::ClientConnection::HelloStatus::Accepted,
              "both devices enrolled over websocket: " + client2.detail);
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 600000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        trem::RemoteBackend backend(test_schema(), host.transport, host.registry, host.provenance,
                                    std::make_shared<tt::SystemClock>(), remote_config);
        auto const games = make_games(4, 6, 777);
        std::thread worker1([&]() { honest_worker(client1); });
        std::thread worker2([&]() { honest_worker(client2); });
        auto const results = backend.run_games(games, fast_config());
        client1.conn->close();
        client2.conn->close();
        worker1.join();
        worker2.join();
        check(results.size() == 6, "six outcomes returned over websocket");
        check(outcomes_match_fabrication(results, games),
              "fabricated outcomes returned in request order over websocket");
        check(host.provenance->size() == 6, "provenance holds six records over websocket");
        check(host.provenance->games_of_device(1).size() + host.provenance->games_of_device(2).size() == 6,
              "all provenance records come from websocket devices");
    }

    void test_dual_stack_listener()
    {
        HostHarness host("dual_stack");
        tnet::NetConfig config;
        config.listen_address = "::";
        if (!host.start(config))
        {
            check(false, "host started on ipv6 wildcard listener");
            return;
        }
        check(host.transport->listening_port() != 0, "ipv6 wildcard listener assigned a port");
        auto raw_client = make_client(host, 1);
        check(raw_client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "ipv4 raw client completes hello on ipv6 wildcard listener: " + raw_client.detail);
        auto ws_client = make_ws_client(host, 2);
        check(ws_client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "ipv4 websocket client completes hello on ipv6 wildcard listener: " + ws_client.detail);
    }

    void test_remote_backend_flow()
    {
        HostHarness host("backend");
        check(host.start(tnet::NetConfig{}), "host started for backend flow");
        auto client1 = make_client(host, 1);
        auto client2 = make_client(host, 2);
        check(client1.status == tnet::ClientConnection::HelloStatus::Accepted
                  && client2.status == tnet::ClientConnection::HelloStatus::Accepted,
              "both devices enrolled over tls");
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 600000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        trem::RemoteBackend backend(test_schema(), host.transport, host.registry, host.provenance,
                                    std::make_shared<tt::SystemClock>(), remote_config);
        auto const games = make_games(1, 6, 777);
        std::thread worker1([&]() { honest_worker(client1); });
        std::thread worker2([&]() { honest_worker(client2); });
        auto const results = backend.run_games(games, fast_config());
        client1.conn->close();
        client2.conn->close();
        worker1.join();
        worker2.join();
        check(results.size() == 6, "six outcomes returned");
        check(outcomes_match_fabrication(results, games), "fabricated outcomes returned in request order");
        check(host.provenance->size() == 6, "provenance holds six records");
        check(host.provenance->games_of_device(1).size() + host.provenance->games_of_device(2).size() == 6,
              "all provenance records come from enrolled devices");
    }

    void test_dropper_reassignment()
    {
        HostHarness host("dropper");
        check(host.start(tnet::NetConfig{}), "host started for dropper test");
        auto dropper = make_client(host, 1);
        auto honest = make_client(host, 2);
        check(dropper.status == tnet::ClientConnection::HelloStatus::Accepted
                  && honest.status == tnet::ClientConnection::HelloStatus::Accepted,
              "dropper and honest device enrolled");
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 2;
        remote_config.lease_ms = 600000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        trem::RemoteBackend backend(test_schema(), host.transport, host.registry, host.provenance,
                                    std::make_shared<tt::SystemClock>(), remote_config);
        auto const games = make_games(2, 4, 888);
        std::thread dropper_worker([&]()
        {
            std::string detail;
            auto assignment = dropper.conn->next_assignment(5000, detail);
            if (assignment)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                dropper.conn->close();
            }
        });
        std::thread honest_loop([&]() { honest_worker(honest); });
        auto const results = backend.run_games(games, fast_config());
        dropper_worker.join();
        honest.conn->close();
        honest_loop.join();
        check(results.size() == 4, "four outcomes returned after reassignment");
        check(outcomes_match_fabrication(results, games), "reassigned outcomes correct");
        check(host.provenance->size() == 4, "provenance holds four records");
        check(host.provenance->games_of_device(1).empty(), "dropper has nothing accepted");
    }

    void test_late_reply_discarded()
    {
        HostHarness host("late");
        check(host.start(tnet::NetConfig{}), "host started for late reply test");
        auto client = make_client(host, 1);
        check(client.status == tnet::ClientConnection::HelloStatus::Accepted, "device enrolled for late reply test");
        std::thread worker([&]()
        {
            std::string detail;
            auto first = client.conn->next_assignment(5000, detail);
            if (first)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(450));
                reply_with_fabricated(client, *first, detail);
            }
            for (;;)
            {
                auto assignment = client.conn->next_assignment(5000, detail);
                if (!assignment)
                {
                    return;
                }
                reply_with_fabricated(client, *assignment, detail);
            }
        });
        tw::AssignmentBatch const assignment = make_assignment(101, 1, 7, 1);
        auto const delivery = host.transport->request(1, assignment, 200);
        check(delivery.status == tt::DeliveryStatus::Timeout, "late device request times out");
        check(!delivery.peer_active, "silent late device reports no peer activity");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        check(client.conn->connected(), "connection survives late reply discard");
        tw::AssignmentBatch replay = assignment;
        replay.nonce = 202;
        auto const second = host.transport->request(1, replay, 2000);
        check(second.status == tt::DeliveryStatus::Delivered, "subsequent request on same connection delivered");
        bool const echo_ok = second.status == tt::DeliveryStatus::Delivered
            && second.result.batch.nonce == 202
            && second.result.batch.device == 1
            && second.result.batch.outcomes.size() == 1
            && second.result.batch.outcomes[0].id == assignment.games[0].id
            && tw::verify_result(client.keys.public_key, second.result.batch, second.result.signature);
        check(echo_ok, "survivor reply echoes nonce, device and valid signature");
        client.conn->close();
        worker.join();
    }

    void test_request_wait_covers_slow_client()
    {
        HostHarness host("slow_wait");
        check(host.start(tnet::NetConfig{}), "host started for slow client wait test");
        auto client = make_client(host, 1);
        check(client.status == tnet::ClientConnection::HelloStatus::Accepted, "device enrolled for slow wait test");
        tw::AssignmentBatch const assignment = make_assignment(301, 1, 9, 2);
        std::thread worker([&]()
        {
            std::string detail;
            auto received = client.conn->next_assignment(5000, detail);
            if (received)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                reply_with_fabricated(client, *received, detail);
            }
        });
        auto const delivery = host.transport->request(1, assignment, 2000);
        check(delivery.status == tt::DeliveryStatus::Delivered,
              "generous wait delivers a slow but live result");
        check(delivery.status == tt::DeliveryStatus::Delivered && delivery.result.batch.nonce == 301,
              "slow client result carries the requested nonce");
        client.conn->close();
        worker.join();
    }

    void test_request_timeout_reports_peer_activity()
    {
        HostHarness host("peer_active");
        check(host.start(tnet::NetConfig{}), "host started for peer activity test");
        auto client = make_client(host, 1);
        check(client.status == tnet::ClientConnection::HelloStatus::Accepted,
              "device enrolled for peer activity test");
        tw::AssignmentBatch const assignment = make_assignment(401, 1, 11, 1);
        std::thread worker([&]()
        {
            std::string detail;
            auto received = client.conn->next_assignment(5000, detail);
            if (!received)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tw::AssignmentBatch stale = *received;
            stale.nonce = 402;
            reply_with_fabricated(client, stale, detail);
            for (;;)
            {
                auto next = client.conn->next_assignment(5000, detail);
                if (!next)
                {
                    return;
                }
                reply_with_fabricated(client, *next, detail);
            }
        });
        auto const delivery = host.transport->request(1, assignment, 600);
        check(delivery.status == tt::DeliveryStatus::Timeout,
              "flight answered only with a wrong nonce still times out");
        check(delivery.peer_active, "arriving stale traffic marks the peer active");
        tw::AssignmentBatch replay = assignment;
        replay.nonce = 403;
        auto const second = host.transport->request(1, replay, 2000);
        check(second.status == tt::DeliveryStatus::Delivered, "connection still delivers after stale traffic");
        client.conn->close();
        worker.join();
    }

    void test_concurrent_clients()
    {
        HostHarness host("concurrent");
        check(host.start(tnet::NetConfig{}), "host started for concurrency test");
        std::vector<ClientHarness> clients;
        bool all_accepted = true;
        for (tw::DeviceId id = 1; id <= 3; ++id)
        {
            clients.push_back(make_client(host, id));
            all_accepted = all_accepted
                && clients.back().status == tnet::ClientConnection::HelloStatus::Accepted;
        }
        check(all_accepted, "three devices enrolled over tls");
        trem::RemoteConfig remote_config;
        remote_config.games_per_assignment = 1;
        remote_config.lease_ms = 600000;
        remote_config.max_assignment_rounds = 4;
        remote_config.per_series_device_cap = 8;
        trem::RemoteBackend backend(test_schema(), host.transport, host.registry, host.provenance,
                                    std::make_shared<tt::SystemClock>(), remote_config);
        auto const games = make_games(3, 9, 999);
        std::vector<std::thread> workers;
        for (auto &client : clients)
        {
            workers.emplace_back([&client]() { honest_worker(client); });
        }
        auto const results = backend.run_games(games, fast_config());
        for (auto &client : clients)
        {
            client.conn->close();
        }
        for (auto &worker : workers)
        {
            worker.join();
        }
        check(results.size() == 9, "nine outcomes returned under concurrency");
        check(outcomes_match_fabrication(results, games), "concurrent outcomes correct per device");
        check(host.provenance->size() == 9, "provenance holds nine records");
    }

    void test_stop_wakes_parked_accept()
    {
        HostHarness host("stop_idle");
        check(host.start(tnet::NetConfig{}), "host started with no clients for stop test");
        check(host.transport->devices().empty(), "no devices connected before stop");
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        check(stop_completes(host.transport, 5000),
              "stop returns within budget with accept thread parked in accept");
        check(stop_completes(host.transport, 1000), "second stop call is idempotent");
    }

    void test_stop_wakes_idle_client()
    {
        HostHarness host("stop_client");
        check(host.start(tnet::NetConfig{}), "host started for idle client stop test");
        auto keys = tw::generate_keypair();
        auto conn = std::make_shared<tnet::ClientConnection>("127.0.0.1", host.transport->listening_port(),
                                                             host.fingerprint);
        check(conn->connected(), "client tls connection established for stop test");
        tw::HelloMessage hello;
        hello.device = 1;
        hello.public_key = keys.public_key;
        std::string hello_detail;
        auto const status = conn->send_hello(hello, hello_detail);
        check(status == tnet::ClientConnection::HelloStatus::Accepted, "client enrolled before stop: " + hello_detail);
        auto returned = std::make_shared<std::atomic<bool>>(false);
        auto detail_box = std::make_shared<std::string>();
        std::thread worker([conn, returned, detail_box]()
        {
            std::string detail;
            conn->next_assignment(30000, detail);
            *detail_box = detail;
            returned->store(true);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(2100));
        check(stop_completes(host.transport, 5000),
              "stop returns within budget with client parked in next_assignment");
        bool observed = false;
        for (int attempt = 0; attempt < 1000 && !observed; ++attempt)
        {
            observed = returned->load();
            if (!observed)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        check(observed, "idle client observed closure within budget after host stop");
        if (observed)
        {
            worker.join();
        }
        else
        {
            worker.detach();
        }
        check(!conn->connected(), "client reports disconnected after host stop");
        check(detail_box->rfind("connection closed", 0) == 0,
              "client detail reports connection closed: " + *detail_box);
    }
}

int main()
{
    test_certificate_helpers();
    test_hello_accept_and_devices();
    test_wrong_fingerprint_fails();
    test_protocol_mismatch_rejected();
    test_pin_enforcement();
    test_duplicate_device_key_rules();
    test_replacement_logs_local_shutdown();
    test_load_devices_file();
    test_allowlist_enrollment();
    test_blacklisted_reconnect_rejected();
    test_engine_fingerprint_pin();
    test_connection_cap();
    test_concurrency_advertised();
    test_websocket_accept_token();
    test_websocket_handshake_and_frames();
    test_websocket_unmasked_frame_rejected();
    test_websocket_oversized_frame_rejected();
    test_websocket_client_round_trip();
    test_remote_backend_flow_websocket();
    test_dual_stack_listener();
    test_remote_backend_flow();
    test_dropper_reassignment();
    test_late_reply_discarded();
    test_request_wait_covers_slow_client();
    test_request_timeout_reports_peer_activity();
    test_concurrent_clients();
    test_stop_wakes_parked_accept();
    test_stop_wakes_idle_client();
    std::error_code cleanup;
    std::filesystem::remove_all(temp_dir(), cleanup);
    std::println("net transport integration: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
