#include "update_backend.hpp"

#include <string>

#include "sysmodule_launcher.hpp"

#ifdef __SWITCH__
#include "drpc/tls_transport.hpp"
#else
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#endif

namespace app {

namespace {

#ifdef __SWITCH__

// Whole-request budgets: the API answer is small; the nro is about 2.7 MB.
constexpr int64_t kApiBudgetMs = 20'000;
constexpr int64_t kFileBudgetMs = 180'000;

class SwitchBackend final : public UpdateBackend {
public:
    SwitchBackend() {
        config_.user_agent = std::string("nx-discord-rpc/") + APP_VERSION_STRING;
        config_.root = "sdmc:";
    }

    update::CheckResult check() override { return update::check(fetch(kApiBudgetMs), config_, APP_VERSION_STRING); }

    update::InstallResult install(const update::Release& release, const update::InstallHooks& hooks) override {
        update::InstallHooks h = hooks;
        h.stop_sysmodule = [] { stop_sysmodule(); };
        h.start_sysmodule = [] { restart_sysmodule(); };
        return update::install(fetch(kFileBudgetMs), config_, release, h);
    }

private:
    static update::Fetch fetch(int64_t budget_ms) {
        return [budget_ms](const std::string& url, const http::Options& options, const http::Sink& sink) {
            // Certificate and host name verification stay on (TlsTransport never turns them off).
            drpc::TlsTransport tls;
            tls.set_time_budget(budget_ms);
            return http::get(tls, url, options, sink);
        };
    }

    update::Config config_;
};

#else

// Stands in for GitHub on desktop builds, for screenshots and trying the screens.
class FakeBackend final : public UpdateBackend {
public:
    explicit FakeBackend(std::string scenario) : scenario_(std::move(scenario)) {}

    update::CheckResult check() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        update::CheckResult r;
        if (scenario_ == "uptodate") {
            r.outcome = update::CheckOutcome::UpToDate;
        } else if (scenario_ == "manual") {
            r.outcome = update::CheckOutcome::ManualOnly;
            r.release.tag = "v0.6.0";
        } else if (scenario_ == "available" || scenario_ == "installfail" || scenario_ == "partial" ||
                   scenario_ == "slow") {
            r.outcome = update::CheckOutcome::Available;
            r.release.tag = "v0.6.0";
            r.release.assets["nx-discord-rpc.nro"].size = 2'713'430;
            r.release.assets["exefs.nsp"].size = 251'797;
            r.release.assets["nx-discord-rpc.ovl"].size = 294'968;
            r.release.assets["toolbox.json"].size = 94;
        }
        return r;
    }

    update::InstallResult install(const update::Release& release, const update::InstallHooks& hooks) override {
        const uint64_t total = release.total_size();
        // "slow" stops at 42% until the app closes; "installfail" fails at 60%.
        const uint64_t stop_at = scenario_ == "slow" ? total * 42 / 100 : scenario_ == "installfail" ? total * 6 / 10 : total;
        for (uint64_t done = 0;; done = std::min(stop_at, done + total / 25)) {
            if (hooks.cancelled && hooks.cancelled()) return {update::InstallOutcome::Failed, "cancelled"};
            if (hooks.progress) hooks.progress(done, total);
            if (done >= stop_at && scenario_ != "slow") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
        if (scenario_ == "installfail") return {update::InstallOutcome::Failed, "fake failure"};
        if (scenario_ == "partial") return {update::InstallOutcome::Partial, "fake partial"};
        return {update::InstallOutcome::Done, "fake install"};
    }

private:
    std::string scenario_;
};

#endif

}  // namespace

std::unique_ptr<UpdateBackend> make_update_backend() {
#ifdef __SWITCH__
    return std::make_unique<SwitchBackend>();
#else
    const char* scenario = std::getenv("NXDRPC_FAKE_UPDATE");
    return std::make_unique<FakeBackend>(scenario ? scenario : "");
#endif
}

}  // namespace app
