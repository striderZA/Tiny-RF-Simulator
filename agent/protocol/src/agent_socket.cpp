#include "agent_socket.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

constexpr std::size_t kSocketBufferBytes = 8192;

enum class WaitStatus { Ready, Timeout, Error };

void setError(std::string *error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

#ifdef _WIN32
struct WinsockState {
    explicit WinsockState(int startup_error) : startup_error(startup_error) {}
    ~WinsockState() {
        if (startup_error == 0) {
            WSACleanup();
        }
    }

    int startup_error;
};
#endif

int initializeSocketSubsystem() {
#ifdef _WIN32
    static const WinsockState state{[] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data);
    }()};
    return state.startup_error;
#else
    return 0;
#endif
}

std::string socketErrorMessage(std::string_view operation, int error_code) {
#ifdef _WIN32
    return std::string(operation) + " failed with Winsock error " + std::to_string(error_code);
#else
    return std::string(operation) + ": " + std::strerror(error_code);
#endif
}

int lastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

NativeSocket nativeSocket(std::intptr_t socket) {
#ifdef _WIN32
    return static_cast<NativeSocket>(static_cast<std::uintptr_t>(socket));
#else
    return static_cast<NativeSocket>(socket);
#endif
}

std::intptr_t storedSocket(NativeSocket socket) { return static_cast<std::intptr_t>(socket); }

bool isInvalidSocket(NativeSocket socket) { return socket == kInvalidSocket; }

void closeNativeSocket(NativeSocket socket) {
#ifdef _WIN32
    static_cast<void>(closesocket(socket));
#else
    static_cast<void>(::close(socket));
#endif
}

Clock::time_point deadlineAfter(std::chrono::milliseconds timeout) {
    const auto now = Clock::now();
    if (timeout <= std::chrono::milliseconds::zero()) {
        return now;
    }

    const auto remaining = Clock::time_point::max() - now;
    if (timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(remaining)) {
        return Clock::time_point::max();
    }
    return now + timeout;
}

WaitStatus waitForSocket(NativeSocket socket, bool writable, Clock::time_point deadline,
                         int *error_code = nullptr) {
    for (;;) {
        const auto now = Clock::now();
        if (now >= deadline) {
            return WaitStatus::Timeout;
        }
        const auto remaining = deadline - now;

#ifdef _WIN32
        const auto timeout_us = std::chrono::ceil<std::chrono::microseconds>(remaining).count();
        const auto max_seconds = static_cast<std::int64_t>(std::numeric_limits<long>::max());
        const auto seconds = timeout_us / 1000000;
        timeval timeout{};
        if (seconds > max_seconds) {
            timeout.tv_sec = std::numeric_limits<long>::max();
            timeout.tv_usec = 999999;
        } else {
            timeout.tv_sec = static_cast<long>(seconds);
            timeout.tv_usec = static_cast<long>(timeout_us % 1000000);
        }

        fd_set read_set;
        fd_set write_set;
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_SET(socket, writable ? &write_set : &read_set);
        const int result = select(0, writable ? nullptr : &read_set,
                                  writable ? &write_set : nullptr, nullptr, &timeout);
        if (result > 0) {
            return WaitStatus::Ready;
        }
        if (result == 0) {
            continue;
        }
        const int failure = WSAGetLastError();
        if (failure == WSAEINTR) {
            continue;
        }
        if (error_code != nullptr) {
            *error_code = failure;
        }
        return WaitStatus::Error;
#else
        const auto timeout_ms = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        const int poll_timeout = static_cast<int>(std::min<std::int64_t>(timeout_ms, INT_MAX));
        pollfd descriptor{};
        descriptor.fd = socket;
        descriptor.events = writable ? POLLOUT : POLLIN;
        const int result = poll(&descriptor, 1, poll_timeout);
        if (result > 0) {
            if ((descriptor.revents & POLLNVAL) != 0) {
                if (error_code != nullptr) {
                    *error_code = EBADF;
                }
                return WaitStatus::Error;
            }
            if ((descriptor.revents & (descriptor.events | POLLERR | POLLHUP)) != 0) {
                return WaitStatus::Ready;
            }
            continue;
        }
        if (result == 0) {
            continue;
        }
        const int failure = errno;
        if (failure == EINTR) {
            continue;
        }
        if (error_code != nullptr) {
            *error_code = failure;
        }
        return WaitStatus::Error;
#endif
    }
}

bool setNonBlocking(NativeSocket socket, bool nonblocking, int &error_code) {
#ifdef _WIN32
    u_long mode = nonblocking ? 1UL : 0UL;
    if (ioctlsocket(socket, FIONBIO, &mode) == SOCKET_ERROR) {
        error_code = WSAGetLastError();
        return false;
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) {
        error_code = errno;
        return false;
    }
    const int updated_flags = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket, F_SETFL, updated_flags) < 0) {
        error_code = errno;
        return false;
    }
#endif
    return true;
}

