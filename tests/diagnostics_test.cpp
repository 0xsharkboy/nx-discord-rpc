#include <doctest/doctest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "diagnostics.hpp"

using namespace drpc;
using app::diag::Facts;
using app::diag::Reason;
using app::diag::Result;
using app::diag::StepId;
using app::diag::Steps;

namespace {

ipc::Status linked_status(int32_t last_relay_status = 204) {
    ipc::Status st;
    std::memset(&st, 0, sizeof st);
    std::strncpy(st.relay_host, "relay.example.org", sizeof st.relay_host - 1);
    std::strncpy(st.name, "Monument Valley", sizeof st.name - 1);
    st.link = ipc::LinkState::Linked;
    st.health = ipc::RelayHealth::Ok;
    st.shown = 1;
    st.elapsed_s = 10;
    st.last_relay_status = last_relay_status;
    return st;
}

Facts all_ok(const ipc::Status& st) {
    Facts f{st};
    f.network = true;
    f.healthz = app::diag::Healthz{200, "ok"};
    return f;
}

const app::diag::Step& step(const Steps& steps, StepId id) { return steps[static_cast<size_t>(id)]; }

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("diagnostics_steps_all_ok") {
    const Steps steps = app::diag::diagnose(all_ok(linked_status()));
    for (size_t i = 0; i < steps.size(); ++i) {
        CHECK(steps[i].id == static_cast<StepId>(i));
        CHECK(steps[i].result == Result::Ok);
    }
    CHECK(step(steps, StepId::Presence).reason == Reason::PresenceShown);
    CHECK(app::diag::finished(steps));
    CHECK_FALSE(app::diag::needs_help(steps));
}

TEST_CASE("diagnostics_steps_pending_until_the_checks_return") {
    Facts f{linked_status()};
    Steps steps = app::diag::diagnose(f);
    for (const auto& s : steps) CHECK(s.result == Result::Pending);
    CHECK_FALSE(app::diag::finished(steps));

    f.network = true;
    steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Network).result == Result::Ok);
    CHECK(step(steps, StepId::Relay).result == Result::Pending);
    CHECK(step(steps, StepId::Presence).result == Result::Pending);
    CHECK_FALSE(app::diag::finished(steps));
}

TEST_CASE("diagnostics_steps_no_network_skips_the_rest") {
    Facts f{linked_status()};
    f.network = false;
    const Steps steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Network).result == Result::Failed);
    CHECK(step(steps, StepId::Network).reason == Reason::NoNetwork);
    for (StepId id : {StepId::Relay, StepId::Key, StepId::Link, StepId::Presence}) {
        CHECK(step(steps, id).result == Result::Skipped);
        CHECK(step(steps, id).reason == Reason::NotChecked);
    }
    CHECK(app::diag::finished(steps));
    CHECK(app::diag::needs_help(steps));
}

TEST_CASE("diagnostics_steps_without_status") {
    // The sysmodule never answered Status: its empty relay host must not read as an invalid relay_url.
    Facts f{};
    f.no_status = true;
    f.network = true;
    const Steps steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Relay).result == Result::Failed);
    CHECK(step(steps, StepId::Relay).reason == Reason::NoSysmodule);
    CHECK(step(steps, StepId::Key).result == Result::Skipped);
    CHECK(app::diag::finished(steps));
    CHECK(std::string(app::diag::reason_key(Reason::NoSysmodule)) == "app/diag/reason/no_sysmodule");
}

TEST_CASE("diagnostics_steps_relay_url_invalid") {
    ipc::Status st = linked_status();
    st.relay_host[0] = '\0';
    st.health = ipc::RelayHealth::NoUrl;
    Facts f{st};
    f.network = true;  // no probe is made without a host
    const Steps steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Relay).result == Result::Failed);
    CHECK(step(steps, StepId::Relay).reason == Reason::NoUrl);
    CHECK(step(steps, StepId::Key).result == Result::Skipped);
    CHECK(app::diag::finished(steps));
}

