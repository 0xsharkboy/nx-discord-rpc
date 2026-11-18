#pragma once

#include <string>
#include <string_view>

namespace app {

// The app's own settings, in sdmc:/config/nx-discord-rpc/app.ini (not the sysmodule's config.ini).
struct AppSettings {
    bool check_updates = true;
};

// key=value lines; unknown keys, comments ('#' or ';') and bad values are ignored.
AppSettings parse_app_settings(std::string_view ini);
std::string format_app_settings(const AppSettings& settings);

// On desktop builds, a file under the system temp dir.
std::string app_settings_path();
AppSettings load_app_settings(const std::string& path);
// Writes a temp file then renames it over path; creates the parent dir if missing. True on success.
bool save_app_settings(const std::string& path, const AppSettings& settings);

}  // namespace app
