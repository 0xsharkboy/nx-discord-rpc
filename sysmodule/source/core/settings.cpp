#include "core/settings.hpp"

#include <cstdio>

namespace drpc {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

void parse_bool(std::string_view v, bool& out) {
    if (v == "true" || v == "1") out = true;
    else if (v == "false" || v == "0") out = false;
}

std::string_view strip_bom(std::string_view s) {
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    return s.substr(0, kBom.size()) == kBom ? s.substr(kBom.size()) : s;
}

}  // namespace

Settings parse_settings(std::string_view ini) {
    Settings s;
    ini = strip_bom(ini);
    while (!ini.empty()) {
        const size_t nl = ini.find('\n');
        std::string_view line = trim(ini.substr(0, nl));
        ini = nl == std::string_view::npos ? std::string_view{} : ini.substr(nl + 1);

        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        if (key == "enabled") parse_bool(value, s.enabled);
        else if (key == "private_mode") parse_bool(value, s.private_mode);
        else if (key == "show_elapsed") parse_bool(value, s.show_elapsed);
        else if (key == "show_firmware") parse_bool(value, s.show_firmware);
        else if (key == "show_home_menu") parse_bool(value, s.show_home_menu);
        else if (key == "debug_log") parse_bool(value, s.debug_log);
    }
    return s;
}

std::string serialize_settings(const Settings& s) {
    const auto b = [](bool v) { return v ? "true" : "false"; };
    std::string out;
    out += std::string("enabled = ") + b(s.enabled) + "\n";
    out += std::string("private_mode = ") + b(s.private_mode) + "\n";
    out += std::string("show_elapsed = ") + b(s.show_elapsed) + "\n";
    out += std::string("show_firmware = ") + b(s.show_firmware) + "\n";
    out += std::string("show_home_menu = ") + b(s.show_home_menu) + "\n";
    out += std::string("debug_log = ") + b(s.debug_log) + "\n";
    return out;
}

std::string redact(std::string_view text, std::string_view secret) {
    if (secret.empty()) return std::string{text};
    std::string out;
    size_t pos = 0;
    for (;;) {
        const size_t hit = text.find(secret, pos);
        if (hit == std::string_view::npos) break;
        out.append(text.substr(pos, hit - pos));
        out += "[redacted]";
        pos = hit + secret.size();
    }
    out.append(text.substr(pos));
    return out;
}

std::string format_title_id(uint64_t title_id) {
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llX", static_cast<unsigned long long>(title_id));
    return hex;
}

}  // namespace drpc
