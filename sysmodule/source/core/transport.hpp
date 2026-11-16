#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace drpc {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool connect(std::string_view host, uint16_t port) = 0;
    virtual int read(std::span<uint8_t> buf, int timeout_ms) = 0;  // >0 bytes, 0 timeout, <0 closed/error
    virtual bool write_all(std::span<const uint8_t> data) = 0;
    virtual void close() = 0;
};

}  // namespace drpc
