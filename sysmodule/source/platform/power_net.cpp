#include "platform/power_net.hpp"

#include <cstdio>
#include <iterator>

#include "core/power_state.hpp"
#include "platform/clock.hpp"
#include "platform/log.hpp"

namespace drpc {
namespace {

static_assert(power_action(PscPmState_ReadySleep) == PowerAction::CloseThenAck);
static_assert(power_action(PscPmState_ReadyShutdown) == PowerAction::CloseThenAck);
static_assert(power_action(PscPmState_ReadySleepCritical) == PowerAction::Ack);
static_assert(power_action(PscPmState_Awake) == PowerAction::WakeThenAck);
static_assert(power_action(PscPmState_ReadyAwaken) == PowerAction::Ack);
static_assert(power_action(PscPmState_ReadyAwakenCritical) == PowerAction::Ack);

// Not assigned to any system module (the known ids stop at Spsm = 127 with gaps
// in between); 126 is avoided because other homebrew sysmodules commonly take it.
constexpr auto kModuleId = static_cast<PscPmModuleId>(125);
// psc notifies a module before the modules it depends on: the network and the SD
// card (log) are still up while the connection to the relay is closed.
constexpr u32 kDependencies[] = {PscPmModuleId_Nifm, PscPmModuleId_Fs};
constexpr int64_t kCheckIntervalMs = 1'000;
constexpr int64_t kCloseWaitMs = 2'000;
constexpr size_t kThreadStackSize = 16 * 1024;
// Above the main thread (44) so a busy main loop cannot delay an acknowledgement.
constexpr int kThreadPriority = 40;
constexpr int kThreadCore = 3;
// Core 3 is the only core we may use: a failing wait must not spin at a priority above the main thread.
constexpr int64_t kWaitFailureBackoffNs = 10'000'000;
constexpr int kMaxWaitFailures = 100;

void warn_rc(const char* what, Result rc) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s failed: 0x%x", what, rc);
    log::warn(buf);
}

}  // namespace

PowerNet::PowerNet() {
    Result rc = pscmInitialize();
    if (R_FAILED(rc)) {
        warn_rc("psc:m", rc);
        return;
    }
    has_pscm_ = true;
    rc = pscmGetPmModule(&module_, kModuleId, kDependencies, std::size(kDependencies), true);
    if (R_FAILED(rc)) {
        warn_rc("Sleep notification registration", rc);
        return;
    }

    // Acknowledgements live on their own thread: a main loop stuck in a network or SD
    // call while the console sleeps would otherwise stall the whole console's wake.
    ueventCreate(&exit_, false);
    ueventCreate(&closed_event_, true);
    rc = threadCreate(&thread_, psc_thread, this, nullptr, kThreadStackSize, kThreadPriority, kThreadCore);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&thread_);
        if (R_FAILED(rc)) threadClose(&thread_);
    }
    if (R_FAILED(rc)) {
        // A registered module nobody acknowledges would block every sleep.
        warn_rc("Power state thread", rc);
        pscPmModuleFinalize(&module_);
        pscPmModuleClose(&module_);
        return;
    }
    has_module_ = true;
}

PowerNet::~PowerNet() {
    if (has_module_) {
        ueventSignal(&exit_);
        threadWaitForExit(&thread_);
        threadClose(&thread_);
        if (!released_) {
            pscPmModuleFinalize(&module_);
            pscPmModuleClose(&module_);
        }
    }
    if (has_pscm_) pscmExit();
}

bool PowerNet::online() {
    if (asleep_) return false;
    const uint32_t wakes = wakes_;
    if (wakes != online_wakes_) {
        online_wakes_ = wakes;
        online_.reset();
    }
    const int64_t now = mono_ms();
    if (online_ && now < next_check_ms_) return *online_;
    next_check_ms_ = now + kCheckIntervalMs;

    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status;
    type = {};
    status = {};
    const Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    const bool up = R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected;
    if (online_ != up) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s (rc 0x%x, type %d, status %d)", up ? "Network online" : "Network offline",
                      rc, static_cast<int>(type), static_cast<int>(status));
        log::info(buf);
    }
    online_ = up;
    return up;
}

bool PowerNet::take_close_request() {
    const uint32_t requested = close_requested_;
    if (requested == close_handled_) return false;
    close_handled_ = requested;
    return true;
}

void PowerNet::connection_closed() {
    closed_ = close_handled_;
    ueventSignal(&closed_event_);
}

std::optional<PowerNet::WakeReport> PowerNet::take_wake_report() {
    const uint32_t wakes = wakes_;
    if (wakes == wakes_seen_) return std::nullopt;
    wakes_seen_ = wakes;
    return WakeReport{ack_ms_, closed_in_time_, ignored_wakes_.exchange(0), states_seen_.exchange(0)};
}

// Runs on the psc thread: no logging, SD, network or other service that can block.
void PowerNet::psc_thread(void* self) {
    static_cast<PowerNet*>(self)->handle_requests();
}

void PowerNet::handle_requests() {
    int failures = 0;
    for (;;) {
        s32 idx = -1;
        Result rc = waitMulti(&idx, UINT64_MAX, waiterForEvent(&module_.event), waiterForUEvent(&exit_));
        if (R_FAILED(rc)) {
            error_ = rc;
            if (++failures >= kMaxWaitFailures) {
                // A module nobody acknowledges would hang every later sleep; without it psc stops waiting on us.
                pscPmModuleFinalize(&module_);
                pscPmModuleClose(&module_);
                released_ = true;
                asleep_ = false;
                return;
            }
            svcSleepThread(kWaitFailureBackoffNs);
            continue;
        }
        failures = 0;
        if (idx == 1) return;

        PscPmState state;
        u32 flags = 0;
        rc = pscPmModuleGetRequest(&module_, &state, &flags);
        if (R_FAILED(rc)) {
            error_ = rc;
            continue;
        }
        handle(state);
    }
}

void PowerNet::handle(PscPmState state) {
    if (static_cast<uint32_t>(state) < 32) states_seen_ |= 1u << static_cast<uint32_t>(state);
    switch (power_action(state)) {
        case PowerAction::CloseThenAck: {
            asleep_ = true;
            // A late signal from an earlier request must not count for this one.
            ueventClear(&closed_event_);
            const uint32_t request = ++close_requested_;
            const int64_t start = mono_ms();
            const int64_t deadline = start + kCloseWaitMs;
            bool in_time = false;
            for (;;) {
                if (closed_ == request) {
                    in_time = true;
                    break;
                }
                const int64_t left = deadline - mono_ms();
                if (left <= 0) break;
                s32 idx = -1;
                const Result rc = waitMulti(&idx, uint64_t(left) * 1'000'000, waiterForUEvent(&closed_event_),
                                            waiterForUEvent(&exit_));
                if (R_SUCCEEDED(rc) ? idx == 1 : rc != KERNELRESULT(TimedOut)) break;
            }
            ack_ms_ = mono_ms() - start;
            closed_in_time_ = in_time;
            break;
        }
        case PowerAction::Ack:
            if (asleep_ && (state == PscPmState_ReadyAwaken || state == PscPmState_ReadyAwakenCritical)) ++ignored_wakes_;
            break;
        case PowerAction::WakeThenAck:
            if (asleep_.exchange(false)) ++wakes_;
            break;
    }
    // Every request must be acknowledged, or the console's sleep/wake transition stalls.
    const Result rc = pscPmModuleAcknowledge(&module_, state);
    if (R_FAILED(rc)) error_ = rc;
}

}  // namespace drpc
