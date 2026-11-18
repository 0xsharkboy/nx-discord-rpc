#include "sysmodule_launcher.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace app {

#ifdef __SWITCH__

namespace {

constexpr u64 kProgramId = 0x4200000000D15C0D;
constexpr int kRestartAttempts = 15;
constexpr s64 kRestartWaitNs = 200'000'000;

}  // namespace

LaunchResult launch_sysmodule() {
    Result rc = pmshellInitialize();
    if (R_FAILED(rc)) return {false, rc};
    const NcmProgramLocation loc{.program_id = kProgramId, .storageID = NcmStorageId_None, .pad = {}};
    u64 pid = 0;
    rc = pmshellLaunchProgram(0, &loc, &pid);
    pmshellExit();
    if (R_FAILED(rc)) return {false, rc};
    return {true, 0};
}

void stop_sysmodule() {
    if (R_FAILED(pmshellInitialize())) return;
    // Fails when it is not running, which is fine.
    pmshellTerminateProgram(kProgramId);
    pmshellExit();
}

LaunchResult restart_sysmodule() {
    // pm forgets a terminated process shortly after it exits; until then a launch is refused.
    LaunchResult last;
    for (int attempt = 0; attempt < kRestartAttempts; ++attempt) {
        last = launch_sysmodule();
        if (last.ok) return last;
        svcSleepThread(kRestartWaitNs);
    }
    return last;
}

#else

LaunchResult launch_sysmodule() { return {}; }
void stop_sysmodule() {}
LaunchResult restart_sysmodule() { return {}; }

#endif

}  // namespace app
