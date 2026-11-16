#include "platform/log.hpp"

#include <switch.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>

#include "core/settings.hpp"
#include "platform/clock.hpp"
#include "platform/paths.hpp"

namespace drpc::log {
namespace {

constexpr long kMaxLogBytes = 256 * 1024;

std::string g_api_key, g_admin_key;
bool g_debug = false;

void rotate_if_full() {
    struct stat st;
    if (stat(paths::kLog, &st) != 0 || st.st_size < kMaxLogBytes) return;
    std::remove(paths::kLogOld);
    std::rename(paths::kLog, paths::kLogOld);
}

void write(std::string_view level, std::string_view msg) {
    rotate_if_full();
    // Opened per line: a sysmodule can be killed at any time and must not lose buffered lines.
    FILE* f = std::fopen(paths::kLog, "a");
    if (!f) {
        mkdir(paths::kConfigRoot, 0777);
        mkdir(paths::kConfigDir, 0777);
        if (!(f = std::fopen(paths::kLog, "a"))) return;
    }
    const uint64_t ms = uint64_t(mono_ms());
    const std::string_view secrets[] = {g_api_key, g_admin_key};
    const std::string line = redact(msg, secrets);
    std::fprintf(f, "[%llu.%03llu] %.*s %.*s\n", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000),
                 int(level.size()), level.data(), int(line.size()), line.data());
    std::fclose(f);
}

}  // namespace

void info(std::string_view msg) { write("INFO", msg); }
void warn(std::string_view msg) { write("WARN", msg); }
void error(std::string_view msg) { write("ERROR", msg); }
void debug(std::string_view msg) { if (g_debug) write("DEBUG", msg); }
void set_debug(bool enabled) { g_debug = enabled; }
void set_secrets(std::string_view api_key, std::string_view admin_key) {
    g_api_key = api_key;
    g_admin_key = admin_key;
}

}  // namespace drpc::log
