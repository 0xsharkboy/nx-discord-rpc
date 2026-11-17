#include "sysmodule_launcher.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace app {

#ifdef __SWITCH__

namespace {

constexpr u64 kProgramId = 0x4200000000D15C0D;

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

#else

LaunchResult launch_sysmodule() { return {}; }

#endif

}  // namespace app
