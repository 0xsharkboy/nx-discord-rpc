#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace drpc {

inline constexpr std::string_view kAvatarCdnHost = "cdn.discordapp.com";

// Decimal digits only, at most 2^64 - 1.
bool valid_discord_id(std::string_view id);
// 32 lower-case hex digits, optionally prefixed with "a_" (animated).
bool valid_avatar_hash(std::string_view hash);
// Path on kAvatarCdnHost. An empty avatar selects the default avatar. Nullopt when the id is
// invalid or a non-empty avatar is invalid.
std::optional<std::string> avatar_cdn_path(std::string_view discord_id, std::string_view avatar);

}  // namespace drpc
