#include "platform/icon_source.hpp"

#include <switch.h>

#include <cstdlib>
#include <span>

namespace drpc {
namespace {

constexpr size_t kMaxIconBytes = 131072;

// Reads the title's control data and calls use() with its icon when it is one load_icon would return.
template <typename F>
bool with_icon(uint64_t title_id, F&& use) {
    // About 144 KB (NACP + icon): heap-allocated only for the duration of the call.
    auto* data = static_cast<NsApplicationControlData*>(std::malloc(sizeof(NsApplicationControlData)));
    if (!data) return false;

    bool found = false;
    u64 size = 0;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, data, sizeof *data, &size)) &&
        size > sizeof(NacpStruct)) {
        const size_t icon_size = size_t(size) - sizeof(NacpStruct);
        const uint8_t* p = data->icon;
        if (icon_size >= 3 && icon_size <= kMaxIconBytes && icon_size <= sizeof data->icon &&
            p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) {
            use(std::span<const uint8_t>{p, icon_size});
            found = true;
        }
    }
    std::free(data);
    return found;
}

}  // namespace

std::optional<std::vector<uint8_t>> load_icon(uint64_t title_id) {
    std::optional<std::vector<uint8_t>> icon;
    with_icon(title_id, [&](std::span<const uint8_t> bytes) { icon.emplace(bytes.begin(), bytes.end()); });
    return icon;
}

std::optional<std::string> icon_sha256(uint64_t title_id) {
    // Hashed in place: no copy of the icon next to the control data.
    u8 digest[SHA256_HASH_SIZE];
    if (!with_icon(title_id, [&](std::span<const uint8_t> bytes) { sha256CalculateHash(digest, bytes.data(), bytes.size()); }))
        return std::nullopt;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex(2 * SHA256_HASH_SIZE, '0');
    for (size_t i = 0; i < SHA256_HASH_SIZE; ++i) {
        hex[2 * i] = kHex[digest[i] >> 4];
        hex[2 * i + 1] = kHex[digest[i] & 0x0F];
    }
    return hex;
}

}  // namespace drpc
