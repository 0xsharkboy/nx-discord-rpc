#include "boot_flag.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>

#ifndef __SWITCH__
#include <filesystem>
#include <system_error>
#endif

namespace app {

namespace {

// Plain POSIX calls, like the avatar cache: they behave the same on the SD card (fsdev) and on desktop.
bool is_file(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string parent_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

std::string boot_flag_path() {
#ifdef __SWITCH__
    return "sdmc:/atmosphere/contents/4200000000D15C0D/flags/boot2.flag";
#else
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    return (temp / "nx-discord-rpc-flags" / "boot2.flag").string();
#endif
}

bool boot_flag_enabled(const std::string& path) { return !path.empty() && is_file(path); }

bool set_boot_flag(const std::string& path, bool on) {
    if (path.empty()) return false;
    if (!on) {
        if (std::remove(path.c_str()) == 0) return true;
        struct stat st {};
        return stat(path.c_str(), &st) != 0 && (errno == ENOENT || errno == ENOTDIR);
    }
    if (is_file(path)) return true;
    const std::string dir = parent_of(path);
    if (!dir.empty()) mkdir(dir.c_str(), 0777);  // only the flags dir; it may already exist
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    return std::fclose(f) == 0 && is_file(path);
}

}  // namespace app
