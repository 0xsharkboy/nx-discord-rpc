#pragma once

#include <cstdint>

namespace app {

struct LaunchResult {
    bool ok = false;
    uint32_t code = 0;  // the pm result on Switch, 0 otherwise
};

// Asks pm to start the sysmodule for this session. The boot flag is left alone. A failure may still mean it
// is running (for example already started), so callers check by connecting. Desktop builds cannot start
// anything and always fail.
LaunchResult launch_sysmodule();

}  // namespace app
