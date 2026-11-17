#include "core/nro_swap.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>

namespace drpc::nro_swap {

namespace {

constexpr size_t kMaxHeartbeatBytes = 32;

bool exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

}  // namespace

std::string format_heartbeat(int64_t now_ms) {
    // Fixed width, so a rewrite in place never shortens the file.
    char buf[24];
    std::snprintf(buf, sizeof buf, "%019lld\n", static_cast<long long>(now_ms < 0 ? 0 : now_ms));
    return buf;
}

std::optional<int64_t> parse_heartbeat(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    if (text.empty() || text.size() > 19) return std::nullopt;
    int64_t v = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        if (v > (INT64_MAX - (c - '0')) / 10) return std::nullopt;
        v = v * 10 + (c - '0');
    }
    return v;
}

bool app_open(std::optional<int64_t> stamp_ms, int64_t now_ms) {
    return stamp_ms && *stamp_ms <= now_ms && now_ms - *stamp_ms < kHeartbeatStaleMs;
}

Action decide(bool staged_exists, std::optional<int64_t> stamp_ms, int64_t now_ms) {
    if (!staged_exists) return Action::None;
    return app_open(stamp_ms, now_ms) ? Action::Wait : Action::Swap;
}

SwapResult swap(const std::string& target, const std::string& staged) {
    const std::string old = target + ".old";
    std::remove(old.c_str());
    const bool had = exists(target);
    if (had && std::rename(target.c_str(), old.c_str()) != 0) return SwapResult::Locked;
    if (std::rename(staged.c_str(), target.c_str()) != 0) {
        if (had) std::rename(old.c_str(), target.c_str());
        return SwapResult::Failed;
    }
    std::remove(old.c_str());
    return SwapResult::Swapped;
}

std::optional<int64_t> read_heartbeat(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    char buf[kMaxHeartbeatBytes];
    const size_t n = std::fread(buf, 1, sizeof buf, f);
    std::fclose(f);
    return parse_heartbeat(std::string_view(buf, n));
}

}  // namespace drpc::nro_swap
