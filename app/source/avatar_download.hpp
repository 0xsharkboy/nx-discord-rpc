#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace app {

// Fetches a PNG from the Discord CDN; path comes from drpc::avatar_cdn_path. Blocks for up to
// 8 seconds: call it off the UI thread. On desktop, reads the file named by NXDRPC_AVATAR_FILE.
std::optional<std::vector<uint8_t>> download_avatar(std::string_view path);

}  // namespace app
