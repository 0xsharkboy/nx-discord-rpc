#include <switch.h>

#include <cstdio>
#include <malloc.h>
#include <optional>
#include <string>

#include "core/settings.hpp"
#include "platform/clock.hpp"
#include "platform/config_store.hpp"
#include "platform/init.hpp"
#include "platform/log.hpp"
#include "platform/power_net.hpp"
#include "platform/title_monitor.hpp"

namespace drpc {
namespace {

constexpr int64_t kLoopMs = 250;
constexpr int64_t kHeapLogIntervalMs = 60'000;
constexpr int64_t kAsleepSliceMs = 100;

void log_wake(const PowerNet::WakeReport& wake) {
    log::info("Console awake; sleep acknowledged after " + std::to_string(wake.ack_ms) + " ms" +
              (wake.closed_in_time ? "" : " without waiting for the main loop"));
}

void log_heap() {
    const struct mallinfo mi = mallinfo();
    char buf[96];
    std::snprintf(buf, sizeof buf, "Heap: %lu KB in use, %lu KB arena", (unsigned long)(mi.uordblks / 1024),
                  (unsigned long)(mi.arena / 1024));
    log::info(buf);
}

void log_title_change(const std::optional<RunningTitle>& title) {
    if (title) {
        log::info("Running title " + format_title_id(title->title_id) + ": " + title->name);
    } else {
        log::info("HOME Menu");
    }
}

[[noreturn]] void run() {
    log::info("nx-discord-rpc starting");
    ConfigStore store;
    const Settings settings = store.load();
    log::set_debug(settings.debug_log);

    static PowerNet power;

    bool polled = false;
    std::optional<uint64_t> shown_id;
    int64_t next_heap_log = 0;

    for (;;) {
        if (power.take_close_request()) power.connection_closed();
        // Until the console wakes: no log or title polling, either may block while asleep.
        if (power.asleep()) {
            svcSleepThread(kAsleepSliceMs * 1'000'000);
            continue;
        }
        if (const auto wake = power.take_wake_report()) log_wake(*wake);
        if (const Result rc = power.take_error()) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "Power state request failed: 0x%x", rc);
            log::warn(buf);
        }
        if (power.take_disabled()) log::warn("Sleep handling disabled after repeated wait failures");

        const auto title = current_title();
        const std::optional<uint64_t> id = title ? std::optional{title->title_id} : std::nullopt;
        if (!polled || id != shown_id) {
            polled = true;
            shown_id = id;
            log_title_change(title);
        }

        const int64_t now = mono_ms();
        if (now >= next_heap_log) {
            log_heap();
            next_heap_log = now + kHeapLogIntervalMs;
        }

        svcSleepThread(kLoopMs * 1'000'000);
    }
}

}  // namespace
}  // namespace drpc

int main(int, char**) {
    if (const auto failure = drpc::init_failure()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "Service %s failed to start: 0x%x; exiting", failure->service, failure->rc);
        drpc::log::error(buf);
        return 0;
    }
    drpc::run();
}
