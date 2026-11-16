#include "drpc/discord_ids.hpp"

#include <cstdint>

namespace drpc {

namespace {

bool parse_u64(std::string_view s, uint64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

}  // namespace

bool valid_discord_id(std::string_view id) {
    uint64_t v;
    return parse_u64(id, v);
}

bool valid_avatar_hash(std::string_view hash) {
    if (hash.substr(0, 2) == "a_") hash.remove_prefix(2);
    if (hash.size() != 32) return false;
    for (const char c : hash) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::optional<std::string> avatar_cdn_path(std::string_view discord_id, std::string_view avatar) {
    uint64_t id;
    if (!parse_u64(discord_id, id)) return std::nullopt;
    if (avatar.empty()) return "/embed/avatars/" + std::to_string((id >> 22) % 6) + ".png";
    if (!valid_avatar_hash(avatar)) return std::nullopt;
    return "/avatars/" + std::string{discord_id} + "/" + std::string{avatar} + ".png?size=128";
}

}  // namespace drpc
