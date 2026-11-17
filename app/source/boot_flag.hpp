#pragma once

#include <string>

namespace app {

// Atmosphere starts the sysmodule at boot when this file exists. On desktop builds it is a file under the
// system temp dir, so runs never touch the repo.
std::string boot_flag_path();

// Whether the flag file exists.
bool boot_flag_enabled(const std::string& path);

// On: creates the parent flags dir if missing and an empty file. Off: removes the file. True on success,
// also when the flag is already in the wanted state.
bool set_boot_flag(const std::string& path, bool on);

}  // namespace app
