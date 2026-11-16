#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include "drpc/client.hpp"

namespace drpc {

namespace {

constexpr uint32_t kPendingSeconds = 300;
constexpr int64_t kLinkAfterMs = 8000;

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

enum class Scenario { LinkedGame, LinkedHome, NoKey, LinkFlow, Backoff, Missing, Mismatch };

class FakeClient final : public Client {
public:
    explicit FakeClient(Scenario s) : scenario_(s) {
        std::memset(&settings_, 0, sizeof settings_);
        settings_.enabled = 1;
        settings_.show_elapsed = 1;
        settings_.show_firmware = 1;
        settings_.show_home_menu = 1;
        std::memset(&link_, 0, sizeof link_);
        key_ = linked();
        if (key_) {
            link_.phase = ipc::LinkPhase::Linked;
            put(link_.username, "test_rpc");
        }
    }

    ConnectError connect() override {
        if (scenario_ == Scenario::Missing) return ConnectError::NotRunning;
        if (scenario_ == Scenario::Mismatch) return ConnectError::VersionMismatch;
        return ConnectError::None;
    }

    uint32_t remote_version() const override {
        return scenario_ == Scenario::Mismatch ? 2 : ipc::kProtocolVersion;
    }

    bool status(ipc::Status& out) override {
        std::memset(&out, 0, sizeof out);
        put(out.sysmodule_version, "0.1.0");
        put(out.relay_host, "relay.example.org");
        out.link = ipc::LinkState::NoKey;
        out.health = ipc::RelayHealth::Ok;
        refresh_link();
        if (key_) {
            out.link = ipc::LinkState::Linked;
            put(out.username, "test_rpc");
        }
        if (scenario_ == Scenario::LinkedGame || scenario_ == Scenario::Backoff) {
            put(out.name, "Monument Valley");
            put(out.details, "Playing");
            put(out.title_id, "0100000000010000");
            out.is_game = 1;
            out.shown = settings_.private_mode ? 0 : 1;
            out.elapsed_s = 754;
        } else if (scenario_ == Scenario::LinkedHome) {
            put(out.name, "Home Menu");
            out.shown = settings_.show_home_menu ? 1 : 0;
        }
        if (scenario_ == Scenario::Backoff) {
            out.health = ipc::RelayHealth::Backoff;
            out.backoff_s = 120;
        }
        out.unlink = unlink_result_;
        return true;
    }

    bool settings(ipc::Settings& out) override {
        out = settings_;
        return true;
    }

    bool set_settings(const ipc::Settings& in) override {
        settings_ = in;
        std::memset(settings_.reserved, 0, sizeof settings_.reserved);
        return true;
    }

    bool link(ipc::Link& out) override {
        refresh_link();
        out = link_;
        return true;
    }

    bool start_link() override {
        if (link_.phase == ipc::LinkPhase::Pending) return true;
        std::memset(&link_, 0, sizeof link_);
        link_.phase = ipc::LinkPhase::Pending;
        link_.seconds_left = kPendingSeconds;
        put(link_.user_code, "ABCD-EFGH");
        put(link_.verification_uri, "https://relay.example.org/link");
        started_ms_ = now_ms();
        return true;
    }

    bool cancel_link() override {
        if (link_.phase == ipc::LinkPhase::Pending) std::memset(&link_, 0, sizeof link_);
        return true;
    }

    bool unlink() override {
        std::memset(&link_, 0, sizeof link_);
        key_ = false;
        unlink_result_ = ipc::UnlinkResult::Done;
        return true;
    }

    bool forget_key() override {
        std::memset(&link_, 0, sizeof link_);
        key_ = false;
        return true;
    }

private:
    bool linked() const {
        return scenario_ == Scenario::LinkedGame || scenario_ == Scenario::LinkedHome ||
               scenario_ == Scenario::Backoff;
    }

    void refresh_link() {
        if (link_.phase != ipc::LinkPhase::Pending) return;
        const int64_t elapsed = now_ms() - started_ms_;
        if (elapsed >= kLinkAfterMs) {
            std::memset(&link_, 0, sizeof link_);
            link_.phase = ipc::LinkPhase::Linked;
            put(link_.username, "test_rpc");
            key_ = true;
            return;
        }
        link_.seconds_left = kPendingSeconds - static_cast<uint32_t>(elapsed / 1000);
    }

    Scenario scenario_;
    ipc::Settings settings_;
    ipc::Link link_;
    ipc::UnlinkResult unlink_result_ = ipc::UnlinkResult::None;
    int64_t started_ms_ = 0;
    bool key_ = false;
};

}  // namespace

void set_fake_clock(int64_t (*clock)()) { g_clock = clock; }

std::unique_ptr<Client> make_fake_client(std::string_view scenario) {
    struct Entry {
        std::string_view name;
        Scenario value;
    };
    static constexpr Entry kScenarios[] = {
        {"linked-game", Scenario::LinkedGame}, {"linked-home", Scenario::LinkedHome},
        {"nokey", Scenario::NoKey},            {"link-flow", Scenario::LinkFlow},
        {"backoff", Scenario::Backoff},        {"missing", Scenario::Missing},
        {"mismatch", Scenario::Mismatch},
    };
    for (const auto& e : kScenarios) {
        if (e.name == scenario) return std::make_unique<FakeClient>(e.value);
    }
    return nullptr;
}

}  // namespace drpc
