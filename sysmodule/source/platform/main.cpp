#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <malloc.h>
#include <optional>
#include <string>

#include "core/elapsed.hpp"
#include "core/http.hpp"
#include "core/ipc_convert.hpp"
#include "core/link_client.hpp"
#include "core/relay_client.hpp"
#include "core/relay_url.hpp"
#include "core/report.hpp"
#include "core/retry.hpp"
#include "core/settings.hpp"
#include "core/settings_saver.hpp"
#include "core/shared_state.hpp"
#include "core/version.hpp"
#include "drpc/net_log.hpp"
#include "platform/clock.hpp"
#include "platform/config_store.hpp"
#include "platform/firmware.hpp"
#include "platform/init.hpp"
#include "platform/ipc_server.hpp"
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
constexpr int64_t kIpcRetryMs = 5000;
constexpr int64_t kProfileRefreshMs = 24 * 60 * 60 * 1000;

void log_wake(const PowerNet::WakeReport& wake) {
    log::info("Console awake; sleep acknowledged after " + std::to_string(wake.ack_ms) + " ms" +
              (wake.closed_in_time ? "" : " without waiting for the main loop"));
    char buf[96];
    std::snprintf(buf, sizeof buf, "Waking steps while asleep: %u; PSC states seen: 0x%x", wake.ignored_wakes, wake.states_seen);
    log::debug(buf);
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
        settings.username.clear();
        settings.discord_id.clear();
        settings.avatar.clear();
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


// What a config.ini write touches.
struct Store {
    ConfigStore& config;
    Settings& settings;
    SettingsSaver& saver;
    FailureLog failures;
};

// A failed write is retried by the debounce; only the first failure and the recovery are logged.
void save_now(Store& s, int64_t now_ms) {
    if (s.config.save(s.settings)) {
        s.saver.saved();
        if (s.failures.recovered()) log::info("config.ini saved again");
    } else {
        if (s.failures.failed(0)) log::error("config.ini could not be saved; retrying");
        s.saver.changed(now_ms);
    }
}

void clear_key(Store& s, RelayClient& client, int64_t now_ms) {
    s.settings.api_key.clear();
    s.settings.username.clear();
    s.settings.discord_id.clear();
    s.settings.avatar.clear();
    save_now(s, now_ms);
    client.set_key(false, !s.settings.admin_key.empty());
}

void apply_link_effect(const LinkEffect& effect, Store& s, RelayClient& client, LinkClient& link, int64_t now_ms) {
    if (effect.clear_key) {
        clear_key(s, client, now_ms);
        return;
    }
    bool save = false;
    if (effect.api_key) {
        if (*effect.api_key != s.settings.api_key) {
            s.settings.api_key = *effect.api_key;
            log::set_secrets(s.settings.api_key, s.settings.admin_key);
            log::info("API key imported");
        }
        client.set_key(true, !s.settings.admin_key.empty());
        if (!effect.username || effect.username->empty()) link.request_username();
        save = true;
    }
    if (effect.username && *effect.username != s.settings.username) {
        s.settings.username = *effect.username;
        save = true;
    }
    if (effect.profile &&
        (effect.profile->discord_id != s.settings.discord_id || effect.profile->avatar != s.settings.avatar)) {
        s.settings.discord_id = effect.profile->discord_id;
        s.settings.avatar = effect.profile->avatar;
        save = true;
    }
    if (save) save_now(s, now_ms);
}

const char* failure_text(ipc::LinkFailure failure) {
    switch (failure) {
        case ipc::LinkFailure::RateLimited:
            return "too many attempts, try again later";
        case ipc::LinkFailure::Unreachable:
            return "relay unreachable";
        case ipc::LinkFailure::Interrupted:
            return "interrupted by sleep";
        case ipc::LinkFailure::Other:
        case ipc::LinkFailure::None:
            break;
    }
    return "unexpected relay answer";
}

// One line per link phase or unlink result change. The code and its URL stay out of the log.
class LinkLog {
public:
    void observe(const LinkView& view, ipc::UnlinkResult unlink, const std::string& username) {
        if (view.phase != phase_ || view.failure != failure_) {
            const bool was_linking = phase_ == ipc::LinkPhase::Starting || phase_ == ipc::LinkPhase::Pending;
            phase_ = view.phase;
            failure_ = view.failure;
            log_phase(was_linking, username);
        }
        if (unlink != unlink_) {
            unlink_ = unlink;
            if (unlink == ipc::UnlinkResult::Done) log::info("Unlinked");
            if (unlink == ipc::UnlinkResult::Failed) log::warn("Unlink failed; the relay could not be reached");
        }
    }

private:
    void log_phase(bool was_linking, const std::string& username) const {
        switch (phase_) {
            case ipc::LinkPhase::Idle:
                if (was_linking) log::info("Linking cancelled");
                break;
            case ipc::LinkPhase::Starting:
                log::info("Linking: requesting a code");
                break;
            case ipc::LinkPhase::Pending:
                log::info("Linking: code shown");
                break;
            case ipc::LinkPhase::Linked:
                log::info(username.empty() ? std::string{"Linked"} : "Linked as " + username);
                break;
            case ipc::LinkPhase::Denied:
                log::info("Link denied");
                break;
            case ipc::LinkPhase::Expired:
                log::info("Link expired");
                break;
            case ipc::LinkPhase::Failed:
                log::warn(std::string{"Link failed: "} + failure_text(failure_));
                break;
        }
    }

    ipc::LinkPhase phase_ = ipc::LinkPhase::Idle;
    ipc::LinkFailure failure_ = ipc::LinkFailure::None;
    ipc::UnlinkResult unlink_ = ipc::UnlinkResult::None;
};

ipc::LinkState ipc_link_state(LinkState state) {
    switch (state) {
        case LinkState::Ok:
            return ipc::LinkState::Linked;
        case LinkState::Unauthorized:
            return ipc::LinkState::Unauthorized;
        case LinkState::LinkBroken:
            return ipc::LinkState::LinkBroken;
        case LinkState::NoKey:
            break;
    }
    return ipc::LinkState::NoKey;
}

uint32_t clamp_u32(int64_t v) {
    return static_cast<uint32_t>(std::clamp<int64_t>(v, 0, std::numeric_limits<uint32_t>::max()));
}

struct Snapshot {
    const Settings& settings;
    const std::optional<RelayUrl>& url;
    bool online;
    const std::optional<Report>& report;
    int64_t elapsed_s;
    const RelayClient& client;
    const LinkClient& link;
    int64_t now_ms;
};

void publish(SharedState& shared, const Snapshot& s) {
    ipc::Status st{};
    copy_str(st.sysmodule_version, sizeof st.sysmodule_version, kVersion);
    copy_str(st.username, sizeof st.username, s.settings.username);
    copy_str(st.discord_id, sizeof st.discord_id, s.settings.discord_id);
    copy_str(st.avatar, sizeof st.avatar, s.settings.avatar);
    if (s.url) copy_str(st.relay_host, sizeof st.relay_host, s.url->host);
    st.elapsed_s = -1;
    if (s.report) {
        st.shown = 1;
        copy_str(st.name, sizeof st.name, s.report->name);
        copy_str(st.details, sizeof st.details, s.report->details);
        if (s.report->title_id) {
            st.is_game = 1;
            copy_str(st.title_id, sizeof st.title_id, *s.report->title_id);
        }
        st.elapsed_s = std::max<int64_t>(s.elapsed_s, 0);
    }
    st.link = ipc_link_state(s.client.link_state());
    if (!s.url) {
        st.health = ipc::RelayHealth::NoUrl;
    } else if (!s.online) {
        st.health = ipc::RelayHealth::Offline;
    } else if (const auto left = s.client.backoff_left_ms(s.now_ms)) {
        st.health = ipc::RelayHealth::Backoff;
        st.backoff_s = clamp_u32((*left + 999) / 1000);
    } else {
        st.health = ipc::RelayHealth::Ok;
    }
    st.unlink = s.link.unlink_result();

    const LinkView view = s.link.view(s.now_ms);
    ipc::Link lk{};
    lk.phase = view.phase;
    lk.failure = view.failure;
    lk.seconds_left = clamp_u32(view.seconds_left);
    lk.retry_after_s = clamp_u32(view.retry_after_s);
    copy_str(lk.user_code, sizeof lk.user_code, view.user_code);
    copy_str(lk.verification_uri, sizeof lk.verification_uri, view.verification_uri);
    copy_str(lk.username, sizeof lk.username, view.username);
    shared.publish(st, lk);
}

[[noreturn]] void run() {
    log::info("NX Discord RPC starting");
    ConfigStore store;
    Settings settings = load_settings(store);
    log::set_debug(settings.debug_log);
    const std::optional<RelayUrl> url = relay_url(settings);
    UnexpectedStatus unexpected;
    const std::string firmware = firmware_display_version();

    static PowerNet power;
    static RelayClient client;
    static LinkClient link;
    static SharedState shared;
    client.set_key(!settings.api_key.empty(), !settings.admin_key.empty());
    if (!settings.api_key.empty()) link.request_username();

    SettingsSaver saver;
    Store persist{store, settings, saver, {}};
    LinkLog link_log;
    shared.seed_settings(to_ipc(settings));
    publish(shared, Snapshot{settings, url, url && power.online(), std::nullopt, 0, client, link, mono_ms()});
    RetryGate ipc_retry(kIpcRetryMs);
    bool ipc_up = ipc_server_start(shared);
    ipc_retry.attempted(mono_ms());
    if (!ipc_up) log::error("drpc service could not start; retrying");
    Result shown_ipc_error = 0;
    // An answer that came back after sleep began: applied on the first awake tick.
    std::optional<LinkEffect> deferred;

    bool polled = false;
    std::optional<uint64_t> shown_id;
    Elapsed elapsed(unix_ms, mono_ms);
    LinkState shown_link = client.link_state();
    int64_t next_heap_log = 0;
    int64_t next_key_check = mono_ms() + kKeyCheckIntervalMs;
    int64_t next_profile_ms = mono_ms() + kProfileRefreshMs;

    for (;;) {
        // One short-lived connection per request: none is ever open between ticks.
        if (power.take_close_request()) {
            link.on_sleep();
            power.connection_closed();
        }
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
        if (const Result rc = ipc_server_take_error(); rc != 0 && rc != shown_ipc_error) {
            shown_ipc_error = rc;
            char buf[64];
            std::snprintf(buf, sizeof buf, "drpc service error: 0x%x", rc);
            log::warn(buf);
        }

        const int64_t now = mono_ms();
        if (!ipc_up && ipc_retry.due(now)) {
            ipc_retry.attempted(now);
            ipc_up = ipc_server_start(shared);
            if (ipc_up) log::info("drpc service started");
        }
        if (deferred) {
            apply_link_effect(*deferred, persist, client, link, now);
            deferred.reset();
        }
        if (const auto changed = shared.take_settings()) {
            const Settings before = settings;
            apply_ipc(*changed, settings);
            log::set_debug(settings.debug_log);
            shared.seed_settings(to_ipc(settings));
            if (settings != before) saver.changed(now);
        }
        if (saver.due(now)) save_now(persist, now);

        if (shared.take_request(ipc::Cmd::StartLink)) link.start(now);
        if (shared.take_request(ipc::Cmd::CancelLink)) link.cancel();
        if (shared.take_request(ipc::Cmd::Unlink)) link.unlink();
        if (shared.take_request(ipc::Cmd::ForgetKey)) {
            link.forget();
            clear_key(persist, client, now);
            log::info("Forgot the API key locally");
        }

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
        const std::optional<Report> report = render_report(scene, settings);
        client.set_report(report, elapsed.seconds());

        if (needs_key(client.link_state()) && now >= next_key_check) {
            next_key_check = now + kKeyCheckIntervalMs;
            if (auto key = store.import_api_key_file()) {
                if (import_key(store, settings, std::move(*key))) {
                    client.set_key(true, !settings.admin_key.empty());
                    link.request_username();
                }
            }
        }

        const bool online = url && power.online();
        if (online && client.link_state() == LinkState::Ok && now >= next_profile_ms) {
            link.request_username();
            next_profile_ms = now + kProfileRefreshMs;
        }
        // Before the request, which can block for 8 s: the UIs see what this tick took.
        publish(shared, Snapshot{settings, url, online, report, elapsed.seconds(), client, link, mono_ms()});

        if (!url) {
            if (const auto lreq = link.next(now)) {
                apply_link_effect(link.on_response(*lreq, LinkResponse{}, now), persist, client, link, now);
            }
        } else if (online) {
            if (const auto lreq = link.next(now)) {
                const LinkResponse resp = execute_link(*url, *lreq, settings.api_key);
                const LinkEffect effect = link.on_response(*lreq, resp, mono_ms());
                if (power.asleep()) {
                    deferred = effect;
                } else {
                    apply_link_effect(effect, persist, client, link, mono_ms());
                }
            } else if (const auto req = client.next(mono_ms())) {
                const Outcome out = execute(*url, *req, settings.api_key, settings.admin_key);
                if (!power.asleep()) {
                    if (req->kind == Request::Kind::PutIcon && req->use_admin_key && out.status == 401) {
                        log::warn("Relay rejected the admin key; icons use the API key until reboot");
                    }
                    unexpected.observe(*req, out.status);
                }
                client.on_outcome(*req, out, mono_ms());
            }
        } else if (!power.asleep()) {
            link.on_offline();
        }

        const int64_t after = mono_ms();
        publish(shared, Snapshot{settings, url, online, report, elapsed.seconds(), client, link, after});
        // Sleep began during the request: no log or SD access until the console wakes.
        if (power.asleep()) continue;

        if (client.link_state() != shown_link) {
            shown_link = client.link_state();
            log_link_state(shown_link);
        }
        link_log.observe(link.view(after), link.unlink_result(), settings.username);

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
    drpc::set_net_log([](std::string_view message) { drpc::log::warn(std::string{message}); });
    if (const auto failure = drpc::init_failure()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "Service %s failed to start: 0x%x; exiting", failure->service, failure->rc);
        drpc::log::error(buf);
        return 0;
    }
    drpc::run();
}
