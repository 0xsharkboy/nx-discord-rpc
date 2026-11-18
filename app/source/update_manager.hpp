#pragma once

#include <borealis/core/event.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "update_backend.hpp"
#include "updater.hpp"

namespace drpc {
class Client;
}

namespace app {

enum class UpdatePhase {
    Idle,         // nothing checked yet, or a quiet check failed
    Checking,
    UpToDate,
    Available,    // a newer release the app can install
    ManualOnly,   // a newer release without the single files (v0.4.0 and older): update from GitHub
    CheckFailed,  // only after a manual check
    Downloading,
    Done,         // installed (see UpdateState::start_failed); the app must be closed for its .nro to be swapped
    Ready,        // at start: an update is staged already (the app was reopened before the swap); no quiet check
    Failed,       // download or hash failed: nothing changed
    Partial,      // a replace failed after others succeeded
};

struct UpdateState {
    UpdatePhase phase = UpdatePhase::Idle;
    std::string tag;  // the newer version, when one was found
    uint64_t done = 0, total = 0;
    bool manual = false;  // the last check came from Settings > Check for updates now
    bool start_failed = false;  // Done or Partial, but the sysmodule did not start again
    bool staged = false;   // a verified update waits for the app to close (kept across a manual check from Ready)
    bool checked = false;  // Idle after a quiet check that found nothing newer

    // 0 to 100.
    int percent() const;
};

// Owns the update check and install for the app's lifetime (tabs come and go). The work runs on a thread of
// its own, not brls::async, whose single worker also serves the hidden games loader and the connection test.
// State changes are applied and announced on the UI thread.
class UpdateManager {
public:
    UpdateManager(drpc::Client& client, std::unique_ptr<UpdateBackend> backend);
    ~UpdateManager();
    UpdateManager(const UpdateManager&) = delete;
    UpdateManager& operator=(const UpdateManager&) = delete;

    // Once, at start: Ready when an update is staged already, else the quiet check when Check for updates is on.
    void start(bool check_updates);
    // A quiet check (at start) shows nothing on failure; a manual one reports it.
    void check(bool manual);
    void install();
    bool busy() const { return state_.phase == UpdatePhase::Checking || state_.phase == UpdatePhase::Downloading; }

    const UpdateState& state() const { return state_; }
    brls::Event<>& changed() { return changed_; }

private:
    void run(std::function<void()> work);
    void set(UpdateState state);
    void reconnect(std::weak_ptr<bool> alive, int attempt);

    drpc::Client& client_;
    std::shared_ptr<UpdateBackend> backend_;
    UpdateState state_;
    update::Release release_;
    brls::Event<> changed_;
    std::thread worker_;
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace app
