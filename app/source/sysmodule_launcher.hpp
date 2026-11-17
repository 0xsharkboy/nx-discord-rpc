#pragma once

#include <cstdint>

namespace app {

enum class LaunchStatus { Started, AlreadyRunning, Failed };

struct LaunchResult {
    LaunchStatus status = LaunchStatus::Failed;
    uint32_t code = 0;  // the pm result on Switch, 0 otherwise
};

// Asks pm to start the sysmodule for this session. The boot flag is left alone. Desktop builds cannot start
// anything and always fail.
LaunchResult launch_sysmodule();

}  // namespace app
