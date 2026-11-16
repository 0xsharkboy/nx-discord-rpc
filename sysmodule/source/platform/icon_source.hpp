#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace drpc {

// Copies the title's NACP icon; nullopt when unavailable, not a JPEG (FF D8 FF) or over 131072 bytes.
std::optional<std::vector<uint8_t>> load_icon(uint64_t title_id);

}  // namespace drpc
