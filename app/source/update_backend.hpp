#pragma once

#include <functional>
#include <memory>

#include "updater.hpp"

namespace app {

// Where the updater's network, SD card and sysmodule work happens. check and install block: the
// UpdateManager runs them on its own thread.
class UpdateBackend {
public:
    virtual ~UpdateBackend() = default;
    // cancelled turns true when the app closes: the check then stops at its next read.
    virtual update::CheckResult check(const std::function<bool()>& cancelled) = 0;
    virtual update::InstallResult install(const update::Release& release, const update::InstallHooks& hooks) = 0;
    // A verified update of the app waits for the sysmodule to swap it in (see update::staged_ready).
    virtual bool staged_ready() = 0;
};

// The console: GitHub over verified TLS, the SD card, pm. Desktop: a fake chosen by NXDRPC_FAKE_UPDATE
// ("uptodate", "available", "manual", "checkfail", "installfail", "partial", "slow", "startfail" (installed, but
// the sysmodule did not start), "ready" (an update is staged already)); unset, the check fails
// quietly, as with no network.
std::unique_ptr<UpdateBackend> make_update_backend();

}  // namespace app
