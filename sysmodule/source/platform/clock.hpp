#pragma once

#include <switch.h>

#include <cstdint>

namespace drpc {

// Monotonic milliseconds since boot.
inline int64_t mono_ms() {
    return int64_t(armTicksToNs(armGetSystemTick()) / 1'000'000);
}

// Wall clock in Unix milliseconds; 0 when no clock is available.
// The network clock is synced with Nintendo's servers; the user clock can be set to anything.
inline int64_t unix_ms() {
    u64 seconds = 0;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_NetworkSystemClock, &seconds)) ||
        R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &seconds)))
        return int64_t(seconds) * 1000;
    return 0;
}

}  // namespace drpc