TEST_CASE("diagnostics_steps_relay_unreachable") {
    Facts f = all_ok(linked_status(-1));
    f.healthz = app::diag::Healthz{0, ""};
    const Steps steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Network).result == Result::Ok);
    CHECK(step(steps, StepId::Relay).result == Result::Failed);
    CHECK(step(steps, StepId::Relay).reason == Reason::RelayUnreachable);
    CHECK(step(steps, StepId::Key).result == Result::Skipped);
    CHECK(step(steps, StepId::Link).result == Result::Skipped);
    CHECK(step(steps, StepId::Presence).result == Result::Skipped);
    CHECK(app::diag::needs_help(steps));
}

TEST_CASE("diagnostics_steps_relay_http_error_keeps_the_code") {
    Facts f = all_ok(linked_status());
    f.healthz = app::diag::Healthz{502, "Bad Gateway"};
    const Steps steps = app::diag::diagnose(f);
    CHECK(step(steps, StepId::Relay).result == Result::Failed);
    CHECK(step(steps, StepId::Relay).reason == Reason::RelayHttp);
    CHECK(step(steps, StepId::Relay).code == 502);
}

TEST_CASE("diagnostics_steps_relay_must_answer_ok") {
    Facts f = all_ok(linked_status());
    f.healthz = app::diag::Healthz{200, "<html>captive portal</html>"};
    CHECK(step(app::diag::diagnose(f), StepId::Relay).reason == Reason::RelayWrongAnswer);
    f.healthz = app::diag::Healthz{200, "ok\n"};
    CHECK(step(app::diag::diagnose(f), StepId::Relay).result == Result::Ok);
}

TEST_CASE("diagnostics_steps_no_key") {
    ipc::Status st = linked_status(0);
    st.link = ipc::LinkState::NoKey;
    const Steps steps = app::diag::diagnose(all_ok(st));
    CHECK(step(steps, StepId::Key).result == Result::Failed);
    CHECK(step(steps, StepId::Key).reason == Reason::NoKey);
    CHECK(step(steps, StepId::Link).result == Result::Skipped);
    CHECK(step(steps, StepId::Presence).result == Result::Skipped);
}

TEST_CASE("diagnostics_steps_key_rejected") {
    ipc::Status st = linked_status(401);
    st.link = ipc::LinkState::Unauthorized;
    const Steps steps = app::diag::diagnose(all_ok(st));
    CHECK(step(steps, StepId::Key).result == Result::Failed);
    CHECK(step(steps, StepId::Key).reason == Reason::KeyRejected);
    CHECK(step(steps, StepId::Link).result == Result::Skipped);
}

TEST_CASE("diagnostics_steps_link_broken") {
    ipc::Status st = linked_status(409);
    st.link = ipc::LinkState::LinkBroken;
    const Steps steps = app::diag::diagnose(all_ok(st));
    CHECK(step(steps, StepId::Key).result == Result::Ok);
    CHECK(step(steps, StepId::Link).result == Result::Failed);
    CHECK(step(steps, StepId::Link).reason == Reason::LinkBroken);
    CHECK(step(steps, StepId::Presence).result == Result::Skipped);
    CHECK(app::diag::needs_help(steps));
}

TEST_CASE("diagnostics_steps_presence_follows_the_last_relay_status") {
    const auto presence = [](int32_t code) { return step(app::diag::diagnose(all_ok(linked_status(code))), StepId::Presence); };
    CHECK(presence(200).result == Result::Ok);
    CHECK(presence(204).result == Result::Ok);

    CHECK(presence(0).result == Result::Info);
    CHECK(presence(0).reason == Reason::NotSentYet);

    // The relay answered the probe just now: an older failure is retried by the sysmodule.
    CHECK(presence(-1).result == Result::Warning);
    CHECK(presence(-1).reason == Reason::NotDelivered);
    CHECK(presence(503).result == Result::Warning);
    CHECK(presence(503).reason == Reason::RelayError);
    CHECK(presence(503).code == 503);
    CHECK(presence(429).result == Result::Warning);
    CHECK(presence(429).reason == Reason::RateLimited);

    CHECK(presence(400).result == Result::Failed);
    CHECK(presence(400).reason == Reason::Refused);
    CHECK(presence(400).code == 400);
}

TEST_CASE("diagnostics_steps_backoff_is_a_warning_with_the_delay") {
    ipc::Status st = linked_status(503);
    st.health = ipc::RelayHealth::Backoff;
    st.backoff_s = 120;
    const auto& s = step(app::diag::diagnose(all_ok(st)), StepId::Presence);
    CHECK(s.result == Result::Warning);
    CHECK(s.reason == Reason::Retrying);
    CHECK(s.code == 120);
}

