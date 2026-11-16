#include "drpc/tls_transport.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "drpc/net_log.hpp"

namespace drpc {
namespace {

constexpr int64_t kConnectTimeoutMs = 10'000;
constexpr int64_t kHandshakeTimeoutMs = 15'000;
constexpr int64_t kWriteTimeoutMs = 10'000;

// ssl module (123), description 204: the operation would block in SslIoMode_NonBlocking.
// Not named in ssl.h; same value devkitPro curl's libnx backend compares against.
constexpr Result kSslWouldBlock = MAKERESULT(123, 204);

// Monotonic milliseconds since boot.
int64_t mono_ms() {
    return int64_t(armTicksToNs(armGetSystemTick()) / 1'000'000);
}

// Waits until fd has one of the events or the deadline passes; false on timeout or error.
bool wait_fd(int fd, short events, int64_t deadline) {
    const int64_t left = deadline - mono_ms();
    if (left <= 0) return false;
    pollfd p{fd, events, 0};
    if (poll(&p, 1, int(left)) <= 0) return false;
    // A partial TLS record keeps the socket readable while ssl still would block; avoid spinning.
    svcSleepThread(1'000'000);
    return true;
}

int64_t capped(int64_t deadline, std::optional<int64_t> overall) {
    return overall ? std::min(deadline, *overall) : deadline;
}

int open_socket(const std::string& host, uint16_t port, std::optional<int64_t> overall_deadline) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        net_log("DNS lookup failed for " + host);
        return -1;
    }
    if (overall_deadline && mono_ms() >= *overall_deadline) {
        freeaddrinfo(res);
        net_log("DNS lookup for " + host + " took too long");
        return -1;
    }
    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0) {
            int rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
            const int64_t deadline = capped(mono_ms() + kConnectTimeoutMs, overall_deadline);
            if (rc != 0 && errno == EINPROGRESS && wait_fd(fd, POLLOUT, deadline)) {
                int err = 0;
                socklen_t len = sizeof err;
                rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0 ? 0 : -1;
            }
            if (rc == 0) break;
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) net_log("TCP connect failed to " + host);
    return fd;
}

void log_failure(const char* what, Result rc) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "TLS %s failed: 0x%x", what, rc);
    net_log(buf);
}

}  // namespace

bool TlsTransport::connect(std::string_view host_view, uint16_t port) {
    close();
    if (host_view.empty() || host_view.size() > 0xff) return false;
    const std::string host{host_view};

    fd_ = open_socket(host, port, overall_deadline_);
    if (fd_ < 0) return false;

    Result rc = sslCreateContext(&context_, SslVersion_Auto);
    if (R_FAILED(rc)) {
        log_failure("context", rc);
        close();
        return false;
    }
    has_context_ = true;

    rc = sslContextCreateConnection(&context_, &connection_);
    if (R_FAILED(rc)) {
        log_failure("connection", rc);
        close();
        return false;
    }
    has_connection_ = true;

    // The socket stays ours so we can poll() it while ssl calls would block.
    rc = sslConnectionSetOption(&connection_, SslOptionType_DoNotCloseSocket, true);
    if (R_FAILED(rc)) {
        log_failure("option", rc);
        close();
        return false;
    }
    errno = 0;
    const int ssl_fd = socketSslConnectionSetSocketDescriptor(&connection_, fd_);
    if (ssl_fd >= 0) {
        ::close(ssl_fd);
    } else if (errno != ENOENT) {  // ENOENT: no descriptor handed back, expected with DoNotCloseSocket
        net_log("TLS socket descriptor failed");
        close();
        return false;
    }

    rc = sslConnectionSetHostName(&connection_, host.c_str(), u32(host.size()));
    if (R_SUCCEEDED(rc)) rc = sslConnectionSetIoMode(&connection_, SslIoMode_NonBlocking);
    if (R_FAILED(rc)) {
        log_failure("setup", rc);
        close();
        return false;
    }

    const int64_t deadline = cap(mono_ms() + kHandshakeTimeoutMs);
    while ((rc = sslConnectionDoHandshake(&connection_, nullptr, nullptr, nullptr, 0)) == kSslWouldBlock) {
        if (!wait_fd(fd_, POLLIN | POLLOUT, deadline)) break;
    }
    if (R_FAILED(rc)) {
        log_failure(rc == kSslWouldBlock ? "handshake timeout" : "handshake", rc);
        close();
        return false;
    }
    return true;
}

int TlsTransport::read(std::span<uint8_t> buf, int timeout_ms) {
    if (!has_connection_ || buf.empty()) return -1;

    // A readable socket may hold only part of a TLS record, so keep retrying until the deadline.
    const int64_t deadline = cap(mono_ms() + timeout_ms);
    for (;;) {
        u32 got = 0;
        const Result rc = sslConnectionRead(&connection_, buf.data(), u32(buf.size()), &got);
        if (rc == kSslWouldBlock) {
            if (!wait_fd(fd_, POLLIN, deadline)) return 0;
            continue;
        }
        if (R_FAILED(rc) || got == 0) return -1;
        return int(got);
    }
}

bool TlsTransport::write_all(std::span<const uint8_t> data) {
    if (!has_connection_) return false;
    const int64_t deadline = cap(mono_ms() + kWriteTimeoutMs);
    while (!data.empty()) {
        u32 sent = 0;
        const Result rc = sslConnectionWrite(&connection_, data.data(), u32(data.size()), &sent);
        if (rc == kSslWouldBlock) {
            if (!wait_fd(fd_, POLLOUT, deadline)) return false;
            continue;
        }
        if (R_FAILED(rc) || sent == 0) return false;
        data = data.subspan(sent);
    }
    return true;
}

void TlsTransport::set_time_budget(std::optional<int64_t> budget_ms) {
    overall_deadline_.reset();
    if (budget_ms) overall_deadline_ = mono_ms() + *budget_ms;
}

int64_t TlsTransport::cap(int64_t deadline) const {
    return capped(deadline, overall_deadline_);
}

void TlsTransport::close() {
    if (has_connection_) sslConnectionClose(&connection_);
    if (has_context_) sslContextClose(&context_);
    if (fd_ >= 0) ::close(fd_);
    has_connection_ = has_context_ = false;
    fd_ = -1;
}

}  // namespace drpc
