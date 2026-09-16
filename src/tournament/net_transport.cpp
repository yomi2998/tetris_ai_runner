#include "tournament/net_transport.h"

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "tournament/bytes.h"

namespace tournament_net
{
    namespace
    {
        using tournament_transport::DeliveryStatus;
        using tournament_wire::DeviceId;
        using tournament_wire::FramedMessage;
        using tournament_wire::HelloMessage;
        using tournament_wire::MessageKind;
        using tournament_wire::Nonce;
        using tournament_wire::SignedResult;

        using SteadyClock = std::chrono::steady_clock;
        using TimePoint = SteadyClock::time_point;

        constexpr std::uint64_t kHandshakeTimeoutMs = 5000;
        constexpr std::uint64_t kWriteTimeoutMs = 5000;
        constexpr std::uint64_t kHelloReplyTimeoutMs = 10000;
        constexpr int kListenBacklog = 64;

        void ignore_sigpipe_once()
        {
            static std::once_flag flag;
            std::call_once(flag, []()
            {
                signal(SIGPIPE, SIG_IGN);
            });
        }

        std::span<std::uint8_t const> as_bytes(std::string const &text)
        {
            return {reinterpret_cast<std::uint8_t const *>(text.data()), text.size()};
        }

        TimePoint deadline_after(std::uint64_t milliseconds)
        {
            return SteadyClock::now() + std::chrono::milliseconds(milliseconds);
        }

        std::string normalize_fingerprint(std::string const &fingerprint)
        {
            std::string lowered;
            lowered.reserve(fingerprint.size());
            for (char c : fingerprint)
            {
                if (c == ':')
                {
                    continue;
                }
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                lowered.push_back(c);
            }
            return lowered;
        }

        bool ssl_write_all(SSL *ssl, char const *data, std::size_t size, TimePoint deadline)
        {
            std::size_t sent = 0;
            while (sent < size)
            {
                if (SteadyClock::now() > deadline)
                {
                    return false;
                }
                int const written = SSL_write(ssl, data + sent, static_cast<int>(size - sent));
                if (written > 0)
                {
                    sent += static_cast<std::size_t>(written);
                    continue;
                }
                int const code = SSL_get_error(ssl, written);
                if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
                {
                    continue;
                }
                return false;
            }
            return true;
        }

        bool ssl_read_all(SSL *ssl, char *data, std::size_t size, TimePoint deadline)
        {
            std::size_t received = 0;
            while (received < size)
            {
                if (SteadyClock::now() > deadline)
                {
                    return false;
                }
                int const chunk = SSL_read(ssl, data + received, static_cast<int>(size - received));
                if (chunk > 0)
                {
                    received += static_cast<std::size_t>(chunk);
                    continue;
                }
                int const code = SSL_get_error(ssl, chunk);
                if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
                {
                    continue;
                }
                return false;
            }
            return true;
        }

        enum class FrameRead
        {
            Frame,
            Closed,
        };

        FrameRead read_frame(SSL *ssl, FramedMessage &out, TimePoint deadline)
        {
            char header[5] = {};
            if (!ssl_read_all(ssl, header, sizeof header, deadline))
            {
                return FrameRead::Closed;
            }
            unsigned char const *raw = reinterpret_cast<unsigned char const *>(header);
            std::uint32_t const length = static_cast<std::uint32_t>(raw[0])
                | (static_cast<std::uint32_t>(raw[1]) << 8)
                | (static_cast<std::uint32_t>(raw[2]) << 16)
                | (static_cast<std::uint32_t>(raw[3]) << 24);
            std::uint8_t const kind = raw[4];
            if (length > tournament_wire::max_frame_payload || kind < 1 || kind > 5)
            {
                return FrameRead::Closed;
            }
            std::string payload(static_cast<std::size_t>(length), '\0');
            if (length > 0 && !ssl_read_all(ssl, payload.data(), payload.size(), deadline))
            {
                return FrameRead::Closed;
            }
            out.kind = static_cast<MessageKind>(kind);
            out.payload = std::move(payload);
            return FrameRead::Frame;
        }

        bool ssl_write_frame(SSL *ssl, MessageKind kind, std::string const &payload, TimePoint deadline)
        {
            std::string const frame = tournament_wire::frame_message(kind, payload);
            return ssl_write_all(ssl, frame.data(), frame.size(), deadline);
        }

