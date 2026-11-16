#pragma once

#include <switch.h>

#include <optional>

#include "drpc/transport.hpp"

namespace drpc {

// TLS through the console's ssl service; certificate and host name verification stay on.
class TlsTransport : public Transport {
public:
    ~TlsTransport() override { close(); }

    bool connect(std::string_view host, uint16_t port) override;
    int read(std::span<uint8_t> buf, int timeout_ms) override;
    bool write_all(std::span<const uint8_t> data) override;
    void close() override;

    // Caps every later call at one overall deadline budget_ms from now; nullopt removes the cap.
    // DNS cannot be interrupted, so a slow lookup is only detected after it returns.
    void set_time_budget(std::optional<int64_t> budget_ms);

private:
    int64_t cap(int64_t deadline) const;

    std::optional<int64_t> overall_deadline_;
    int fd_ = -1;
    bool has_context_ = false;
    bool has_connection_ = false;
    SslContext context_{};
    SslConnection connection_{};
};

}  // namespace drpc
