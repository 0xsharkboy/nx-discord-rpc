#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "drpc/client.hpp"

namespace drpc {

namespace {

constexpr uint32_t kPendingSeconds = 300;
constexpr int64_t kOutcomeAfterMs = 8000;
constexpr int64_t kUnlinkAfterMs = 2000;
constexpr uint64_t kFakeTitleId = 0x0100000000010000ULL;

int64_t (*g_clock)() = nullptr;

int64_t now_ms() {
    if (g_clock) return g_clock();
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

template <size_t N>
void put(char (&dst)[N], const char* src) {
    std::snprintf(dst, N, "%s", src);
}

enum class Presence { None, Game, Home };

struct Scenario {
    std::string_view name;
    ConnectError connect = ConnectError::None;
    bool key = true;
    const char* username = "test_rpc";
    // Only reported while a key is held, so no_key scenarios show none until a link completes.
    const char* discord_id = "80351110224678912";
    Presence presence = Presence::Game;
    ipc::LinkState link = ipc::LinkState::Linked;
    ipc::RelayHealth health = ipc::RelayHealth::Ok;
    uint32_t backoff_s = 0;
    const char* relay_host = "relay.example.org";
    bool long_names = false;
    int64_t starting_ms = 0;
    bool fail_at_start = false;
    ipc::LinkPhase outcome = ipc::LinkPhase::Linked;
    ipc::LinkFailure failure = ipc::LinkFailure::None;
    uint32_t retry_after_s = 0;
    uint32_t pending_s = kPendingSeconds;
    bool unlink_fails = false;
    uint32_t remote_version = ipc::kProtocolVersion + 1;
    int32_t relay_status = 204;
    bool hidden_full = false;  // the hidden list holds kMaxHiddenTitles entries
    bool paused = false;       // the game is paused: "Paused · HOME Menu", no elapsed time
    ipc::TitleVisibility running = ipc::TitleVisibility::Shown;  // the running title's place in the hidden list
    const char* min_version = "";  // named by the relay when the link state or a link failure is Outdated
};

Scenario no_key(std::string_view name) {
    Scenario s{name};
    s.key = false;
    s.username = "";
    s.presence = Presence::None;
    s.link = ipc::LinkState::NoKey;
    s.relay_status = 0;
    return s;
}

Scenario link_outcome(std::string_view name, ipc::LinkPhase outcome,
                      ipc::LinkFailure failure = ipc::LinkFailure::None) {
    Scenario s = no_key(name);
    s.outcome = outcome;
    s.failure = failure;
    return s;
}

const std::string& long_name() {
    static const std::string kValue(128, 'W');
    return kValue;
}

const std::string& long_details() {
    static const std::string kValue = "Playing " + std::string(120, 'm');
    return kValue;
}

const std::string& long_uri() {
    static const std::string kValue = "https://relay.example.org/link/" + std::string(96, 'a');
    return kValue;
}

const Scenario* find_scenario(std::string_view name) {
    static const auto kScenarios = [] {
        Scenario linked_home{"linked-home"};
        linked_home.presence = Presence::Home;

        Scenario backoff{"backoff"};
        backoff.health = ipc::RelayHealth::Backoff;
        backoff.backoff_s = 120;
        backoff.relay_status = 503;

        Scenario missing = no_key("missing");
        missing.connect = ConnectError::NotRunning;
        Scenario mismatch = no_key("mismatch");
        mismatch.connect = ConnectError::VersionMismatch;
        Scenario mismatch_old = mismatch;
        mismatch_old.name = "mismatch-old";
        mismatch_old.remote_version = 0;

        Scenario expired = link_outcome("link-expired", ipc::LinkPhase::Expired);
        expired.pending_s = kOutcomeAfterMs / 1000;
        Scenario rate_limited =
            link_outcome("link-ratelimited", ipc::LinkPhase::Failed, ipc::LinkFailure::RateLimited);
        rate_limited.starting_ms = 1500;
        rate_limited.fail_at_start = true;
        rate_limited.retry_after_s = 1500;
        Scenario unreachable =
            link_outcome("link-unreachable", ipc::LinkPhase::Failed, ipc::LinkFailure::Unreachable);
        unreachable.starting_ms = 1500;
        unreachable.fail_at_start = true;
        Scenario link_outdated = link_outcome("link-outdated", ipc::LinkPhase::Failed, ipc::LinkFailure::Outdated);
        link_outdated.starting_ms = 1500;
        link_outdated.fail_at_start = true;
        link_outdated.min_version = "0.7.0";
        Scenario starting = no_key("link-starting");
        starting.starting_ms = 6000;

        Scenario unauthorized{"unauthorized"};
        unauthorized.link = ipc::LinkState::Unauthorized;
        unauthorized.relay_status = 401;
        Scenario broken{"broken"};
        broken.link = ipc::LinkState::LinkBroken;
        broken.relay_status = 409;
        Scenario outdated{"outdated"};
        outdated.link = ipc::LinkState::Outdated;
        outdated.relay_status = 426;
        outdated.min_version = "0.7.0";
        Scenario offline{"offline"};
        offline.health = ipc::RelayHealth::Offline;
        offline.relay_status = -1;
        Scenario nourl = no_key("nourl");
        nourl.health = ipc::RelayHealth::NoUrl;
        nourl.relay_host = "";
        Scenario unlink_failed{"unlink-failed"};
        unlink_failed.unlink_fails = true;
        Scenario unnamed{"linked-unnamed"};
        unnamed.username = "";
        Scenario long_names{"long-names"};
        long_names.long_names = true;
        long_names.username = "a_very_long_discord_username_32c";
        Scenario hidden_full{"hidden-full"};
        hidden_full.hidden_full = true;
        Scenario paused{"linked-paused"};
        paused.paused = true;
        Scenario private_game{"linked-private"};
        private_game.running = ipc::TitleVisibility::Private;
        Scenario idle{"linked-idle"};
        idle.presence = Presence::None;

        return std::to_array<Scenario>({
            Scenario{"linked-game"},
            linked_home,
            no_key("nokey"),
            no_key("link-flow"),
            backoff,
            missing,
            mismatch,
            mismatch_old,
            link_outcome("link-denied", ipc::LinkPhase::Denied),
            expired,
            rate_limited,
            unreachable,
            link_outdated,
            link_outcome("link-interrupted", ipc::LinkPhase::Failed, ipc::LinkFailure::Interrupted),
            link_outcome("link-cancelled", ipc::LinkPhase::Idle),
            starting,
            unauthorized,
            broken,
            outdated,
            offline,
            nourl,
            unlink_failed,
            unnamed,
            long_names,
            hidden_full,
            paused,
            private_game,
            idle,
        });
    }();
    for (const auto& s : kScenarios) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

class FakeClient final : public Client {
public:
    explicit FakeClient(const Scenario& s) : sc_(s), state_(s.link), username_(s.username), key_(s.key) {
        std::memset(&settings_, 0, sizeof settings_);
        settings_.enabled = 1;
        settings_.show_firmware = 1;
        settings_.show_home_menu = 1;
        settings_.show_paused = 1;
        settings_.show_badge = 1;
        // Two sample entries, neither of them the running title.
        std::memset(&hidden_, 0, sizeof hidden_);
        hidden_.count = 2;
        hidden_.items[0] = {0x01007EF00011E000ULL, static_cast<uint8_t>(ipc::TitleVisibility::Private), {}};
        hidden_.items[1] = {0x0100152000022000ULL, static_cast<uint8_t>(ipc::TitleVisibility::Hidden), {}};
        if (s.running != ipc::TitleVisibility::Shown)
            hidden_.items[hidden_.count++] = {kFakeTitleId, static_cast<uint8_t>(s.running), {}};
        // hidden-full: games that are not installed fill the rest of the list.
        for (uint64_t i = 0; s.hidden_full && hidden_.count < ipc::kMaxHiddenTitles; ++i)
            hidden_.items[hidden_.count++] = {0x0100C00000000000ULL + (i << 16),
                                              static_cast<uint8_t>(ipc::TitleVisibility::Hidden), {}};
        std::memset(&link_, 0, sizeof link_);
        if (key_ && state_ == ipc::LinkState::Linked) {
            link_.phase = ipc::LinkPhase::Linked;
            put(link_.username, username_.c_str());
        }
    }

    ConnectError connect() override {
        connected_ = sc_.connect == ConnectError::None;
        return sc_.connect;
    }

    uint32_t remote_version() const override {
        return sc_.connect == ConnectError::VersionMismatch ? sc_.remote_version : ipc::kProtocolVersion;
    }

    bool status(ipc::Status& out) override {
        if (!connected_) return false;
        std::memset(&out, 0, sizeof out);
        put(out.sysmodule_version, NXDRPC_VERSION);
        put(out.relay_host, sc_.relay_host);
        out.elapsed_s = -1;
        out.health = sc_.health;
        out.backoff_s = sc_.backoff_s;
        refresh_link();
        refresh_unlink();
        out.link = key_ ? state_ : ipc::LinkState::NoKey;
        if (key_) {
            put(out.username, username_.c_str());
            put(out.discord_id, sc_.discord_id);
        }
        if (sc_.presence == Presence::Game) {
            out.running_title_id = kFakeTitleId;
            const ipc::TitleVisibility vis = visibility(kFakeTitleId);
            if (vis == ipc::TitleVisibility::Private) {
                put(out.name, "Nintendo Switch");
                put(out.details, sc_.paused ? "Paused" : "Playing a game");
                out.shown = 1;
                out.elapsed_s = sc_.paused ? -1 : 754;
            } else if (vis == ipc::TitleVisibility::Shown) {
                put(out.name, sc_.long_names ? long_name().c_str() : "Monument Valley");
                put(out.details, sc_.long_names ? long_details().c_str() : sc_.paused ? "Paused \xC2\xB7 HOME Menu" : "Playing");
                put(out.title_id, "0100000000010000");
                out.is_game = 1;
                out.shown = settings_.private_mode ? 0 : 1;
                out.elapsed_s = sc_.paused ? -1 : 754;
            }
        } else if (sc_.presence == Presence::Home) {
            put(out.name, "Home Menu");
            out.shown = settings_.show_home_menu ? 1 : 0;
        }
        out.unlink = unlink_result_;
        out.last_relay_status = sc_.relay_status;
        if (out.link == ipc::LinkState::Outdated || link_.failure == ipc::LinkFailure::Outdated)
            put(out.min_version, sc_.min_version);
        return true;
    }

    bool settings(ipc::Settings& out) override {
        if (!connected_) return false;
        out = settings_;
        return true;
    }

    bool set_settings(const ipc::Settings& in) override {
        if (!connected_) return false;
        if (in.enabled > 1 || in.private_mode > 1 || in.show_firmware > 1 ||
            in.show_home_menu > 1 || in.debug_log > 1 ||
            in.show_paused > 1 || in.show_docked > 1 || in.show_playtime > 1 || in.show_badge > 1) {
            return false;
        }
        settings_ = in;
        return true;
    }

    bool link(ipc::Link& out) override {
        if (!connected_) return false;
        refresh_link();
        out = link_;
        return true;
    }

    bool start_link() override {
        if (!connected_) return false;
        if (link_.phase == ipc::LinkPhase::Pending || link_.phase == ipc::LinkPhase::Starting) return true;
        std::memset(&link_, 0, sizeof link_);
        started_ms_ = now_ms();
        if (sc_.starting_ms > 0) {
            link_.phase = ipc::LinkPhase::Starting;
        } else {
            enter_pending();
        }
        return true;
    }

    bool cancel_link() override {
        if (!connected_) return false;
        if (link_.phase == ipc::LinkPhase::Pending || link_.phase == ipc::LinkPhase::Starting)
            std::memset(&link_, 0, sizeof link_);
        return true;
    }

    bool unlink() override {
        if (!connected_) return false;
        std::memset(&link_, 0, sizeof link_);
        if (sc_.unlink_fails) {
            unlink_result_ = ipc::UnlinkResult::InProgress;
            unlink_ms_ = now_ms();
            return true;
        }
        key_ = false;
        unlink_result_ = ipc::UnlinkResult::Done;
        return true;
    }

    bool forget_key() override {
        if (!connected_) return false;
        std::memset(&link_, 0, sizeof link_);
        key_ = false;
        return true;
    }

    bool hidden(ipc::HiddenList& out) override {
        if (!connected_) return false;
        out = hidden_;
        return true;
    }

    bool set_hidden(const ipc::HiddenList& in) override {
        if (!connected_ || !ipc::valid_hidden(in)) return false;
        std::memset(&hidden_, 0, sizeof hidden_);
        hidden_.count = in.count;
        for (uint32_t i = 0; i < in.count; ++i) {
            hidden_.items[i].title_id = in.items[i].title_id;
            hidden_.items[i].visibility = in.items[i].visibility;
        }
        return true;
    }

private:
    ipc::TitleVisibility visibility(uint64_t title_id) const {
        for (uint32_t i = 0; i < hidden_.count; ++i) {
            const ipc::HiddenTitle& t = hidden_.items[i];
            if (t.title_id == title_id) return static_cast<ipc::TitleVisibility>(t.visibility);
        }
        return ipc::TitleVisibility::Shown;
    }

    void enter_pending() {
        link_.phase = ipc::LinkPhase::Pending;
        link_.seconds_left = sc_.pending_s;
        put(link_.user_code, "ABCD-EFGH");
        put(link_.verification_uri, sc_.long_names ? long_uri().c_str() : "https://relay.example.org/link");
        pending_ms_ = now_ms();
    }

    void finish() {
        std::memset(&link_, 0, sizeof link_);
        link_.phase = sc_.outcome;
        link_.failure = sc_.failure;
        link_.retry_after_s = sc_.retry_after_s;
        if (sc_.outcome == ipc::LinkPhase::Linked) {
            username_ = "test_rpc";
            put(link_.username, username_.c_str());
            key_ = true;
            state_ = ipc::LinkState::Linked;
        }
    }

    void refresh_link() {
        if (link_.phase == ipc::LinkPhase::Starting) {
            if (now_ms() - started_ms_ < sc_.starting_ms) return;
            if (sc_.fail_at_start) {
                finish();
                return;
            }
            enter_pending();
        }
        if (link_.phase != ipc::LinkPhase::Pending) return;
        const int64_t elapsed = now_ms() - pending_ms_;
        if (elapsed >= kOutcomeAfterMs) {
            finish();
            return;
        }
        link_.seconds_left = sc_.pending_s - static_cast<uint32_t>(elapsed / 1000);
    }

    void refresh_unlink() {
        if (unlink_result_ == ipc::UnlinkResult::InProgress && now_ms() - unlink_ms_ >= kUnlinkAfterMs)
            unlink_result_ = ipc::UnlinkResult::Failed;
    }

    const Scenario& sc_;
    ipc::Settings settings_;
    ipc::Link link_;
    ipc::HiddenList hidden_;
    ipc::UnlinkResult unlink_result_ = ipc::UnlinkResult::None;
    ipc::LinkState state_;
    std::string username_;
    int64_t started_ms_ = 0;
    int64_t pending_ms_ = 0;
    int64_t unlink_ms_ = 0;
    bool key_;
    bool connected_ = false;
};

}  // namespace

void set_fake_clock(int64_t (*clock)()) { g_clock = clock; }

std::unique_ptr<Client> make_fake_client(std::string_view scenario) {
    const Scenario* s = find_scenario(scenario);
    if (!s) return nullptr;
    return std::make_unique<FakeClient>(*s);
}

}  // namespace drpc
