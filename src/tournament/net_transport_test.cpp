#include "tournament/net_transport.h"

#include "tournament/provenance.h"
#include "tournament/registry.h"
#include "tournament/remote_backend.h"
#include "tournament/runner.h"
#include "tournament/transport.h"
#include "tournament/wire.h"
#include "tuning/domain.h"
#include "tuning/match.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
        std::unique_ptr<tnet::ClientConnection> conn;
        tnet::ClientConnection::HelloStatus status = tnet::ClientConnection::HelloStatus::Failed;
        std::string detail;
    };

    ClientHarness make_client(HostHarness &host, tw::DeviceId id, std::string const &adapter = "test_adapter",
                              std::uint32_t protocol = tw::protocol_version, std::uint64_t schema_hash = 0,
                              tw::KeyPair const *keys = nullptr)
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
        tw::HelloMessage hello;
        hello.device = id;
        hello.public_key = client.keys.public_key;
        hello.protocol = protocol;
        hello.adapter_id = adapter;
        hello.schema_hash = schema_hash;
        client.status = client.conn->send_hello(hello, client.detail);
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
        tnet::NetConfig config;
        config.io_timeout_ms = 500;
        check(host.start(config), "host started for dropper test");
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
        tnet::NetConfig config;
        config.io_timeout_ms = 200;
        check(host.start(config), "host started for late reply test");
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
        auto const delivery = host.transport->request(1, assignment);
        check(delivery.status == tt::DeliveryStatus::Timeout, "late device request times out");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        check(client.conn->connected(), "connection survives late reply discard");
        tw::AssignmentBatch replay = assignment;
        replay.nonce = 202;
        auto const second = host.transport->request(1, replay);
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
        check(*detail_box == "connection closed", "client detail reports connection closed: " + *detail_box);
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
    test_remote_backend_flow();
    test_dropper_reassignment();
    test_late_reply_discarded();
    test_concurrent_clients();
    test_stop_wakes_parked_accept();
    test_stop_wakes_idle_client();
    std::error_code cleanup;
    std::filesystem::remove_all(temp_dir(), cleanup);
    std::println("net transport integration: {} checks, {} failures", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
