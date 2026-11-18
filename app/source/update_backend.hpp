#pragma once

#include <memory>

#include "updater.hpp"

namespace app {

// Where the updater's network, SD card and sysmodule work happens. Both calls block: the UpdateManager runs
// them on its own thread.
class UpdateBackend {
public:
    virtual ~UpdateBackend() = default;
    virtual update::CheckResult check() = 0;
    virtual update::InstallResult install(const update::Release& release, const update::InstallHooks& hooks) = 0;
};

// The console: GitHub over verified TLS, the SD card, pm. Desktop: a fake chosen by NXDRPC_FAKE_UPDATE
// ("uptodate", "available", "manual", "checkfail", "installfail", "partial", "slow"); unset, the check fails
// quietly, as with no network.
std::unique_ptr<UpdateBackend> make_update_backend();

}  // namespace app
