#pragma once

#include <switch.h>

#include <atomic>
#include <cstdint>
#include <optional>

namespace drpc {

// Network reachability (nifm) and sleep notifications (psc). Without a psc
// module the sysmodule keeps running, it just does not close cleanly on sleep.
class PowerNet {
public:
    // What happened around the last sleep, reported once the console is awake.
    struct WakeReport {
        int64_t ack_ms;
        bool closed_in_time;
    };

    PowerNet();
    ~PowerNet();
    PowerNet(const PowerNet&) = delete;
    PowerNet& operator=(const PowerNet&) = delete;

    // Internet reachable and the console not on its way to sleep.
    bool online();
    // True while the console is going to sleep, asleep or shutting down: no SD, ns or network access.
    bool asleep() const { return asleep_; }
    // Non-blocking. True once per sleep or shutdown request: the caller closes its
    // connections, then calls connection_closed(). The acknowledgement does not wait longer than 2 s.
    bool take_close_request();
    void connection_closed();
    // Set once after each wake.
    std::optional<WakeReport> take_wake_report();
    // Result code of a failed psc call since the last call, 0 if none.
    Result take_error() { return error_.exchange(0); }
    // True once, after the psc thread gave up and released its module: sleep handling is off.
    bool take_disabled() {
        if (!released_ || disabled_reported_) return false;
        disabled_reported_ = true;
        return true;
    }

private:
    static void psc_thread(void* self);
    void handle_requests();
    void handle(PscPmState state);

    PscPmModule module_{};
    bool has_pscm_ = false;
    bool has_module_ = false;
    Thread thread_{};
    UEvent exit_{};
    UEvent closed_event_{};

    std::atomic<bool> asleep_{false};
    std::atomic<uint32_t> close_requested_{0};
    std::atomic<uint32_t> closed_{0};
    std::atomic<uint32_t> wakes_{0};
    std::atomic<int64_t> ack_ms_{0};
    std::atomic<bool> closed_in_time_{false};
    std::atomic<Result> error_{0};
    std::atomic<bool> released_{false};

    uint32_t close_handled_ = 0;
    uint32_t wakes_seen_ = 0;
    uint32_t online_wakes_ = 0;
    bool disabled_reported_ = false;
    std::optional<bool> online_;
    int64_t next_check_ms_ = 0;
};

}  // namespace drpc
