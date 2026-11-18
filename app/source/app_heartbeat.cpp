#include "app_heartbeat.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>

#include "core/nro_swap.hpp"

#ifdef __SWITCH__
#include <switch.h>
#else
#include <chrono>
#include <filesystem>
#include <system_error>
#endif

namespace app {

namespace {

// The same counter the sysmodule reads: milliseconds since boot.
int64_t now_ms() {
#ifdef __SWITCH__
    return int64_t(armTicksToNs(armGetSystemTick()) / 1'000'000);
#else
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}

std::string heartbeat_path() {
#ifdef __SWITCH__
    return std::string("sdmc:") + drpc::nro_swap::kHeartbeat;
#else
    // Desktop builds stand the SD card's config dir in under the system temp dir.
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec) return {};
    return (temp / "nx-discord-rpc-config" / "app.running").string();
#endif
}

}  // namespace

Heartbeat::Heartbeat() : path_(heartbeat_path()) {
    if (const size_t slash = path_.find_last_of('/'); slash != std::string::npos)
        mkdir(path_.substr(0, slash).c_str(), 0777);
}

void Heartbeat::beat() {
    if (path_.empty()) return;
    // Rewritten in place (same length every time): the file is never seen empty, which would read as closed.
    std::FILE* f = std::fopen(path_.c_str(), "r+b");
    if (!f) f = std::fopen(path_.c_str(), "wb");
    if (!f) return;
    const std::string text = drpc::nro_swap::format_heartbeat(now_ms());
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

void Heartbeat::stop() {
    if (!path_.empty()) std::remove(path_.c_str());
}

}  // namespace app
