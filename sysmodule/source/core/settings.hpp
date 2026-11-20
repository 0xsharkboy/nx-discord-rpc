#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "drpc/protocol.hpp"

namespace drpc {

inline constexpr std::string_view kDefaultRelayUrl = "https://nx-discord-rpc.0xsharkboy.dev";

struct Settings {
    bool enabled = true, private_mode = false,
         show_paused = true, show_firmware = false, show_home_menu = false, debug_log = false,
         show_docked = false, show_playtime = false, show_badge = true,
         english_game_names = false;  // send the NACP English name instead of the console-language one
    std::string relay_url{kDefaultRelayUrl};
    std::string api_key, admin_key;
    std::string username;
    std::string discord_id, avatar;
    // Private or Hidden titles only, at most ipc::kMaxHiddenTitles, each title once.
    std::vector<std::pair<uint64_t, TitleVisibility>> hidden_titles;
    bool operator==(const Settings&) const = default;
};

Settings parse_settings(std::string_view ini);
std::string serialize_settings(const Settings& s);
// Exactly 43 characters of [A-Za-z0-9_-].
std::optional<std::string> normalize_api_key(std::string_view raw);
// At most 32 bytes kept; empty if it holds a control character.
std::string normalize_username(std::string_view raw);
// 16 to 128 printable ASCII characters, no space.
std::optional<std::string> normalize_admin_key(std::string_view raw);
// Replaces every occurrence of each non-empty secret with "[secret]".
std::string redact(std::string_view text, std::span<const std::string_view> secrets);
// "0100000000D15C0D": 16 upper-case hex digits.
std::string format_title_id(uint64_t title_id);
// The visibility hidden_titles gives a title, Shown when it is not listed.
TitleVisibility visibility_of(const Settings& s, uint64_t title_id);

}  // namespace drpc
