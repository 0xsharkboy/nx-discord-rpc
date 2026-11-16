#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app {

inline constexpr size_t kAvatarMaxBytes = 262144;

// "<discord_id>:<avatar hash>", or "<discord_id>:default" for the default avatar.
std::string avatar_cache_key(std::string_view discord_id, std::string_view avatar);

// True when the bytes start with the 8-byte PNG signature.
bool is_png(std::span<const uint8_t> data);

// One cached picture: <dir>/avatar.png, and <dir>/avatar.key naming whose picture it is.
class AvatarCache {
public:
    explicit AvatarCache(std::string dir);

    // The picture stored under key, if it is a PNG.
    std::optional<std::vector<uint8_t>> load(std::string_view key) const;
    // Replaces the cached picture. Creates dir (one level) when missing.
    bool store(std::string_view key, std::span<const uint8_t> png);
    void clear();

private:
    std::string dir_;
};

}  // namespace app
