#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace drpc {

// Copies the title's NACP icon; nullopt when unavailable, not a JPEG (FF D8 FF) or over 131072 bytes.
std::optional<std::vector<uint8_t>> load_icon(uint64_t title_id);

// SHA-256 of the exact bytes load_icon returns, 64 lowercase hex characters; nullopt when load_icon has none.
std::optional<std::string> icon_sha256(uint64_t title_id);

}  // namespace drpc
