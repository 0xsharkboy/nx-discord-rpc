#include "update_manager.hpp"

#include <borealis.hpp>

#include <utility>

#include "drpc/client.hpp"

namespace app {

namespace {

constexpr long kReconnectMs = 500;
constexpr int kReconnectAttempts = 24;

}  // namespace

int UpdateState::percent() const {
    if (total == 0) return 0;
    return int(std::min<uint64_t>(done, total) * 100 / total);
}

UpdateManager::UpdateManager(drpc::Client& client, std::unique_ptr<UpdateBackend> backend)
    : client_(client), backend_(std::move(backend)) {}

UpdateManager::~UpdateManager() {
    alive_.reset();
    // A download stops at its next chunk; a replace already under way finishes first.
    cancel_->store(true);
    if (worker_.joinable()) worker_.join();
}

void UpdateManager::run(std::function<void()> work) {
    if (worker_.joinable()) worker_.join();  // the previous run has posted its result already
    worker_ = std::thread(std::move(work));
}

void UpdateManager::set(UpdateState state) {
    state_ = std::move(state);
    changed_.fire();
}

void UpdateManager::start(bool check_updates) {
    if (state_.phase != UpdatePhase::Idle) return;
    switch (update::start_action(backend_->staged_ready(), check_updates)) {
        case update::StartAction::None:
            return;
        case update::StartAction::Check:
            check(false);
            return;
        case update::StartAction::Ready: {
            UpdateState s;
            s.phase = UpdatePhase::Ready;
            set(s);
            return;
        }
    }
}

void UpdateManager::check(bool manual) {
    if (state_.phase == UpdatePhase::Checking && manual && !state_.manual) {
        // Asked for while the quiet check at start runs: that one reports now.
        state_.manual = true;
        changed_.fire();
        return;
    }
    if (busy()) return;
    // A finished install wants a restart, not another check.
    if (state_.phase == UpdatePhase::Done) return;
    // A staged update is not offered again at start, but a manual check stays possible: should the swap never
    // happen (the sysmodule cannot start, the .nro stays locked), installing again is the way out.
    if (state_.phase == UpdatePhase::Ready && !manual) return;
    UpdateState next;
    next.phase = UpdatePhase::Checking;
    next.manual = manual;
    set(next);

    std::weak_ptr<bool> alive = alive_;
    std::shared_ptr<UpdateBackend> backend = backend_;
    std::shared_ptr<std::atomic<bool>> cancel = cancel_;
    run([this, alive, backend, cancel] {
        auto result = std::make_shared<update::CheckResult>(backend->check([cancel] { return cancel->load(); }));
        brls::sync([this, alive, result] {
            if (alive.expired()) return;
            const bool manual = state_.manual;
            UpdateState s;
            s.manual = manual;
            switch (result->outcome) {
                case update::CheckOutcome::UpToDate:
                    s.phase = manual ? UpdatePhase::UpToDate : UpdatePhase::Idle;
                    break;
                case update::CheckOutcome::Available:
                    s.phase = UpdatePhase::Available;
                    break;
                case update::CheckOutcome::ManualOnly:
                    s.phase = UpdatePhase::ManualOnly;
                    break;
                case update::CheckOutcome::Failed:
                    s.phase = manual ? UpdatePhase::CheckFailed : UpdatePhase::Idle;
                    break;
            }
            s.tag = result->release.tag;
            release_ = std::move(result->release);
            set(s);
        });
    });
}

// pm accepts the launch before the new sysmodule registers its service: poll like the Start now screen does
// (every 500 ms for up to 12 s). A protocol mismatch also means it runs; the user restarts the app anyway.
void UpdateManager::reconnect(std::weak_ptr<bool> alive, int attempt) {
    const drpc::ConnectError error = client_.connect();
    if (error != drpc::ConnectError::NotRunning || attempt >= kReconnectAttempts) return;
    brls::delay(kReconnectMs, [this, alive, attempt] {
        if (!alive.expired()) reconnect(alive, attempt + 1);
    });
}

void UpdateManager::install() {
    if (busy() || release_.tag.empty() ||
        (state_.phase != UpdatePhase::Available && state_.phase != UpdatePhase::Failed &&
         state_.phase != UpdatePhase::Partial))
        return;
    UpdateState next = state_;
    next.phase = UpdatePhase::Downloading;
    next.done = 0;
    next.total = release_.total_size();
    set(next);

    std::weak_ptr<bool> alive = alive_;
    std::shared_ptr<UpdateBackend> backend = backend_;
    std::shared_ptr<std::atomic<bool>> cancel = cancel_;
    const update::Release release = release_;
    run([this, alive, backend, cancel, release] {
        // Progress reaches the UI only when the percentage moves.
        auto shown = std::make_shared<int>(-1);
        update::InstallHooks hooks;
        hooks.cancelled = [cancel] { return cancel->load(); };
        hooks.progress = [this, alive, shown](uint64_t done, uint64_t total) {
            const int pct = total ? int(std::min(done, total) * 100 / total) : 0;
            if (pct == *shown) return;
            *shown = pct;
            brls::sync([this, alive, done, total] {
                if (alive.expired() || state_.phase != UpdatePhase::Downloading) return;
                UpdateState s = state_;
                s.done = done;
                s.total = total;
                set(s);
            });
        };
        const update::InstallResult result = backend->install(release, hooks);
        if (result.outcome == update::InstallOutcome::Done) brls::Logger::info("Update: {}", result.detail);
        else brls::Logger::error("Update failed: {}", result.detail);
        const update::InstallOutcome outcome = result.outcome;
        const bool started = result.sysmodule_started;
        brls::sync([this, alive, outcome, started] {
            if (alive.expired()) return;
            UpdateState s = state_;
            s.start_failed = !started;
            switch (outcome) {
                case update::InstallOutcome::Done:
                    s.phase = UpdatePhase::Done;
                    break;
                case update::InstallOutcome::Failed:
                    s.phase = UpdatePhase::Failed;
                    break;
                case update::InstallOutcome::Partial:
                    s.phase = UpdatePhase::Partial;
                    break;
            }
            set(s);
            // The sysmodule was stopped and launched again: talk to the new one once it serves.
            if (outcome != update::InstallOutcome::Failed) reconnect(alive, 1);
        });
    });
}

}  // namespace app