bool isConnectPending(int error_code) {
#ifdef _WIN32
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS ||
           error_code == WSAEALREADY || error_code == WSAEINTR;
#else
    return error_code == EINPROGRESS || error_code == EALREADY || error_code == EWOULDBLOCK ||
           error_code == EINTR;
#endif
}

bool isInterrupted(int error_code) {
#ifdef _WIN32
    return error_code == WSAEINTR;
#else
    return error_code == EINTR;
#endif
}

bool isWouldBlock(int error_code) {
#ifdef _WIN32
    return error_code == WSAEWOULDBLOCK;
#else
    return error_code == EAGAIN || error_code == EWOULDBLOCK;
#endif
}

} // namespace

AgentChannel::AgentChannel(std::intptr_t socket) noexcept : m_socket(socket) {}

AgentChannel::AgentChannel(AgentChannel &&other) noexcept
    : m_socket(std::exchange(other.m_socket, -1)), m_reader(std::move(other.m_reader)) {}

AgentChannel &AgentChannel::operator=(AgentChannel &&other) noexcept {
    if (this != &other) {
        close();
        m_socket = std::exchange(other.m_socket, -1);
        m_reader = std::move(other.m_reader);
    }
    return *this;
}

AgentChannel::~AgentChannel() { close(); }

AgentReadStatus AgentChannel::readLine(std::string &line, std::chrono::milliseconds timeout) {
    line.clear();
    const auto deadline = deadlineAfter(timeout);
    std::array<char, kSocketBufferBytes> buffer{};

    for (;;) {
        switch (m_reader.next(line)) {
        case AgentLineReader::Next::Line:
            return AgentReadStatus::Line;
        case AgentLineReader::Next::Oversized:
            return AgentReadStatus::Oversized;
        case AgentLineReader::Next::NeedMore:
            break;
        }

        if (!isOpen()) {
            return AgentReadStatus::Closed;
        }

        const NativeSocket socket = nativeSocket(m_socket);
        int failure = 0;
        const WaitStatus ready = waitForSocket(socket, false, deadline, &failure);
        if (ready == WaitStatus::Timeout) {
            return AgentReadStatus::Timeout;
        }
        if (ready == WaitStatus::Error) {
            close();
            return AgentReadStatus::Error;
        }

#ifdef _WIN32
        const int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const ssize_t received = recv(socket, buffer.data(), buffer.size(), 0);
#endif
        if (received > 0) {
            m_reader.append(std::string_view{buffer.data(), static_cast<std::size_t>(received)});
            continue;
        }
        if (received == 0) {
            close();
            return AgentReadStatus::Closed;
        }

        failure = lastSocketError();
        if (isInterrupted(failure)) {
            continue;
        }
        if (isWouldBlock(failure)) {
            continue;
        }
        close();
        return AgentReadStatus::Error;
    }
}

bool AgentChannel::writeLine(std::string_view line) {
    if (!isOpen() || line.find('\n') != std::string_view::npos) {
        return false;
    }

    std::string message(line);
    message.push_back('\n');
    const NativeSocket socket = nativeSocket(m_socket);
    std::size_t sent_total = 0;
    while (sent_total < message.size()) {
        const auto remaining = message.size() - sent_total;
        const int chunk_size = static_cast<int>(std::min<std::size_t>(remaining, INT_MAX));
#ifdef _WIN32
        const int sent = send(socket, message.data() + sent_total, chunk_size, 0);
#else
#ifdef MSG_NOSIGNAL
        constexpr int send_flags = MSG_NOSIGNAL;
#else
        constexpr int send_flags = 0;
#endif
        const ssize_t sent = send(socket, message.data() + sent_total,
                                  static_cast<std::size_t>(chunk_size), send_flags);
#endif
        if (sent > 0) {
            sent_total += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && isInterrupted(lastSocketError())) {
            continue;
        }
        close();
        return false;
    }
    return true;
}

void AgentChannel::close() {
    if (m_socket == -1) {
        return;
    }
    const NativeSocket socket = nativeSocket(m_socket);
    m_socket = -1;
    closeNativeSocket(socket);
}

bool AgentChannel::isOpen() const { return m_socket != -1; }

AgentListener::AgentListener(std::intptr_t socket, int port) noexcept
    : m_socket(socket), m_port(port) {}

AgentListener::AgentListener(AgentListener &&other) noexcept
    : m_socket(std::exchange(other.m_socket, -1)), m_port(std::exchange(other.m_port, 0)) {}

AgentListener &AgentListener::operator=(AgentListener &&other) noexcept {
    if (this != &other) {
        close();
        m_socket = std::exchange(other.m_socket, -1);
        m_port = std::exchange(other.m_port, 0);
    }
    return *this;
}

AgentListener::~AgentListener() { close(); }

