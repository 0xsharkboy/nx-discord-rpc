#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace drpc {

// Incremental SHA-256. On the console it wraps libnx's hardware-accelerated context; elsewhere a portable
// implementation (FIPS 180-4) stands in, so host tests and the desktop build hash the same way.
class Sha256 {
public:
    using Digest = std::array<uint8_t, 32>;

    Sha256();
    void update(std::span<const uint8_t> data);
    // Ends the hash; the object must not be updated afterwards.
    Digest finish();

    static Digest of(std::span<const uint8_t> data);

private:
#ifdef __SWITCH__
    Sha256Context ctx_{};
#else
    void block(const uint8_t* p);

    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buf_{};
    size_t used_ = 0;
    uint64_t total_ = 0;
#endif
};

// Lowercase hex, 64 characters.
std::string to_hex(const Sha256::Digest& digest);

}  // namespace drpc
