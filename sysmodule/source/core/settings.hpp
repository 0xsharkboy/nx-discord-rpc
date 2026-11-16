#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace drpc {

struct Settings {
    bool enabled = true, private_mode = false, show_elapsed = true,
         show_firmware = false, show_home_menu = false, debug_log = false;
    bool operator==(const Settings&) const = default;
};

Settings parse_settings(std::string_view ini);
std::string serialize_settings(const Settings& s);
std::string redact(std::string_view text, std::string_view secret);
// "0100000000D15C0D": 16 upper-case hex digits.
std::string format_title_id(uint64_t title_id);

}  // namespace drpc
