#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tournament/registry.h"
#include "tournament/transport.h"
#include "tournament/wire.h"

namespace tournament_net
{
    using tournament_transport::Delivery;
    using tournament_wire::AssignmentBatch;
    using tournament_wire::DeviceId;
    using tournament_wire::HelloMessage;
    using tournament_wire::PublicKey;
    using tournament_wire::SignedResult;

    struct NetConfig
    {
        std::string listen_address = "127.0.0.1";
        std::uint16_t port = 0;
        std::string certificate_path;
        std::string private_key_path;
        std::string expected_adapter_id;
        std::uint64_t expected_schema_hash = 0;
        std::uint64_t expected_engine_fingerprint = 0;
        std::string devices_file;
        int max_connections = 64;
        std::function<void(std::string const &)> log;
    };

    struct AllowedDevice
    {
        DeviceId id = 0;
        PublicKey public_key;
    };

    std::optional<std::vector<AllowedDevice>> load_devices_file(std::string const &path, std::string &error);

    bool generate_self_signed_host_cert(std::string const &certificate_path,
                                        std::string const &private_key_path,
                                        std::string &error);

    std::optional<std::string> certificate_fingerprint(std::string const &certificate_path);

    std::string websocket_accept_token(std::string const &sec_websocket_key);

    class HostTransport : public tournament_transport::Transport
    {
    public:
        HostTransport(std::shared_ptr<tournament_registry::DeviceRegistry> registry, NetConfig config);
        ~HostTransport() override;
        HostTransport(HostTransport const &) = delete;
        HostTransport &operator=(HostTransport const &) = delete;

        bool start(std::string &error);
        void stop();
        std::uint16_t listening_port() const;

        std::vector<DeviceId> devices() const override;
        Delivery request(DeviceId device, AssignmentBatch const &assignment,
                         std::uint64_t wait_ms) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class ClientConnectionBase
    {
    public:
        enum class HelloStatus
        {
            Accepted,
            Rejected,
            Failed,
        };

        virtual ~ClientConnectionBase() = default;

        virtual bool connected() const = 0;
        virtual HelloStatus send_hello(HelloMessage const &hello, std::string &detail) = 0;
        virtual std::optional<AssignmentBatch> next_assignment(std::uint64_t timeout_ms, std::string &detail) = 0;
        virtual bool send_result(SignedResult const &result, std::string &detail) = 0;
        virtual void close() = 0;
    };

    class ClientConnection : public ClientConnectionBase
    {
    public:
        ClientConnection(std::string host, std::uint16_t port, std::string expected_fingerprint);
        ~ClientConnection() override;
        ClientConnection(ClientConnection const &) = delete;
        ClientConnection &operator=(ClientConnection const &) = delete;

        bool connected() const override;
        HelloStatus send_hello(HelloMessage const &hello, std::string &detail) override;
        std::optional<AssignmentBatch> next_assignment(std::uint64_t timeout_ms, std::string &detail) override;
        bool send_result(SignedResult const &result, std::string &detail) override;
        void close() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class WsClientConnection : public ClientConnectionBase
    {
    public:
        WsClientConnection(std::string host, std::uint16_t port, std::string expected_fingerprint,
                           std::string path = "/", bool ca_verified = false,
                           std::uint64_t heartbeat_ms = 30000);
        ~WsClientConnection() override;
        WsClientConnection(WsClientConnection const &) = delete;
        WsClientConnection &operator=(WsClientConnection const &) = delete;

        bool connected() const override;
        HelloStatus send_hello(HelloMessage const &hello, std::string &detail) override;
        std::optional<AssignmentBatch> next_assignment(std::uint64_t timeout_ms, std::string &detail) override;
        bool send_result(SignedResult const &result, std::string &detail) override;
        void close() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
