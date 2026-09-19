#include "tournament/net_transport.h"

#include <openssl/asn1.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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

        struct SslFailure
        {
            int error = SSL_ERROR_NONE;
            int errno_value = 0;
            unsigned long queue = 0;
        };

        thread_local SslFailure t_ssl_failure{};

        void record_ssl_failure(int error)
        {
            t_ssl_failure.error = error;
            t_ssl_failure.errno_value = errno;
            t_ssl_failure.queue = ERR_peek_last_error();
        }

        void clear_ssl_failure()
        {
            t_ssl_failure = SslFailure{};
        }

        std::string ssl_failure_reason()
        {
            switch (t_ssl_failure.error)
            {
            case SSL_ERROR_ZERO_RETURN:
                return "peer closed the tls stream";
            case SSL_ERROR_SYSCALL:
                if (t_ssl_failure.queue == 0)
                {
                    return "socket error errno " + std::to_string(t_ssl_failure.errno_value);
                }
                return "socket io failure errno " + std::to_string(t_ssl_failure.errno_value) + ": "
                    + ERR_reason_error_string(t_ssl_failure.queue);
            case SSL_ERROR_SSL:
                return "tls failure: "
                    + std::string(ERR_reason_error_string(t_ssl_failure.queue) != nullptr
                          ? ERR_reason_error_string(t_ssl_failure.queue)
                          : "unknown reason");
            case SSL_ERROR_WANT_READ:
                return "read stalled past its deadline";
            case SSL_ERROR_WANT_WRITE:
                return "write stalled past its deadline";
            default:
                break;
            }
            return "ssl error code " + std::to_string(t_ssl_failure.error);
        }

        bool ssl_write_all(SSL *ssl, char const *data, std::size_t size, TimePoint deadline)
        {
            clear_ssl_failure();
            std::size_t sent = 0;
            while (sent < size)
            {
                if (SteadyClock::now() > deadline)
                {
                    record_ssl_failure(SSL_ERROR_WANT_WRITE);
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
                record_ssl_failure(code);
                return false;
            }
            return true;
        }

        bool ssl_read_all(SSL *ssl, char *data, std::size_t size, TimePoint deadline)
        {
            clear_ssl_failure();
            std::size_t received = 0;
            while (received < size)
            {
                if (SteadyClock::now() > deadline)
                {
                    record_ssl_failure(SSL_ERROR_WANT_READ);
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
                record_ssl_failure(code);
                return false;
            }
            return true;
        }

        constexpr std::uint8_t kWsOpcodeContinuation = 0x0;
        constexpr std::uint8_t kWsOpcodeBinary = 0x2;
        constexpr std::uint8_t kWsOpcodeClose = 0x8;
        constexpr std::uint8_t kWsOpcodePing = 0x9;
        constexpr std::uint8_t kWsOpcodePong = 0xA;
        constexpr std::size_t kMaxHttpHeadBytes = 8192;
        constexpr std::uint64_t kWsMessageMax
            = static_cast<std::uint64_t>(tournament_wire::max_frame_payload) + 5;

        enum class FrameRead
        {
            Frame,
            Closed,
        };

        std::string base64_encode(std::vector<std::uint8_t> const &bytes)
        {
            static char const alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            encoded.reserve((bytes.size() + 2) / 3 * 4);
            std::size_t i = 0;
            while (i + 3 <= bytes.size())
            {
                std::uint32_t const group = (static_cast<std::uint32_t>(bytes[i]) << 16)
                    | (static_cast<std::uint32_t>(bytes[i + 1]) << 8)
                    | static_cast<std::uint32_t>(bytes[i + 2]);
                encoded.push_back(alphabet[(group >> 18) & 0x3F]);
                encoded.push_back(alphabet[(group >> 12) & 0x3F]);
                encoded.push_back(alphabet[(group >> 6) & 0x3F]);
                encoded.push_back(alphabet[group & 0x3F]);
                i += 3;
            }
            std::size_t const remaining = bytes.size() - i;
            if (remaining == 1)
            {
                std::uint32_t const group = static_cast<std::uint32_t>(bytes[i]) << 16;
                encoded.push_back(alphabet[(group >> 18) & 0x3F]);
                encoded.push_back(alphabet[(group >> 12) & 0x3F]);
                encoded.append("==");
            }
            else if (remaining == 2)
            {
                std::uint32_t const group = (static_cast<std::uint32_t>(bytes[i]) << 16)
                    | (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
                encoded.push_back(alphabet[(group >> 18) & 0x3F]);
                encoded.push_back(alphabet[(group >> 12) & 0x3F]);
                encoded.push_back(alphabet[(group >> 6) & 0x3F]);
                encoded.push_back('=');
            }
            return encoded;
        }

        bool stream_read_all(SSL *ssl, std::string &prefix, char *data, std::size_t size, TimePoint deadline)
        {
            if (!prefix.empty())
            {
                std::size_t const taken = std::min(size, prefix.size());
                std::memcpy(data, prefix.data(), taken);
                prefix.erase(0, taken);
                data += taken;
                size -= taken;
            }
            return size == 0 || ssl_read_all(ssl, data, size, deadline);
        }

        void ws_unmask(char *data, std::size_t size, std::uint8_t const *key)
        {
            for (std::size_t i = 0; i < size; ++i)
            {
                data[i] = static_cast<char>(static_cast<std::uint8_t>(data[i]) ^ key[i % 4]);
            }
        }

        bool ws_write_frame(SSL *ssl, bool mask, std::uint8_t opcode, std::string const &payload, TimePoint deadline)
        {
            std::string frame;
            frame.reserve(payload.size() + 14);
            frame.push_back(static_cast<char>(0x80 | opcode));
            std::uint8_t mask_key[4] = {};
            char const mask_flag = mask ? static_cast<char>(0x80) : static_cast<char>(0);
            std::size_t const length = payload.size();
            if (length < 126)
            {
                frame.push_back(static_cast<char>(mask_flag | static_cast<char>(length)));
            }
            else if (length <= 0xFFFF)
            {
                frame.push_back(static_cast<char>(mask_flag | 126));
                frame.push_back(static_cast<char>((length >> 8) & 0xFF));
                frame.push_back(static_cast<char>(length & 0xFF));
            }
            else
            {
                frame.push_back(static_cast<char>(mask_flag | 127));
                for (int shift = 56; shift >= 0; shift -= 8)
                {
                    frame.push_back(static_cast<char>((length >> shift) & 0xFF));
                }
            }
            if (mask)
            {
                if (RAND_bytes(mask_key, sizeof mask_key) != 1)
                {
                    return false;
                }
                frame.append(reinterpret_cast<char const *>(mask_key), sizeof mask_key);
            }
            frame.append(payload);
            if (mask)
            {
                ws_unmask(frame.data() + (frame.size() - payload.size()), payload.size(), mask_key);
            }
            return ssl_write_all(ssl, frame.data(), frame.size(), deadline);
        }

        using WsPongSender = std::function<bool(std::string const &)>;

        FrameRead ws_read_message(SSL *ssl, std::string &prefix, bool expect_masked, TimePoint deadline,
                                  WsPongSender const &send_pong, std::string &out)
        {
            std::string assembled;
            bool fragmenting = false;
            for (;;)
            {
                char header[2] = {};
                if (!stream_read_all(ssl, prefix, header, sizeof header, deadline))
                {
                    return FrameRead::Closed;
                }
                bool const fin = (header[0] & 0x80) != 0;
                std::uint8_t const opcode = static_cast<std::uint8_t>(header[0] & 0x0F);
                bool const masked = (header[1] & 0x80) != 0;
                std::uint64_t const length7 = static_cast<std::uint8_t>(header[1] & 0x7F);
                if (masked != expect_masked)
                {
                    return FrameRead::Closed;
                }
                if (opcode >= 0x8)
                {
                    if (!fin || length7 > 125)
                    {
                        return FrameRead::Closed;
                    }
                    std::uint8_t mask_key[4] = {};
                    if (masked
                        && !stream_read_all(ssl, prefix, reinterpret_cast<char *>(mask_key), 4, deadline))
                    {
                        return FrameRead::Closed;
                    }
                    std::string control(static_cast<std::size_t>(length7), '\0');
                    if (length7 > 0
                        && !stream_read_all(ssl, prefix, control.data(), control.size(), deadline))
                    {
                        return FrameRead::Closed;
                    }
                    if (masked)
                    {
                        ws_unmask(control.data(), control.size(), mask_key);
                    }
                    if (opcode == kWsOpcodePing)
                    {
                        if (!send_pong || !send_pong(control))
                        {
                            return FrameRead::Closed;
                        }
                    }
                    else if (opcode == kWsOpcodeClose)
                    {
                        return FrameRead::Closed;
                    }
                    continue;
                }
                if (opcode != kWsOpcodeBinary && opcode != kWsOpcodeContinuation)
                {
                    return FrameRead::Closed;
                }
                if (opcode == kWsOpcodeContinuation ? !fragmenting : fragmenting)
                {
                    return FrameRead::Closed;
                }
                std::uint64_t length = length7;
                if (length7 == 126)
                {
                    char extended[2] = {};
                    if (!stream_read_all(ssl, prefix, extended, sizeof extended, deadline))
                    {
                        return FrameRead::Closed;
                    }
                    length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[0])) << 8)
                        | static_cast<std::uint64_t>(static_cast<std::uint8_t>(extended[1]));
                }
                else if (length7 == 127)
                {
                    char extended[8] = {};
                    if (!stream_read_all(ssl, prefix, extended, sizeof extended, deadline))
                    {
                        return FrameRead::Closed;
                    }
                    length = 0;
                    for (char byte : extended)
                    {
                        length = (length << 8) | static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte));
                    }
                }
                if (length > kWsMessageMax
                    || static_cast<std::uint64_t>(assembled.size()) + length > kWsMessageMax)
                {
                    return FrameRead::Closed;
                }
                std::uint8_t mask_key[4] = {};
                if (masked && !stream_read_all(ssl, prefix, reinterpret_cast<char *>(mask_key), 4, deadline))
                {
                    return FrameRead::Closed;
                }
                std::string chunk(static_cast<std::size_t>(length), '\0');
                if (length > 0 && !stream_read_all(ssl, prefix, chunk.data(), chunk.size(), deadline))
                {
                    return FrameRead::Closed;
                }
                if (masked)
                {
                    ws_unmask(chunk.data(), chunk.size(), mask_key);
                }
                assembled.append(chunk);
                fragmenting = true;
                if (fin)
                {
                    out = std::move(assembled);
                    return FrameRead::Frame;
                }
            }
        }

        FrameRead read_frame(SSL *ssl, std::string &prefix, FramedMessage &out, TimePoint deadline)
        {
            char header[5] = {};
            if (!stream_read_all(ssl, prefix, header, sizeof header, deadline))
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

        struct WireMode
        {
            bool websocket = false;
            bool peer_masks = false;

            bool send_masked() const
            {
                return websocket && !peer_masks;
            }
        };

        FrameRead read_wire_frame(SSL *ssl, std::string &prefix, WireMode wire, FramedMessage &out,
                                  TimePoint deadline, WsPongSender const &send_pong)
        {
            if (!wire.websocket)
            {
                return read_frame(ssl, prefix, out, deadline);
            }
            std::string message;
            if (ws_read_message(ssl, prefix, wire.peer_masks, deadline, send_pong, message) != FrameRead::Frame)
            {
                return FrameRead::Closed;
            }
            if (message.size() < 5)
            {
                return FrameRead::Closed;
            }
            unsigned char const *raw = reinterpret_cast<unsigned char const *>(message.data());
            std::uint32_t const length = static_cast<std::uint32_t>(raw[0])
                | (static_cast<std::uint32_t>(raw[1]) << 8)
                | (static_cast<std::uint32_t>(raw[2]) << 16)
                | (static_cast<std::uint32_t>(raw[3]) << 24);
            std::uint8_t const kind = raw[4];
            if (length > tournament_wire::max_frame_payload || kind < 1 || kind > 5
                || message.size() != static_cast<std::size_t>(length) + 5)
            {
                return FrameRead::Closed;
            }
            out.kind = static_cast<MessageKind>(kind);
            out.payload = message.substr(5);
            return FrameRead::Frame;
        }

        bool write_wire_frame(SSL *ssl, WireMode wire, MessageKind kind, std::string const &payload,
                              TimePoint deadline)
        {
            std::string const frame = tournament_wire::frame_message(kind, payload);
            if (!wire.websocket)
            {
                return ssl_write_all(ssl, frame.data(), frame.size(), deadline);
            }
            return ws_write_frame(ssl, wire.send_masked(), kWsOpcodeBinary, frame, deadline);
        }

        void set_socket_timeouts(int fd)
        {
            timeval timeout{};
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        }

        void clear_receive_timeout(int fd)
        {
            timeval timeout{};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        }

        void enable_keepalive(int fd)
        {
            int const enabled = 1;
            int const idle = 30;
            int const interval = 10;
            int const count = 3;
            setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof enabled);
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
            setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
        }

        std::string http_trim(std::string const &text)
        {
            std::size_t const begin = text.find_first_not_of(" \t");
            if (begin == std::string::npos)
            {
                return {};
            }
            std::size_t const end = text.find_last_not_of(" \t");
            return text.substr(begin, end - begin + 1);
        }

        std::string http_lower(std::string const &text)
        {
            std::string lowered;
            lowered.reserve(text.size());
            for (char c : text)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                lowered.push_back(c);
            }
            return lowered;
        }

        struct HttpHead
        {
            std::string first_line;
            std::map<std::string, std::string> fields;
        };

        bool read_http_head(SSL *ssl, std::string &prefix, TimePoint deadline, HttpHead &head, std::string &rest)
        {
            std::string text = std::move(prefix);
            prefix.clear();
            while (text.find("\r\n\r\n") == std::string::npos)
            {
                if (text.size() > kMaxHttpHeadBytes || SteadyClock::now() > deadline)
                {
                    return false;
                }
                char chunk[512] = {};
                int const received = SSL_read(ssl, chunk, sizeof chunk);
                if (received <= 0)
                {
                    int const code = SSL_get_error(ssl, received);
                    if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
                    {
                        continue;
                    }
                    return false;
                }
                text.append(chunk, static_cast<std::size_t>(received));
            }
            std::size_t const terminator = text.find("\r\n\r\n");
            rest = text.substr(terminator + 4);
            std::size_t const first_end = text.find("\r\n");
            if (first_end == std::string::npos || first_end > terminator)
            {
                return false;
            }
            head.first_line = text.substr(0, first_end);
            std::size_t position = first_end + 2;
            while (position < terminator)
            {
                std::size_t const line_end = text.find("\r\n", position);
                if (line_end == std::string::npos || line_end > terminator)
                {
                    break;
                }
                std::string const line = text.substr(position, line_end - position);
                position = line_end + 2;
                std::size_t const colon = line.find(':');
                if (colon == std::string::npos)
                {
                    continue;
                }
                std::string const name = http_lower(http_trim(line.substr(0, colon)));
                std::string const value = http_trim(line.substr(colon + 1));
                head.fields.emplace(name, value);
            }
            return true;
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
            WireMode wire;
            std::string read_prefix;
            std::mutex write_mutex;
            std::mutex pending_mutex;
            std::condition_variable pending_cv;
            std::map<Nonce, PendingRequest> pending;
            std::atomic<bool> closed{false};
            std::atomic<std::uint64_t> frames_received{0};

            bool write_frame(MessageKind kind, std::string const &payload, std::string &error)
            {
                std::lock_guard<std::mutex> lock(write_mutex);
                if (closed.load())
                {
                    error = "connection closed";
                    return false;
                }
                if (write_wire_frame(ssl, wire, kind, payload, deadline_after(kWriteTimeoutMs)))
                {
                    return true;
                }
                error = "frame write failed";
                return false;
            }

            bool send_pong(std::string const &payload)
            {
                if (!wire.websocket)
                {
                    return true;
                }
                std::lock_guard<std::mutex> lock(write_mutex);
                if (closed.load())
                {
                    return false;
                }
                return ws_write_frame(ssl, wire.send_masked(), kWsOpcodePong, payload,
                                      deadline_after(kWriteTimeoutMs));
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
            std::vector<std::weak_ptr<HostConnection>> live_connections;
            std::optional<std::vector<AllowedDevice>> allowlist;
            std::vector<std::thread> readers;
        };

        bool validate_hello(HostShared &shared, HostConnection &conn, std::string &reason)
        {
            WsPongSender const send_pong = [&conn](std::string const &payload)
            {
                return conn.send_pong(payload);
            };
            FramedMessage message;
            if (read_wire_frame(conn.ssl, conn.read_prefix, conn.wire, message,
                                deadline_after(kHandshakeTimeoutMs), send_pong) != FrameRead::Frame)
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
            if (shared.allowlist.has_value())
            {
                auto const allowed = std::find_if(shared.allowlist->begin(), shared.allowlist->end(),
                    [device = hello->device](AllowedDevice const &entry)
                    {
                        return entry.id == device;
                    });
                if (allowed == shared.allowlist->end())
                {
                    reason = "device not in allowlist";
                    return false;
                }
                if (allowed->public_key != hello->public_key)
                {
                    reason = "device key does not match allowlist";
                    return false;
                }
            }
            if (shared.config.expected_engine_fingerprint != 0
                && hello->engine_fingerprint != shared.config.expected_engine_fingerprint)
            {
                reason = "engine fingerprint mismatch: client "
                    + std::format("{:016x}", hello->engine_fingerprint) + ", host expects "
                    + std::format("{:016x}", shared.config.expected_engine_fingerprint)
                    + ": inconforming build";
                return false;
            }
            if (!shared.registry)
            {
                reason = "registry unavailable";
                return false;
            }
            conn.device = hello->device;
            if (shared.registry->key_banned(hello->public_key))
            {
                reason = "device key is banned";
                return false;
            }
            if (shared.registry->blacklisted(hello->device))
            {
                reason = "device is blacklisted";
                return false;
            }
            tournament_wire::PublicKey const *stored = shared.registry->public_key(hello->device);
            if (stored != nullptr)
            {
                if (*stored != hello->public_key)
                {
                    reason = "device key mismatch";
                    return false;
                }
            }
            else
            {
                tournament_wire::PublicKey const *bound = shared.registry->bound_key(hello->device);
                if (bound != nullptr && *bound != hello->public_key)
                {
                    reason = "device id is bound to another key";
                    return false;
                }
                if (!shared.registry->enroll(hello->device, hello->public_key))
                {
                    reason = "enrollment failed";
                    return false;
                }
            }
            shared.registry->set_concurrency(hello->device, hello->max_concurrent_assignments);
            return true;
        }

        void connection_reader(std::shared_ptr<HostShared> shared, std::shared_ptr<HostConnection> conn)
        {
            WsPongSender const send_pong = [&conn = *conn](std::string const &payload)
            {
                return conn.send_pong(payload);
            };
            std::string reason = "peer closed connection";
            for (;;)
            {
                if (conn->closed.load())
                {
                    reason = "local shutdown";
                    break;
                }
                FramedMessage message;
                if (read_wire_frame(conn->ssl, conn->read_prefix, conn->wire, message, TimePoint::max(),
                                    send_pong) != FrameRead::Frame)
                {
                    reason = conn->closed.load() ? std::string("local shutdown")
                        : (t_ssl_failure.error == SSL_ERROR_NONE
                               ? std::string("malformed frame")
                               : ssl_failure_reason());
                    break;
                }
                if (message.kind != MessageKind::Result)
                {
                    reason = "protocol violation: frame kind "
                        + std::to_string(static_cast<int>(message.kind));
                    break;
                }
                std::optional<SignedResult> result = tournament_wire::decode_result_message(as_bytes(message.payload));
                if (!result)
                {
                    reason = "malformed result frame";
                    break;
                }
                conn->frames_received.fetch_add(1, std::memory_order_relaxed);
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
            if (shared->config.log)
            {
                shared->config.log("device " + std::to_string(conn->device) + " connection lost: " + reason);
            }
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                auto it = shared->connections.find(conn->device);
                if (it != shared->connections.end() && it->second == conn)
                {
                    shared->connections.erase(it);
                }
            }
        }

        bool ws_server_handshake(HostConnection &conn)
        {
            TimePoint const deadline = deadline_after(kHandshakeTimeoutMs);
            HttpHead head;
            std::string rest;
            if (!read_http_head(conn.ssl, conn.read_prefix, deadline, head, rest))
            {
                return false;
            }
            conn.read_prefix = std::move(rest);
            auto const upgrade = head.fields.find("upgrade");
            auto const key = head.fields.find("sec-websocket-key");
            if (upgrade == head.fields.end() || http_lower(upgrade->second) != "websocket")
            {
                return false;
            }
            if (key == head.fields.end() || key->second.empty())
            {
                return false;
            }
            std::string const response = "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + websocket_accept_token(key->second) + "\r\n"
                "\r\n";
            return ssl_write_all(conn.ssl, response.data(), response.size(), deadline);
        }

        bool negotiate_wire(HostConnection &conn)
        {
            char probe[4] = {};
            if (!ssl_read_all(conn.ssl, probe, sizeof probe, deadline_after(kHandshakeTimeoutMs)))
            {
                return false;
            }
            conn.read_prefix.assign(probe, sizeof probe);
            if (std::memcmp(probe, "GET ", sizeof probe) != 0)
            {
                return true;
            }
            conn.wire = WireMode{true, true};
            return ws_server_handshake(conn);
        }

        void handle_connection(std::shared_ptr<HostShared> shared, std::shared_ptr<HostConnection> conn)
        {
            set_socket_timeouts(conn->fd);
            conn->ssl = SSL_new(shared->ctx);
            if (conn->ssl == nullptr || SSL_set_fd(conn->ssl, conn->fd) != 1
                || !ssl_handshake(conn->ssl, true, deadline_after(kHandshakeTimeoutMs)))
            {
                conn->teardown();
                return;
            }
            if (!negotiate_wire(*conn))
            {
                conn->teardown();
                return;
            }
            std::string reason;
            if (!validate_hello(*shared, *conn, reason))
            {
                std::string error;
                conn->write_frame(MessageKind::Reject, tournament_wire::encode_reject(reason), error);
                if (shared->config.log)
                {
                    shared->config.log("connection rejected: " + reason);
                }
                conn->teardown();
                return;
            }
            bool accept_sent = false;
            {
                std::lock_guard<std::mutex> write_lock(conn->write_mutex);
                if (!conn->closed.load())
                {
                    accept_sent = write_wire_frame(conn->ssl, conn->wire, MessageKind::Accept, "",
                                                   deadline_after(kWriteTimeoutMs));
                }
            }
            if (!accept_sent)
            {
                conn->teardown();
                return;
            }
            clear_receive_timeout(conn->fd);
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
            if (shared->config.log)
            {
                shared->config.log("device " + std::to_string(conn->device) + " enrolled over "
                    + std::string(conn->wire.websocket ? "websocket" : "raw tls"));
            }
            if (stale)
            {
                stale->fail_all(DeliveryStatus::Unreachable);
                stale->kill();
                if (shared->config.log)
                {
                    shared->config.log("device " + std::to_string(conn->device)
                        + " replaced its previous connection");
                }
            }
        }

        std::size_t live_connection_count(HostShared &shared)
        {
            auto &live = shared.live_connections;
            live.erase(std::remove_if(live.begin(), live.end(),
                           [](std::weak_ptr<HostConnection> const &entry)
                           {
                               return entry.expired();
                           }),
                live.end());
            return live.size();
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
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->shutting_down.load())
                {
                    close(fd);
                    break;
                }
                if (live_connection_count(*shared)
                    >= static_cast<std::size_t>(shared->config.max_connections))
                {
                    close(fd);
                    continue;
                }
                std::shared_ptr<HostConnection> conn = std::make_shared<HostConnection>();
                conn->fd = fd;
                enable_keepalive(fd);
                shared->live_connections.push_back(conn);
                std::thread(handle_connection, shared, conn).detach();
            }
        }

        struct ClientCore
        {
            ~ClientCore()
            {
                if (ctx != nullptr)
                {
                    SSL_CTX_free(ctx);
                }
            }

            static bool host_is_name(std::string const &host)
            {
                sockaddr_in v4{};
                sockaddr_in6 v6{};
                return inet_pton(AF_INET, host.c_str(), &v4.sin_addr) != 1
                    && inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) != 1;
            }

            bool tcp_connect(std::string const &host, std::uint16_t port, std::string &error)
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
                return true;
            }

            bool tls_handshake_pinned(std::string const &host, std::string const &expected_fingerprint,
                                      std::string &error)
            {
                ctx = SSL_CTX_new(TLS_client_method());
                if (ctx == nullptr)
                {
                    error = "TLS client context creation failed";
                    return false;
                }
                SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
                ssl = SSL_new(ctx);
                if (ssl == nullptr || SSL_set_fd(ssl, fd) != 1)
                {
                    error = "TLS handshake failed";
                    return false;
                }
                if (host_is_name(host) && SSL_set_tlsext_host_name(ssl, host.c_str()) != 1)
                {
                    error = "TLS server name configuration failed";
                    return false;
                }
                if (!ssl_handshake(ssl, false, deadline_after(kHelloReplyTimeoutMs)))
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
                return true;
            }

            bool tls_handshake_ca(std::string const &host, std::string &error)
            {
                ctx = SSL_CTX_new(TLS_client_method());
                if (ctx == nullptr)
                {
                    error = "TLS client context creation failed";
                    return false;
                }
                SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
                if (SSL_CTX_set_default_verify_paths(ctx) != 1)
                {
                    error = "default verify paths unavailable";
                    return false;
                }
                ssl = SSL_new(ctx);
                if (ssl == nullptr || SSL_set_fd(ssl, fd) != 1)
                {
                    error = "TLS client setup failed";
                    return false;
                }
                SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
                if (host_is_name(host) && SSL_set_tlsext_host_name(ssl, host.c_str()) != 1)
                {
                    error = "TLS server name configuration failed";
                    return false;
                }
                if (!ssl_handshake(ssl, false, deadline_after(kHelloReplyTimeoutMs)))
                {
                    error = "TLS handshake failed";
                    return false;
                }
                if (SSL_get_verify_result(ssl) != X509_V_OK)
                {
                    error = "certificate verification failed";
                    return false;
                }
                X509 *certificate = SSL_get1_peer_certificate(ssl);
                if (certificate == nullptr)
                {
                    error = "server presented no certificate";
                    return false;
                }
                int const matched = X509_check_host(certificate, host.c_str(), host.size(), 0, nullptr);
                X509_free(certificate);
                if (matched != 1)
                {
                    error = "certificate hostname mismatch";
                    return false;
                }
                return true;
            }

            void finish_connect()
            {
                clear_receive_timeout(fd);
                connected.store(true);
                reader = std::thread(&ClientCore::reader_loop, this);
                if (wire.websocket && heartbeat_ms > 0)
                {
                    heartbeat = std::thread(&ClientCore::heartbeat_loop, this);
                }
            }

            void heartbeat_loop()
            {
                std::uint64_t elapsed_ms = 0;
                while (!stop_requested.load() && !closed.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    elapsed_ms += 250;
                    if (elapsed_ms < heartbeat_ms)
                    {
                        continue;
                    }
                    elapsed_ms = 0;
                    std::lock_guard<std::mutex> lock(write_mutex);
                    if (closed.load() || ssl == nullptr)
                    {
                        return;
                    }
                    if (!ws_write_frame(ssl, wire.send_masked(), kWsOpcodePing, "hb",
                                        deadline_after(kWriteTimeoutMs)))
                    {
                        mark_disconnected();
                        return;
                    }
                }
            }

            void reader_loop()
            {
                WsPongSender const send_pong = [this](std::string const &payload)
                {
                    return send_ws_pong(payload);
                };
                std::string reason;
                for (;;)
                {
                    FramedMessage message;
                    if (read_wire_frame(ssl, read_prefix, wire, message, TimePoint::max(), send_pong)
                        != FrameRead::Frame)
                    {
                        reason = (closed.load() || stop_requested.load())
                            ? std::string("local shutdown")
                            : (t_ssl_failure.error == SSL_ERROR_NONE
                                   ? std::string("malformed frame")
                                   : ssl_failure_reason());
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        queue.push_back(std::move(message));
                    }
                    queue_cv.notify_all();
                }
                disconnect_reason = std::move(reason);
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
                if (write_wire_frame(ssl, wire, kind, payload, deadline_after(kWriteTimeoutMs)))
                {
                    return true;
                }
                error = "frame write failed";
                return false;
            }

            bool send_ws_pong(std::string const &payload)
            {
                if (!wire.websocket)
                {
                    return true;
                }
                std::lock_guard<std::mutex> lock(write_mutex);
                if (closed.load())
                {
                    return false;
                }
                return ws_write_frame(ssl, wire.send_masked(), kWsOpcodePong, payload,
                                      deadline_after(kWriteTimeoutMs));
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
                    error = disconnect_reason.empty()
                        ? std::string("connection closed")
                        : "connection closed: " + disconnect_reason;
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
            WireMode wire;
            std::string read_prefix;
            std::thread reader;
            std::thread heartbeat;
            std::uint64_t heartbeat_ms = 0;
            std::mutex queue_mutex;
            std::condition_variable queue_cv;
            std::deque<FramedMessage> queue;
            std::mutex write_mutex;
            std::atomic<bool> connected{false};
            std::atomic<bool> closed{false};
            std::atomic<bool> stop_requested{false};
            std::string connect_error;
            std::string disconnect_reason;
        };

        bool client_connected(ClientCore const &core)
        {
            return core.connected.load() && !core.closed.load();
        }

        ClientConnectionBase::HelloStatus client_send_hello(ClientCore &core, HelloMessage const &hello,
                                                            std::string &detail)
        {
            detail.clear();
            if (!core.connected.load() || core.closed.load())
            {
                detail = !core.disconnect_reason.empty()
                    ? "not connected: " + core.disconnect_reason
                    : (core.connect_error.empty()
                           ? std::string("not connected")
                           : "not connected: " + core.connect_error);
                return ClientConnectionBase::HelloStatus::Failed;
            }
            std::string error;
            if (!core.write_frame(MessageKind::Hello, tournament_wire::encode_hello(hello), error))
            {
                detail = "hello write failed: " + error;
                core.mark_disconnected();
                return ClientConnectionBase::HelloStatus::Failed;
            }
            FramedMessage message;
            std::string wait_error;
            if (!core.pop_frame(message, deadline_after(kHelloReplyTimeoutMs), wait_error))
            {
                detail = wait_error;
                return ClientConnectionBase::HelloStatus::Failed;
            }
            if (message.kind == MessageKind::Accept)
            {
                return ClientConnectionBase::HelloStatus::Accepted;
            }
            if (message.kind == MessageKind::Reject)
            {
                auto const reason = tournament_wire::decode_reject(as_bytes(message.payload));
                detail = reason.value_or("malformed reject payload");
                return ClientConnectionBase::HelloStatus::Rejected;
            }
            detail = "unexpected frame kind while awaiting accept";
            return ClientConnectionBase::HelloStatus::Failed;
        }

        std::optional<AssignmentBatch> client_next_assignment(ClientCore &core, std::uint64_t timeout_ms,
                                                              std::string &detail)
        {
            detail.clear();
            if (!core.connected.load() && core.queue.empty())
            {
                detail = core.connect_error.empty()
                    ? "not connected"
                    : "not connected: " + core.connect_error;
                return std::nullopt;
            }
            FramedMessage message;
            std::string error;
            if (!core.pop_frame(message, deadline_after(timeout_ms), error))
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

        bool client_send_result(ClientCore &core, SignedResult const &result, std::string &detail)
        {
            detail.clear();
            if (!core.connected.load() || core.closed.load())
            {
                detail = !core.disconnect_reason.empty()
                    ? "not connected: " + core.disconnect_reason
                    : (core.connect_error.empty()
                           ? std::string("not connected")
                           : "not connected: " + core.connect_error);
                return false;
            }
            std::string error;
            if (!core.write_frame(MessageKind::Result, tournament_wire::encode_result_message(result), error))
            {
                detail = "result write failed: " + error;
                core.mark_disconnected();
                return false;
            }
            return true;
        }

        void client_close(ClientCore &core)
        {
            bool const first = !core.stop_requested.exchange(true);
            if (first && core.fd >= 0)
            {
                shutdown(core.fd, SHUT_RDWR);
            }
            if (core.heartbeat.joinable())
            {
                core.heartbeat.join();
            }
            if (core.reader.joinable())
            {
                core.reader.join();
            }
            core.connected.store(false);
            core.closed.store(true);
            if (first)
            {
                core.release_socket();
            }
        }
    }

    std::optional<std::vector<AllowedDevice>> load_devices_file(std::string const &path, std::string &error)
    {
        error.clear();
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "cannot read devices file: " + path;
            return std::nullopt;
        }
        std::vector<AllowedDevice> devices;
        std::set<DeviceId> seen;
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            if (line.empty())
            {
                continue;
            }
            std::size_t const separator = line.find(' ');
            if (separator == std::string::npos)
            {
                error = "devices file line " + std::to_string(line_number)
                    + ": expected '<device id> <public key hex>'";
                return std::nullopt;
            }
            std::string const id_text = line.substr(0, separator);
            std::string const key_text = line.substr(separator + 1);
            DeviceId id = 0;
            bool const id_valid = !id_text.empty()
                && id_text.find_first_not_of("0123456789") == std::string::npos
                && std::from_chars(id_text.data(), id_text.data() + id_text.size(), id).ec == std::errc{};
            if (!id_valid)
            {
                error = "devices file line " + std::to_string(line_number) + ": invalid decimal device id";
                return std::nullopt;
            }
            if (key_text.size() != 64)
            {
                error = "devices file line " + std::to_string(line_number)
                    + ": public key must be 64 hex characters";
                return std::nullopt;
            }
            if (key_text.find_first_of("ABCDEF") != std::string::npos)
            {
                error = "devices file line " + std::to_string(line_number) + ": uppercase hex is not allowed";
                return std::nullopt;
            }
            std::optional<std::vector<std::uint8_t>> key = tournament_bytes::decode_hex(key_text);
            if (!key || key->size() != 32)
            {
                error = "devices file line " + std::to_string(line_number) + ": malformed public key hex";
                return std::nullopt;
            }
            if (!seen.insert(id).second)
            {
                error = "devices file line " + std::to_string(line_number) + ": duplicate device id "
                    + std::to_string(id);
                return std::nullopt;
            }
            devices.push_back(AllowedDevice{id, std::move(*key)});
        }
        if (input.bad())
        {
            error = "error reading devices file: " + path;
            return std::nullopt;
        }
        return devices;
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

    std::string websocket_accept_token(std::string const &sec_websocket_key)
    {
        static std::string const guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        std::string const input = sec_websocket_key + guid;
        std::vector<std::uint8_t> digest(SHA_DIGEST_LENGTH);
        SHA1(reinterpret_cast<unsigned char const *>(input.data()), input.size(), digest.data());
        return base64_encode(digest);
    }

    struct HostTransport::Impl
    {
        Impl(std::shared_ptr<tournament_registry::DeviceRegistry> registry_in, NetConfig config_in)
            : shared(std::make_shared<HostShared>())
        {
            shared->registry = std::move(registry_in);
            shared->config = config_in;
            if (shared->config.max_connections <= 0)
            {
                shared->config.max_connections = NetConfig{}.max_connections;
            }
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
        if (!impl_->shared->config.devices_file.empty())
        {
            std::string load_error;
            std::optional<std::vector<AllowedDevice>> allowlist
                = load_devices_file(impl_->shared->config.devices_file, load_error);
            if (!allowlist)
            {
                error = load_error;
                return false;
            }
            impl_->shared->allowlist = std::move(allowlist);
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
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        addrinfo *resolved = nullptr;
        if (getaddrinfo(impl_->shared->config.listen_address.c_str(),
                        std::to_string(impl_->shared->config.port).c_str(), &hints, &resolved) != 0
            || resolved == nullptr)
        {
            SSL_CTX_free(ctx);
            error = "address resolution failed for " + impl_->shared->config.listen_address;
            return false;
        }
        int fd = -1;
        for (addrinfo const *entry = resolved; entry != nullptr && fd < 0; entry = entry->ai_next)
        {
            int candidate = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
            if (candidate < 0)
            {
                continue;
            }
            int const reuse = 1;
            setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
            if (entry->ai_family == AF_INET6)
            {
                int const v6only = 0;
                setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only);
            }
            if (bind(candidate, entry->ai_addr, entry->ai_addrlen) == 0
                && listen(candidate, kListenBacklog) == 0)
            {
                fd = candidate;
            }
            else
            {
                close(candidate);
            }
        }
        freeaddrinfo(resolved);
        if (fd < 0)
        {
            SSL_CTX_free(ctx);
            error = "bind or listen failed on " + impl_->shared->config.listen_address;
            return false;
        }
        sockaddr_storage bound{};
        socklen_t bound_length = sizeof bound;
        std::uint16_t assigned = 0;
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &bound_length) == 0)
        {
            if (bound.ss_family == AF_INET6)
            {
                assigned = ntohs(reinterpret_cast<sockaddr_in6 const *>(&bound)->sin6_port);
            }
            else
            {
                assigned = ntohs(reinterpret_cast<sockaddr_in const *>(&bound)->sin_port);
            }
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

    tournament_transport::Delivery HostTransport::request(DeviceId device, AssignmentBatch const &assignment,
                                                          std::uint64_t wait_ms)
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
        std::uint64_t const frames_before = conn->frames_received.load(std::memory_order_relaxed);
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
            if (impl_->shared->config.log)
            {
                impl_->shared->config.log("device " + std::to_string(device)
                    + " assignment write failed: " + error);
            }
            return {DeliveryStatus::Malformed, {}};
        }
        TimePoint const deadline = deadline_after(wait_ms);
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
        tournament_transport::Delivery delivery;
        delivery.status = DeliveryStatus::Timeout;
        delivery.peer_active = conn->frames_received.load(std::memory_order_relaxed) != frames_before;
        if (impl_->shared->config.log)
        {
            impl_->shared->config.log("device " + std::to_string(device) + " nonce " + std::to_string(nonce)
                + " timed out after " + std::to_string(wait_ms) + " ms"
                + (delivery.peer_active ? " (peer still sending)" : " (peer silent)"));
        }
        return delivery;
    }

    struct ClientConnection::Impl : ClientCore
    {
        bool establish(std::string const &host, std::uint16_t port, std::string const &expected_fingerprint,
                       std::string &error)
        {
            if (!tcp_connect(host, port, error))
            {
                return false;
            }
            if (!tls_handshake_pinned(host, expected_fingerprint, error))
            {
                return false;
            }
            finish_connect();
            return true;
        }
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
        return client_connected(*impl_);
    }

    ClientConnection::HelloStatus ClientConnection::send_hello(HelloMessage const &hello, std::string &detail)
    {
        return client_send_hello(*impl_, hello, detail);
    }

    std::optional<AssignmentBatch> ClientConnection::next_assignment(std::uint64_t timeout_ms, std::string &detail)
    {
        return client_next_assignment(*impl_, timeout_ms, detail);
    }

    bool ClientConnection::send_result(SignedResult const &result, std::string &detail)
    {
        return client_send_result(*impl_, result, detail);
    }

    void ClientConnection::close()
    {
        if (!impl_)
        {
            return;
        }
        client_close(*impl_);
    }

    struct WsClientConnection::Impl : ClientCore
    {
        bool ws_upgrade(std::string const &host, std::uint16_t port, std::string const &path, std::string &error)
        {
            TimePoint const deadline = deadline_after(kHelloReplyTimeoutMs);
            std::vector<std::uint8_t> nonce(16);
            if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1)
            {
                error = "websocket key generation failed";
                return false;
            }
            std::string const key = base64_encode(nonce);
            std::string const request = "GET " + (path.empty() ? std::string("/") : path) + " HTTP/1.1\r\n"
                "Host: " + host + ":" + std::to_string(port) + "\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: " + key + "\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n";
            if (!ssl_write_all(ssl, request.data(), request.size(), deadline))
            {
                error = "websocket upgrade request failed";
                return false;
            }
            HttpHead head;
            std::string rest;
            if (!read_http_head(ssl, read_prefix, deadline, head, rest))
            {
                error = "websocket upgrade response incomplete";
                return false;
            }
            read_prefix = std::move(rest);
            if (head.first_line.size() < 12 || head.first_line.compare(8, 4, " 101") != 0)
            {
                error = "websocket upgrade refused";
                return false;
            }
            auto const accept = head.fields.find("sec-websocket-accept");
            if (accept == head.fields.end() || accept->second != websocket_accept_token(key))
            {
                error = "websocket accept key mismatch";
                return false;
            }
            return true;
        }

        bool establish(std::string const &host, std::uint16_t port, std::string const &expected_fingerprint,
                       std::string const &path, bool ca_verified, std::uint64_t heartbeat_interval_ms,
                       std::string &error)
        {
            heartbeat_ms = heartbeat_interval_ms;
            if (!tcp_connect(host, port, error))
            {
                return false;
            }
            if (ca_verified)
            {
                if (!tls_handshake_ca(host, error))
                {
                    return false;
                }
            }
            else if (!tls_handshake_pinned(host, expected_fingerprint, error))
            {
                return false;
            }
            wire = WireMode{true, false};
            if (!ws_upgrade(host, port, path, error))
            {
                return false;
            }
            finish_connect();
            return true;
        }
    };

    WsClientConnection::WsClientConnection(std::string host, std::uint16_t port, std::string expected_fingerprint,
                                           std::string path, bool ca_verified, std::uint64_t heartbeat_ms)
        : impl_(std::make_unique<Impl>())
    {
        ignore_sigpipe_once();
        std::string error;
        if (!impl_->establish(host, port, expected_fingerprint, path, ca_verified, heartbeat_ms, error))
        {
            impl_->connect_error = error;
            impl_->connected.store(false);
            impl_->closed.store(true);
        }
    }

    WsClientConnection::~WsClientConnection()
    {
        close();
    }

    bool WsClientConnection::connected() const
    {
        return client_connected(*impl_);
    }

    WsClientConnection::HelloStatus WsClientConnection::send_hello(HelloMessage const &hello, std::string &detail)
    {
        return client_send_hello(*impl_, hello, detail);
    }

    std::optional<AssignmentBatch> WsClientConnection::next_assignment(std::uint64_t timeout_ms, std::string &detail)
    {
        return client_next_assignment(*impl_, timeout_ms, detail);
    }

    bool WsClientConnection::send_result(SignedResult const &result, std::string &detail)
    {
        return client_send_result(*impl_, result, detail);
    }

    void WsClientConnection::close()
    {
        if (!impl_)
        {
            return;
        }
        client_close(*impl_);
    }
}
