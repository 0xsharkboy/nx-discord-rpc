#include "presentation.hpp"

#include <cstdio>

namespace app {

FooterLine footer_line(const drpc::ipc::Status& status) {
    using drpc::ipc::LinkState;
    using drpc::ipc::RelayHealth;
    if (status.health == RelayHealth::NoUrl) return {Footer::NoUrl, Tone::Bad};
    if (status.health == RelayHealth::Offline) return {Footer::Offline, Tone::Warning};
    switch (status.link) {
        case LinkState::NoKey:
            return {Footer::NoKey, Tone::Neutral};
        case LinkState::LinkBroken:
            return {Footer::Broken, Tone::Bad};
        case LinkState::Unauthorized:
            return {Footer::Unauthorized, Tone::Bad};
        case LinkState::Linked:
            break;
    }
    if (status.health == RelayHealth::Backoff) return {Footer::Backoff, Tone::Warning};
    if (status.username[0] == '\0') return {Footer::OkUnnamed, Tone::Good};
    return {Footer::Ok, Tone::Good};
}

std::string format_elapsed(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const long long h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    char buffer[32];
    if (h > 0)
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buffer, sizeof buffer, "%02lld:%02lld", m, s);
    return buffer;
}

std::string link_countdown(uint32_t seconds) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%u:%02u", static_cast<unsigned>(seconds / 60),
                  static_cast<unsigned>(seconds % 60));
    return buffer;
}

uint32_t retry_minutes(uint32_t seconds) {
    const uint32_t minutes = seconds / 60 + (seconds % 60 != 0 ? 1 : 0);
    return minutes == 0 ? 1 : minutes;
}

}  // namespace app