        void set_socket_timeouts(int fd)
        {
            timeval timeout{};
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        }

        bool ssl_handshake(SSL *ssl, bool server, TimePoint deadline)
        {
            for (;;)
            {
                if (SteadyClock::now() > deadline)
                {
                    return false;
                }
                int const status = server ? SSL_accept(ssl) : SSL_connect(ssl);
                if (status == 1)
                {
                    return true;
                }
                int const code = SSL_get_error(ssl, status);
                if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
                {
                    continue;
                }
                return false;
            }
        }

        struct PendingRequest
        {
            bool done = false;
            DeliveryStatus status = DeliveryStatus::Malformed;
            SignedResult result{};
        };

        struct HostShared;

        struct HostConnection
        {
            DeviceId device = 0;
            int fd = -1;
            SSL *ssl = nullptr;
            std::mutex write_mutex;
            std::mutex pending_mutex;
            std::condition_variable pending_cv;
            std::map<Nonce, PendingRequest> pending;
            std::atomic<bool> closed{false};

            bool write_frame(MessageKind kind, std::string const &payload, std::string &error)
            {
                std::lock_guard<std::mutex> lock(write_mutex);
                if (closed.load())
                {
                    error = "connection closed";
                    return false;
                }
                if (ssl_write_frame(ssl, kind, payload, deadline_after(kWriteTimeoutMs)))
                {
                    return true;
                }
                error = "frame write failed";
                return false;
            }

            void fail_all(DeliveryStatus status)
            {
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    for (auto &entry : pending)
                    {
                        if (!entry.second.done)
                        {
                            entry.second.done = true;
                            entry.second.status = status;
                            entry.second.result = SignedResult{};
                        }
                    }
                }
                pending_cv.notify_all();
            }

            void kill()
            {
                if (!closed.exchange(true) && fd >= 0)
                {
                    shutdown(fd, SHUT_RDWR);
                }
            }

            void teardown()
            {
                closed.store(true);
                std::lock_guard<std::mutex> lock(write_mutex);
                if (ssl != nullptr)
                {
                    SSL_shutdown(ssl);
                    SSL_free(ssl);
                    ssl = nullptr;
                }
                if (fd >= 0)
                {
                    close(fd);
                    fd = -1;
                }
            }
        };

        struct HostShared
        {
            ~HostShared()
            {
                if (ctx != nullptr)
                {
                    SSL_CTX_free(ctx);
                }
            }

            SSL_CTX *ctx = nullptr;
            std::shared_ptr<tournament_registry::DeviceRegistry> registry;
            NetConfig config;
            std::atomic<bool> shutting_down{false};
            std::mutex mutex;
            std::map<DeviceId, std::shared_ptr<HostConnection>> connections;
            std::vector<std::thread> readers;
        };

        bool validate_hello(HostShared &shared, HostConnection &conn, std::string &reason)
        {
            FramedMessage message;
            if (read_frame(conn.ssl, message, deadline_after(kHandshakeTimeoutMs)) != FrameRead::Frame)
            {
                reason = "no hello frame received";
                return false;
            }
            if (message.kind != MessageKind::Hello)
            {
                reason = "first frame was not a hello";
                return false;
            }
            std::optional<HelloMessage> hello = tournament_wire::decode_hello(as_bytes(message.payload));
            if (!hello)
            {
                reason = "hello payload failed to decode";
                return false;
            }
            if (hello->protocol != tournament_wire::protocol_version)
            {
                reason = "protocol version mismatch";
                return false;
            }
            if (!shared.config.expected_adapter_id.empty() && hello->adapter_id != shared.config.expected_adapter_id)
            {
                reason = "adapter id mismatch";
                return false;
            }
            if (shared.config.expected_schema_hash != 0 && hello->schema_hash != shared.config.expected_schema_hash)
            {
                reason = "schema hash mismatch";
                return false;
            }
            if (!shared.registry)
            {
                reason = "registry unavailable";
                return false;
            }
            conn.device = hello->device;
            tournament_wire::PublicKey const *stored = shared.registry->public_key(hello->device);
            if (stored != nullptr)
            {
                if (*stored != hello->public_key)
                {
                    reason = "device key mismatch";
                    return false;
                }
            }
            else if (!shared.registry->enroll(hello->device, hello->public_key))
            {
                reason = "enrollment failed";
                return false;
            }
            return true;
        }

