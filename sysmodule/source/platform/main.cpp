#include <switch.h>

#include <cstdio>
#include <malloc.h>
#include <optional>
#include <string>

#include "core/http.hpp"
#include "core/relay_client.hpp"
#include "core/relay_url.hpp"
#include "core/report.hpp"
#include "core/settings.hpp"
#include "platform/clock.hpp"
#include "platform/config_store.hpp"
#include "platform/firmware.hpp"
#include "platform/init.hpp"
#include "platform/log.hpp"
#include "platform/power_net.hpp"
#include "platform/relay_http.hpp"
#include "platform/title_monitor.hpp"

namespace drpc {
namespace {

constexpr int64_t kLoopMs = 250;
constexpr int64_t kHeapLogIntervalMs = 60'000;
constexpr int64_t kAsleepSliceMs = 100;
constexpr int64_t kKeyCheckIntervalMs = 30'000;

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

void log_link_state(LinkState state) {
    switch (state) {
        case LinkState::Unauthorized:
            log::warn("Relay rejected the API key; link again (import api_key.txt)");
            break;
        case LinkState::LinkBroken:
            log::warn("Discord link is broken; link again (import api_key.txt)");
            break;
        case LinkState::NoKey:
        case LinkState::Ok:
            break;
    }
}

bool needs_key(LinkState state) {
    return state == LinkState::NoKey || state == LinkState::Unauthorized || state == LinkState::LinkBroken;
}

// Returns false when the key is the one in use. api_key.txt is deleted only once config.ini holds the key.
bool import_key(ConfigStore& store, Settings& settings, std::string key) {
    const bool changed = key != settings.api_key;
    if (changed) {
        settings.api_key = std::move(key);
        log::set_secrets(settings.api_key, settings.admin_key);
        log::info("API key imported");
    }
    if (store.save(settings)) {
        store.delete_api_key_file();
        if (!changed) log::info("api_key.txt holds the key already in use");
    } else {
        log::error("config.ini could not be saved; api_key.txt kept for the next start");
    }
    return changed;
}

Settings load_settings(ConfigStore& store) {
    Settings s = store.load();
    log::set_secrets(s.api_key, s.admin_key);
    if (auto key = store.import_api_key_file()) import_key(store, s, std::move(*key));
    if (s.api_key.empty()) log::warn("No API key configured; link the console and import api_key.txt");
    return s;
}

// An empty relay_url means the default. An invalid one yields nothing: the API key must not go to a host
// the user did not configure.
std::optional<RelayUrl> relay_url(const Settings& settings) {
    if (settings.relay_url.empty()) return parse_relay_url(kDefaultRelayUrl);
    auto url = parse_relay_url(settings.relay_url);
    if (!url) log::error("relay_url is invalid; nothing will be sent");
    return url;
}

// One WARN per distinct unexpected status; icon rejections (400, 413) are handled by design.
class UnexpectedStatus {
public:
    void observe(const Request& req, int status) {
        if (status <= 0 || (status >= 200 && status < 300) || status == 401 || status == 409 ||
            is_transient_http_status(status))
            return;
        if (req.kind == Request::Kind::PutIcon && (status == 400 || status == 413)) return;
        if (status == last_) return;
        last_ = status;
        log::warn("Relay answered with unexpected HTTP " + std::to_string(status));
    }

private:
    int last_ = 0;
};

// Seconds on the current title. The wall clock keeps counting across sleep, like the console's play timer.
class Elapsed {
public:
    void restart() {
        start_unix_ms_ = unix_ms();
        start_mono_ms_ = mono_ms();
    }

    int64_t seconds() const {
        const int64_t now_unix = unix_ms();
        if (start_unix_ms_ != 0 && now_unix != 0) return (now_unix - start_unix_ms_) / 1000;
        return (mono_ms() - start_mono_ms_) / 1000;
    }

private:
    int64_t start_unix_ms_ = 0;
    int64_t start_mono_ms_ = 0;
};

[[noreturn]] void run() {
    log::info("nx-discord-rpc starting");
    ConfigStore store;
    Settings settings = load_settings(store);
    log::set_debug(settings.debug_log);
    const std::optional<RelayUrl> url = relay_url(settings);
    UnexpectedStatus unexpected;
    const std::string firmware = firmware_display_version();

    static PowerNet power;
    static RelayClient client;
    client.set_key(!settings.api_key.empty(), !settings.admin_key.empty());

    bool polled = false;
    std::optional<uint64_t> shown_id;
    Elapsed elapsed;
    LinkState shown_link = client.link_state();
    int64_t next_heap_log = 0;
    int64_t next_key_check = mono_ms() + kKeyCheckIntervalMs;

    for (;;) {
        // One short-lived connection per request: none is ever open between ticks.
        if (power.take_close_request()) power.connection_closed();
        // Until the console wakes: no log, title polling or network, any of them may block while asleep.
        if (power.asleep()) {
            svcSleepThread(kAsleepSliceMs * 1'000'000);
            continue;
        }
        if (const auto wake = power.take_wake_report()) {
            log_wake(*wake);
            client.on_wake();
        }
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
            elapsed.restart();
            log_title_change(title);
        }

        SceneInput scene;
        scene.firmware = firmware;
        if (title) {
            scene.scene = Scene::Game;
            scene.title_id = title->title_id;
            scene.game_name = title->name;
        }
        client.set_report(render_report(scene, settings), elapsed.seconds());

        const int64_t now = mono_ms();
        if (needs_key(client.link_state()) && now >= next_key_check) {
            next_key_check = now + kKeyCheckIntervalMs;
            if (auto key = store.import_api_key_file()) {
                if (import_key(store, settings, std::move(*key))) client.set_key(true, !settings.admin_key.empty());
            }
        }

        if (url && power.online()) {
            if (const auto req = client.next(mono_ms())) {
                const Outcome out = execute(*url, *req, settings.api_key, settings.admin_key);
                if (req->kind == Request::Kind::PutIcon && req->use_admin_key && out.status == 401) {
                    log::warn("Relay rejected the admin key; icons use the API key until reboot");
                }
                unexpected.observe(*req, out.status);
                client.on_outcome(*req, out, mono_ms());
            }
        }

        if (client.link_state() != shown_link) {
            shown_link = client.link_state();
            log_link_state(shown_link);
        }

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
