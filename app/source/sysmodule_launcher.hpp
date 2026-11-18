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

// For the updater: terminates the sysmodule through pm so its exefs.nsp can be replaced, then (restart) launches it
// again, retrying while pm still tracks the old process. Both block briefly; call them off the UI thread. Desktop
// builds do nothing.
void stop_sysmodule();
LaunchResult restart_sysmodule();

}  // namespace app