        void connection_reader(std::shared_ptr<HostShared> shared, std::shared_ptr<HostConnection> conn)
        {
            for (;;)
            {
                if (conn->closed.load())
                {
                    break;
                }
                FramedMessage message;
                if (read_frame(conn->ssl, message, TimePoint::max()) != FrameRead::Frame)
                {
                    break;
                }
                if (message.kind != MessageKind::Result)
                {
                    break;
                }
                std::optional<SignedResult> result = tournament_wire::decode_result_message(as_bytes(message.payload));
                if (!result)
                {
                    break;
                }
                Nonce const nonce = result->batch.nonce;
                bool fulfilled = false;
                {
                    std::lock_guard<std::mutex> lock(conn->pending_mutex);
                    auto it = conn->pending.find(nonce);
                    if (it != conn->pending.end() && !it->second.done)
                    {
                        it->second.status = DeliveryStatus::Delivered;
                        it->second.result = std::move(*result);
                        it->second.done = true;
                        fulfilled = true;
                    }
                }
                if (fulfilled)
                {
                    conn->pending_cv.notify_all();
                }
            }
            conn->fail_all(DeliveryStatus::Malformed);
            conn->teardown();
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                auto it = shared->connections.find(conn->device);
                if (it != shared->connections.end() && it->second == conn)
                {
                    shared->connections.erase(it);
                }
            }
        }

        void handle_connection(std::shared_ptr<HostShared> shared, int fd)
        {
            set_socket_timeouts(fd);
            std::shared_ptr<HostConnection> conn = std::make_shared<HostConnection>();
            conn->fd = fd;
            conn->ssl = SSL_new(shared->ctx);
            if (conn->ssl == nullptr || SSL_set_fd(conn->ssl, fd) != 1
                || !ssl_handshake(conn->ssl, true, deadline_after(kHandshakeTimeoutMs)))
            {
                conn->teardown();
                return;
            }
            std::string reason;
            if (!validate_hello(*shared, *conn, reason))
            {
                std::string error;
                conn->write_frame(MessageKind::Reject, tournament_wire::encode_reject(reason), error);
                conn->teardown();
                return;
            }
            bool accept_sent = false;
            {
                std::lock_guard<std::mutex> write_lock(conn->write_mutex);
                if (!conn->closed.load())
                {
                    accept_sent = ssl_write_frame(conn->ssl, MessageKind::Accept, "",
                                                  deadline_after(kWriteTimeoutMs));
                }
            }
            if (!accept_sent)
            {
                conn->teardown();
                return;
            }
            std::shared_ptr<HostConnection> stale;
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->shutting_down.load())
                {
                    conn->teardown();
                    return;
                }
                auto &slot = shared->connections[conn->device];
                stale = slot;
                slot = conn;
                shared->readers.emplace_back(connection_reader, shared, conn);
            }
            if (stale)
            {
                stale->kill();
            }
        }

        void accept_loop(std::shared_ptr<HostShared> shared, int listen_fd)
        {
            while (!shared->shutting_down.load())
            {
                int fd = accept(listen_fd, nullptr, nullptr);
                if (fd < 0)
                {
                    if (shared->shutting_down.load())
                    {
                        break;
                    }
                    if (errno == EINTR || errno == ECONNABORTED)
                    {
                        continue;
                    }
                    break;
                }
                std::thread(handle_connection, shared, fd).detach();
            }
        }
    }

    bool generate_self_signed_host_cert(std::string const &certificate_path,
                                        std::string const &private_key_path,
                                        std::string &error)
    {
        error.clear();
        EVP_PKEY *key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
        if (key == nullptr)
        {
            error = "EC P-256 key generation failed";
            return false;
        }
        X509 *certificate = X509_new();
        if (certificate == nullptr)
        {
            EVP_PKEY_free(key);
            error = "X509 allocation failed";
            return false;
        }
        bool built = X509_set_version(certificate, 2) == 1
            && ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1) == 1
            && X509_gmtime_adj(X509_getm_notBefore(certificate), 0) != nullptr
            && X509_gmtime_adj(X509_getm_notAfter(certificate), 315360000L) != nullptr;
        if (built)
        {
            X509_NAME *name = X509_get_subject_name(certificate);
            built = name != nullptr
                && X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                       reinterpret_cast<unsigned char const *>("tournament-host"), -1, -1, 0) == 1
                && X509_set_issuer_name(certificate, name) == 1
                && X509_set_pubkey(certificate, key) == 1;
        }
        if (built)
        {
            built = X509_sign(certificate, key, EVP_sha256()) > 0;
        }
        if (!built)
        {
            X509_free(certificate);
            EVP_PKEY_free(key);
            error = "self-signed certificate construction failed";
            return false;
        }
        FILE *certificate_file = fopen(certificate_path.c_str(), "wb");
        if (certificate_file == nullptr)
        {
            X509_free(certificate);
            EVP_PKEY_free(key);
            error = "cannot open certificate file: " + certificate_path;
            return false;
        }
        bool const certificate_written = PEM_write_X509(certificate_file, certificate) == 1;
        fclose(certificate_file);
        if (!certificate_written)
        {
            X509_free(certificate);
            EVP_PKEY_free(key);
            error = "certificate PEM write failed: " + certificate_path;
            return false;
        }
        FILE *key_file = fopen(private_key_path.c_str(), "wb");
        if (key_file == nullptr)
        {
            X509_free(certificate);
            EVP_PKEY_free(key);
            error = "cannot open private key file: " + private_key_path;
            return false;
        }
        bool const key_written = PEM_write_PrivateKey(key_file, key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
        fclose(key_file);
        X509_free(certificate);
        EVP_PKEY_free(key);
        if (!key_written)
        {
            error = "private key PEM write failed: " + private_key_path;
            return false;
        }
        return true;
    }

    std::optional<std::string> certificate_fingerprint(std::string const &certificate_path)
    {
        FILE *file = fopen(certificate_path.c_str(), "rb");
        if (file == nullptr)
        {
            return std::nullopt;
        }
        X509 *certificate = PEM_read_X509(file, nullptr, nullptr, nullptr);
        fclose(file);
        if (certificate == nullptr)
        {
            return std::nullopt;
        }
        unsigned char *der = nullptr;
        int const length = i2d_X509(certificate, &der);
        X509_free(certificate);
        if (length <= 0 || der == nullptr)
        {
            if (der != nullptr)
            {
                OPENSSL_free(der);
            }
            return std::nullopt;
        }
        std::vector<std::uint8_t> digest(SHA256_DIGEST_LENGTH);
        SHA256(der, static_cast<std::size_t>(length), digest.data());
        OPENSSL_free(der);
        return tournament_bytes::encode_hex(digest);
    }

    struct HostTransport::Impl
    {
        Impl(std::shared_ptr<tournament_registry::DeviceRegistry> registry_in, NetConfig config_in)
            : shared(std::make_shared<HostShared>())
        {
            shared->registry = std::move(registry_in);
            shared->config = config_in;
        }

        std::shared_ptr<HostShared> shared;
        int listen_fd = -1;
        std::thread accept_thread;
        std::uint16_t port = 0;
        bool started = false;
    };

    HostTransport::HostTransport(std::shared_ptr<tournament_registry::DeviceRegistry> registry, NetConfig config)
        : impl_(std::make_unique<Impl>(std::move(registry), config))
    {
    }

    HostTransport::~HostTransport()
    {
        stop();
    }

    bool HostTransport::start(std::string &error)
    {
        ignore_sigpipe_once();
        std::lock_guard<std::mutex> lock(impl_->shared->mutex);
        if (impl_->shared->shutting_down.load())
        {
            error = "transport was stopped";
            return false;
        }
        if (impl_->started)
        {
            error = "transport already started";
            return false;
        }
        SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
        if (ctx == nullptr)
        {
            error = "TLS server context creation failed";
            return false;
        }
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        if (SSL_CTX_use_certificate_chain_file(ctx, impl_->shared->config.certificate_path.c_str()) != 1
            || SSL_CTX_use_PrivateKey_file(ctx, impl_->shared->config.private_key_path.c_str(),
                                           SSL_FILETYPE_PEM) != 1
            || SSL_CTX_check_private_key(ctx) != 1)
        {
            SSL_CTX_free(ctx);
            error = "certificate or private key rejected from configured paths";
            return false;
        }
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
        {
            SSL_CTX_free(ctx);
            error = "listening socket creation failed";
            return false;
        }
        int const reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(impl_->shared->config.port);
        if (inet_pton(AF_INET, impl_->shared->config.listen_address.c_str(), &address.sin_addr) != 1
            || bind(fd, reinterpret_cast<sockaddr const *>(&address), sizeof address) != 0
            || listen(fd, kListenBacklog) != 0)
        {
            close(fd);
            SSL_CTX_free(ctx);
            error = "bind or listen failed on " + impl_->shared->config.listen_address;
            return false;
        }
        sockaddr_in bound{};
        socklen_t bound_length = sizeof bound;
        std::uint16_t assigned = 0;
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &bound_length) == 0)
        {
            assigned = ntohs(bound.sin_port);
        }
        impl_->shared->ctx = ctx;
        impl_->listen_fd = fd;
        impl_->port = assigned;
        impl_->accept_thread = std::thread(accept_loop, impl_->shared, fd);
        impl_->started = true;
        return true;
    }

    void HostTransport::stop()
    {
        if (!impl_)
        {
            return;
        }
        std::vector<std::thread> to_join;
        {
            std::lock_guard<std::mutex> lock(impl_->shared->mutex);
            impl_->shared->shutting_down.store(true);
            for (auto &entry : impl_->shared->connections)
            {
                entry.second->kill();
            }
            if (impl_->listen_fd >= 0)
            {
                shutdown(impl_->listen_fd, SHUT_RDWR);
            }
            to_join = std::move(impl_->shared->readers);
            impl_->shared->readers.clear();
            impl_->shared->connections.clear();
        }
        for (std::thread &thread : to_join)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
        if (impl_->accept_thread.joinable())
        {
            impl_->accept_thread.join();
        }
        if (impl_->listen_fd >= 0)
        {
            shutdown(impl_->listen_fd, SHUT_RDWR);
            close(impl_->listen_fd);
            impl_->listen_fd = -1;
        }
    }

    std::uint16_t HostTransport::listening_port() const
    {
        return impl_->port;
    }

    std::vector<DeviceId> HostTransport::devices() const
    {
        std::vector<DeviceId> ids;
        {
            std::lock_guard<std::mutex> lock(impl_->shared->mutex);
            ids.reserve(impl_->shared->connections.size());
            for (auto const &entry : impl_->shared->connections)
            {
                if (!entry.second->closed.load())
                {
                    ids.push_back(entry.first);
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    tournament_transport::Delivery HostTransport::request(DeviceId device, AssignmentBatch const &assignment)
    {
        std::shared_ptr<HostConnection> conn;
        {
            std::lock_guard<std::mutex> lock(impl_->shared->mutex);
            auto const it = impl_->shared->connections.find(device);
            if (it == impl_->shared->connections.end())
            {
                return {DeliveryStatus::Unreachable, {}};
            }
            conn = it->second;
        }
        if (conn->closed.load())
        {
            return {DeliveryStatus::Unreachable, {}};
        }
        Nonce const nonce = assignment.nonce;
        {
            std::lock_guard<std::mutex> lock(conn->pending_mutex);
            if (conn->pending.count(nonce) != 0)
            {
                return {DeliveryStatus::Malformed, {}};
            }
            conn->pending.emplace(nonce, PendingRequest{});
        }
        auto erase_pending = [&conn, nonce]()
        {
            std::lock_guard<std::mutex> lock(conn->pending_mutex);
            conn->pending.erase(nonce);
        };
        std::string error;
        if (!conn->write_frame(MessageKind::Assignment, tournament_wire::encode_assignment(assignment), error))
        {
            if (conn->closed.load())
            {
                erase_pending();
                return {DeliveryStatus::Unreachable, {}};
            }
            conn->fail_all(DeliveryStatus::Malformed);
            conn->kill();
            erase_pending();
            return {DeliveryStatus::Malformed, {}};
        }
        TimePoint const deadline = deadline_after(impl_->shared->config.io_timeout_ms);
        {
            std::unique_lock<std::mutex> lock(conn->pending_mutex);
            PendingRequest &entry = conn->pending[nonce];
            bool const fulfilled = conn->pending_cv.wait_until(lock, deadline, [&entry]()
            {
                return entry.done;
            });
            if (fulfilled)
            {
                tournament_transport::Delivery delivery;
                delivery.status = entry.status;
                delivery.result = std::move(entry.result);
                conn->pending.erase(nonce);
                return delivery;
            }
        }
        erase_pending();
        return {DeliveryStatus::Timeout, {}};
    }

    struct ClientConnection::Impl
    {
        ~Impl()
        {
            if (ctx != nullptr)
            {
                SSL_CTX_free(ctx);
            }
        }

        bool establish(std::string const &host, std::uint16_t port, std::string const &expected_fingerprint,
                       std::string &error)
        {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo *listing = nullptr;
            if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &listing) != 0)
            {
                error = "address resolution failed for " + host;
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
                error = "tcp connect failed to " + host;
                return false;
            }
            set_socket_timeouts(fd);
            ctx = SSL_CTX_new(TLS_client_method());
            if (ctx == nullptr)
            {
                error = "TLS client context creation failed";
                return false;
            }
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            ssl = SSL_new(ctx);
            if (ssl == nullptr || SSL_set_fd(ssl, fd) != 1
                || !ssl_handshake(ssl, false, deadline_after(kHelloReplyTimeoutMs)))
            {
                error = "TLS handshake failed";
                return false;
            }
            X509 *certificate = SSL_get1_peer_certificate(ssl);
            if (certificate == nullptr)
            {
                error = "server presented no certificate";
                return false;
            }
            unsigned char *der = nullptr;
            int const length = i2d_X509(certificate, &der);
            X509_free(certificate);
            if (length <= 0 || der == nullptr)
            {
                if (der != nullptr)
                {
                    OPENSSL_free(der);
                }
                error = "peer certificate encoding failed";
                return false;
            }
            std::vector<std::uint8_t> digest(SHA256_DIGEST_LENGTH);
            SHA256(der, static_cast<std::size_t>(length), digest.data());
            OPENSSL_free(der);
            if (normalize_fingerprint(expected_fingerprint) != tournament_bytes::encode_hex(digest))
            {
                error = "certificate fingerprint mismatch";
                return false;
            }
            connected.store(true);
            reader = std::thread(&Impl::reader_loop, this);
            return true;
        }

        void reader_loop()
        {
            for (;;)
            {
                FramedMessage message;
                if (read_frame(ssl, message, TimePoint::max()) != FrameRead::Frame)
                {
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    queue.push_back(std::move(message));
                }
                queue_cv.notify_all();
            }
            connected.store(false);
            closed.store(true);
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
            }
            queue_cv.notify_all();
        }

        bool write_frame(MessageKind kind, std::string const &payload, std::string &error)
        {
            std::lock_guard<std::mutex> lock(write_mutex);
            if (closed.load())
            {
                error = "connection closed";
                return false;
            }
            if (ssl_write_frame(ssl, kind, payload, deadline_after(kWriteTimeoutMs)))
            {
                return true;
            }
            error = "frame write failed";
            return false;
        }

        bool pop_frame(FramedMessage &out, TimePoint deadline, std::string &error)
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            bool const ready = queue_cv.wait_until(lock, deadline, [&]()
            {
                return !queue.empty() || !connected.load();
            });
            if (!ready && queue.empty())
            {
                error = "timed out waiting for frame";
                return false;
            }
            if (queue.empty())
            {
                error = "connection closed";
                return false;
            }
            out = std::move(queue.front());
            queue.pop_front();
            return true;
        }

        void mark_disconnected()
        {
            connected.store(false);
            if (!closed.exchange(true) && fd >= 0)
            {
                shutdown(fd, SHUT_RDWR);
            }
        }

        void release_socket()
        {
            std::lock_guard<std::mutex> lock(write_mutex);
            if (ssl != nullptr)
            {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl = nullptr;
            }
            if (fd >= 0)
            {
                ::close(fd);
                fd = -1;
            }
        }

        int fd = -1;
        SSL *ssl = nullptr;
        SSL_CTX *ctx = nullptr;
        std::thread reader;
        std::mutex queue_mutex;
        std::condition_variable queue_cv;
        std::deque<FramedMessage> queue;
        std::mutex write_mutex;
        std::atomic<bool> connected{false};
        std::atomic<bool> closed{false};
        std::atomic<bool> stop_requested{false};
        std::string connect_error;
    };

    ClientConnection::ClientConnection(std::string host, std::uint16_t port, std::string expected_fingerprint)
        : impl_(std::make_unique<Impl>())
    {
        ignore_sigpipe_once();
        std::string error;
        if (!impl_->establish(host, port, expected_fingerprint, error))
        {
            impl_->connect_error = error;
            impl_->connected.store(false);
            impl_->closed.store(true);
        }
    }

    ClientConnection::~ClientConnection()
    {
        close();
    }

    bool ClientConnection::connected() const
    {
        return impl_->connected.load() && !impl_->closed.load();
    }

    ClientConnection::HelloStatus ClientConnection::send_hello(HelloMessage const &hello, std::string &detail)
    {
        detail.clear();
        if (!impl_->connected.load() || impl_->closed.load())
        {
            detail = impl_->connect_error.empty()
                ? "not connected"
                : "not connected: " + impl_->connect_error;
            return HelloStatus::Failed;
        }
        std::string error;
        if (!impl_->write_frame(MessageKind::Hello, tournament_wire::encode_hello(hello), error))
        {
            detail = "hello write failed: " + error;
            impl_->mark_disconnected();
            return HelloStatus::Failed;
        }
        FramedMessage message;
        std::string wait_error;
        if (!impl_->pop_frame(message, deadline_after(kHelloReplyTimeoutMs), wait_error))
        {
            detail = wait_error;
            return HelloStatus::Failed;
        }
        if (message.kind == MessageKind::Accept)
        {
            return HelloStatus::Accepted;
        }
        if (message.kind == MessageKind::Reject)
        {
            auto const reason = tournament_wire::decode_reject(as_bytes(message.payload));
            detail = reason.value_or("malformed reject payload");
            return HelloStatus::Rejected;
        }
        detail = "unexpected frame kind while awaiting accept";
        return HelloStatus::Failed;
    }

    std::optional<AssignmentBatch> ClientConnection::next_assignment(std::uint64_t timeout_ms, std::string &detail)
    {
        detail.clear();
        if (!impl_->connected.load() && impl_->queue.empty())
        {
            detail = impl_->connect_error.empty()
                ? "not connected"
                : "not connected: " + impl_->connect_error;
            return std::nullopt;
        }
        FramedMessage message;
        std::string error;
        if (!impl_->pop_frame(message, deadline_after(timeout_ms), error))
        {
            detail = error;
            return std::nullopt;
        }
        if (message.kind == MessageKind::Assignment)
        {
            auto const batch = tournament_wire::decode_assignment(as_bytes(message.payload));
            if (!batch)
            {
                detail = "assignment payload failed to decode";
                return std::nullopt;
            }
            return batch;
        }
        if (message.kind == MessageKind::Reject)
        {
            auto const reason = tournament_wire::decode_reject(as_bytes(message.payload));
            detail = std::string("rejected: ") + reason.value_or("malformed reject payload");
            return std::nullopt;
        }
        detail = "unexpected frame kind while awaiting assignment";
        return std::nullopt;
    }

    bool ClientConnection::send_result(SignedResult const &result, std::string &detail)
    {
        detail.clear();
        if (!impl_->connected.load() || impl_->closed.load())
        {
            detail = impl_->connect_error.empty()
                ? "not connected"
                : "not connected: " + impl_->connect_error;
            return false;
        }
        std::string error;
        if (!impl_->write_frame(MessageKind::Result, tournament_wire::encode_result_message(result), error))
        {
            detail = "result write failed: " + error;
            impl_->mark_disconnected();
            return false;
        }
        return true;
    }

    void ClientConnection::close()
    {
        if (!impl_)
        {
            return;
        }
        bool const first = !impl_->stop_requested.exchange(true);
        if (first && impl_->fd >= 0)
        {
            shutdown(impl_->fd, SHUT_RDWR);
        }
        if (impl_->reader.joinable())
        {
            impl_->reader.join();
        }
        impl_->connected.store(false);
        impl_->closed.store(true);
        if (first)
        {
            impl_->release_socket();
        }
    }
}