TEST_CASE("diagnostics_steps_nothing_to_show_is_not_a_failure") {
    ipc::Status st = linked_status(204);
    st.shown = 0;
    st.name[0] = '\0';
    st.running_title_id = 0x0100152000022000ULL;  // a Hidden game is running
    const Steps steps = app::diag::diagnose(all_ok(st));
    CHECK(step(steps, StepId::Presence).result == Result::Info);
    CHECK(step(steps, StepId::Presence).reason == Reason::NothingToShow);
    CHECK_FALSE(app::diag::needs_help(steps));
}

TEST_CASE("diagnostics_steps_every_reason_and_step_has_a_string") {
    const std::string en = read_file(std::string(NXDRPC_I18N_DIR) + "/en-US/app.json");
    REQUIRE_FALSE(en.empty());
    const auto has = [&en](const char* key) {
        // "app/diag/x/y": the leaf "y" sits in the "diag" block.
        const std::string k = key;
        REQUIRE(k.rfind("app/diag/", 0) == 0);
        const std::string leaf = "\"" + k.substr(k.rfind('/') + 1) + "\":";
        const size_t block = en.find("\"diag\":");
        return block != std::string::npos && en.find(leaf, block) != std::string::npos;
    };
    for (size_t i = 0; i < app::diag::kStepCount; ++i) {
        const char* key = app::diag::step_key(static_cast<StepId>(i));
        CHECK_MESSAGE(has(key), key);
    }
    const size_t reasons = en.find("\"reason\":", en.find("\"diag\":"));
    REQUIRE(reasons != std::string::npos);
    for (int r = 0; r <= static_cast<int>(Reason::Last); ++r) {
        const Reason reason = static_cast<Reason>(r);
        const std::string key = app::diag::reason_key(reason);
        REQUIRE_MESSAGE(has(key.c_str()), key);
        // The value takes the step's code as its {} exactly when the reason has one.
        const size_t at = en.find("\"" + key.substr(key.rfind('/') + 1) + "\":", reasons);
        const size_t open = en.find('"', en.find(':', at) + 1);
        const std::string value = en.substr(open + 1, en.find('"', open + 1) - open - 1);
        CHECK_MESSAGE((value.find("{}") != std::string::npos) == app::diag::reason_has_code(reason), key);
    }
}

TEST_CASE("diagnostics_steps_probe_target_keeps_the_port_and_path_of_config_ini") {
    const auto t = app::diag::probe_target("enabled = true\r\nrelay_url = https://relay.example.org:8443/rpc\r\n",
                                           "relay.example.org");
    CHECK(t.host == "relay.example.org");
    CHECK(t.port == 8443);
    CHECK(t.base_path == "/rpc");
}

TEST_CASE("diagnostics_steps_probe_target_falls_back_to_the_status_host") {
    // No config, another host than the sysmodule's (the file changed since boot), or an invalid URL.
    for (const char* ini : {"", "relay_url = https://other.example.org:8443\n", "relay_url = http://relay.example.org\n",
                            "# relay_url = https://relay.example.org:8443\n"}) {
        const auto t = app::diag::probe_target(ini, "relay.example.org");
        CHECK(t.host == "relay.example.org");
        CHECK(t.port == 443);
        CHECK(t.base_path.empty());
    }
}

TEST_CASE("diagnostics_steps_sysmodule_offline_makes_an_old_success_stale") {
    // The app reached the relay, but the sysmodule sees no network: its last 204 says nothing about now.
    ipc::Status st = linked_status(204);
    st.health = ipc::RelayHealth::Offline;
    const auto& s = step(app::diag::diagnose(all_ok(st)), StepId::Presence);
    CHECK(s.result == Result::Warning);
    CHECK(s.reason == Reason::NotDelivered);
}

TEST_CASE("diagnostics_steps_probe_target_skips_a_utf8_bom") {
    const auto t = app::diag::probe_target("\xEF\xBB\xBFrelay_url = https://relay.example.org:8443/rpc\n", "relay.example.org");
    CHECK(t.port == 8443);
    CHECK(t.base_path == "/rpc");
}
