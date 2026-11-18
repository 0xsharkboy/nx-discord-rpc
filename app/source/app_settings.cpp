#include "app_settings.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <optional>

#ifndef __SWITCH__
#include <filesystem>
#include <system_error>
#endif

namespace app {

namespace {

constexpr size_t kMaxBytes = 4096;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::optional<bool> parse_bool(std::string_view v) {
    if (v == "true" || v == "1" || v == "on" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "off" || v == "no") return false;
    return std::nullopt;
}

}  // namespace

AppSettings parse_app_settings(std::string_view ini) {
    AppSettings s;
    while (!ini.empty()) {
        const size_t eol = ini.find('\n');
        const std::string_view line = trim(ini.substr(0, eol));
        ini.remove_prefix(eol == std::string_view::npos ? ini.size() : eol + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));
        if (key == "check_updates") {
            if (const auto b = parse_bool(value)) s.check_updates = *b;
        }
    }
    return s;
}

std::string format_app_settings(const AppSettings& settings) {
    std::string out = "# NX Discord RPC app settings\n";
    out += "check_updates=";
    out += settings.check_updates ? "true" : "false";
    out += "\n";
    return out;
}

std::string app_settings_path() {
#ifdef __SWITCH__
    return "sdmc:/config/nx-discord-rpc/app.ini";
#else
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    return (temp / "nx-discord-rpc-config" / "app.ini").string();
#endif
}

AppSettings load_app_settings(const std::string& path) {
    std::FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string ini(kMaxBytes, '\0');
    ini.resize(std::fread(ini.data(), 1, ini.size(), f));
    std::fclose(f);
    return parse_app_settings(ini);
}

bool save_app_settings(const std::string& path, const AppSettings& settings) {
    if (path.empty()) return false;
    if (const size_t slash = path.find_last_of('/'); slash != std::string::npos) {
        const std::string dir = path.substr(0, slash);
        // sdmc:/config, then sdmc:/config/nx-discord-rpc: each may already exist.
        if (const size_t up = dir.find_last_of('/'); up != std::string::npos) mkdir(dir.substr(0, up).c_str(), 0777);
        mkdir(dir.c_str(), 0777);
    }
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const std::string text = format_app_settings(settings);
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !written) {
        std::remove(tmp.c_str());
        return false;
    }
    // FAT cannot rename over a file.
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace app
