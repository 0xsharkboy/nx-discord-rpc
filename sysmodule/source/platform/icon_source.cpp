#include "platform/icon_source.hpp"

#include <switch.h>

#include <cstdlib>

namespace drpc {
namespace {

constexpr size_t kMaxIconBytes = 131072;

}  // namespace

std::optional<std::vector<uint8_t>> load_icon(uint64_t title_id) {
    // About 144 KB (NACP + icon): heap-allocated only for the duration of the call.
    auto* data = static_cast<NsApplicationControlData*>(std::malloc(sizeof(NsApplicationControlData)));
    if (!data) return std::nullopt;

    std::optional<std::vector<uint8_t>> icon;
    u64 size = 0;
    if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, data, sizeof *data, &size)) &&
        size > sizeof(NacpStruct)) {
        const size_t icon_size = size_t(size) - sizeof(NacpStruct);
        const uint8_t* p = data->icon;
        if (icon_size >= 3 && icon_size <= kMaxIconBytes && icon_size <= sizeof data->icon &&
            p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF)
            icon.emplace(p, p + icon_size);
    }
    std::free(data);
    return icon;
}

}  // namespace drpc