std::optional<AgentListener> AgentListener::bindLoopback(std::string *error) {
    if (error != nullptr) {
        error->clear();
    }
    const int startup_error = initializeSocketSubsystem();
    if (startup_error != 0) {
        setError(error, socketErrorMessage("Winsock initialization", startup_error));
        return std::nullopt;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (isInvalidSocket(socket)) {
        setError(error, socketErrorMessage("socket", lastSocketError()));
        return std::nullopt;
    }

#ifdef _WIN32
    const int exclusive = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
        const int failure = lastSocketError();
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("setsockopt(SO_EXCLUSIVEADDRUSE)", failure));
        return std::nullopt;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (::bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        const int failure = lastSocketError();
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("bind loopback listener", failure));
        return std::nullopt;
    }
    if (::listen(socket, SOMAXCONN) != 0) {
        const int failure = lastSocketError();
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("listen", failure));
        return std::nullopt;
    }

    sockaddr_in bound_address{};
#ifdef _WIN32
    int address_length = sizeof(bound_address);
#else
    socklen_t address_length = sizeof(bound_address);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&bound_address), &address_length) != 0) {
        const int failure = lastSocketError();
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("getsockname", failure));
        return std::nullopt;
    }

    return AgentListener{storedSocket(socket), static_cast<int>(ntohs(bound_address.sin_port))};
}

int AgentListener::port() const { return m_port; }

std::optional<AgentChannel> AgentListener::accept(std::chrono::milliseconds timeout) {
    if (m_socket == -1) {
        return std::nullopt;
    }

    const auto deadline = deadlineAfter(timeout);
    const NativeSocket listener = nativeSocket(m_socket);
    for (;;) {
        int failure = 0;
        const WaitStatus ready = waitForSocket(listener, false, deadline, &failure);
        if (ready != WaitStatus::Ready) {
            return std::nullopt;
        }

        sockaddr_in peer_address{};
#ifdef _WIN32
        int peer_length = sizeof(peer_address);
#else
        socklen_t peer_length = sizeof(peer_address);
#endif
        const NativeSocket accepted =
            ::accept(listener, reinterpret_cast<sockaddr *>(&peer_address), &peer_length);
        if (isInvalidSocket(accepted)) {
            failure = lastSocketError();
            if (isInterrupted(failure) || isWouldBlock(failure)) {
                continue;
            }
            return std::nullopt;
        }

        if (peer_address.sin_family == AF_INET &&
            peer_address.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            return AgentChannel{storedSocket(accepted)};
        }
        closeNativeSocket(accepted);
    }
}

void AgentListener::close() {
    if (m_socket == -1) {
        return;
    }
    const NativeSocket socket = nativeSocket(m_socket);
    m_socket = -1;
    closeNativeSocket(socket);
}

std::optional<AgentChannel> connectAgentLoopback(int port, std::chrono::milliseconds timeout,
                                                 std::string *error) {
    if (error != nullptr) {
        error->clear();
    }
    if (port < 1 || port > 65535) {
        setError(error, "loopback port is outside 1..65535");
        return std::nullopt;
    }
    const auto deadline = deadlineAfter(timeout);

    const int startup_error = initializeSocketSubsystem();
    if (startup_error != 0) {
        setError(error, socketErrorMessage("Winsock initialization", startup_error));
        return std::nullopt;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (isInvalidSocket(socket)) {
        setError(error, socketErrorMessage("socket", lastSocketError()));
        return std::nullopt;
    }

    int failure = 0;
    if (!setNonBlocking(socket, true, failure)) {
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("set nonblocking socket", failure));
        return std::nullopt;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    const int connect_result =
        ::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    bool connected = connect_result == 0;
    if (!connected) {
        failure = lastSocketError();
        if (!isConnectPending(failure)) {
            closeNativeSocket(socket);
            setError(error, socketErrorMessage("connect loopback", failure));
            return std::nullopt;
        }

        int wait_error = 0;
        const WaitStatus ready = waitForSocket(socket, true, deadline, &wait_error);
        if (ready == WaitStatus::Timeout) {
            closeNativeSocket(socket);
            setError(error, "connect loopback timed out");
            return std::nullopt;
        }
        if (ready == WaitStatus::Error) {
            closeNativeSocket(socket);
            setError(error, socketErrorMessage("wait for loopback connection", wait_error));
            return std::nullopt;
        }

        int socket_error = 0;
#ifdef _WIN32
        int socket_error_length = sizeof(socket_error);
#else
        socklen_t socket_error_length = sizeof(socket_error);
#endif
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&socket_error),
                       &socket_error_length) != 0) {
            failure = lastSocketError();
            closeNativeSocket(socket);
            setError(error, socketErrorMessage("getsockopt(SO_ERROR)", failure));
            return std::nullopt;
        }
        if (socket_error != 0) {
            closeNativeSocket(socket);
            setError(error, socketErrorMessage("connect loopback", socket_error));
            return std::nullopt;
        }
        connected = true;
    }

    if (connected && !setNonBlocking(socket, false, failure)) {
        closeNativeSocket(socket);
        setError(error, socketErrorMessage("restore blocking socket", failure));
        return std::nullopt;
    }
    return AgentChannel{storedSocket(socket)};
}
