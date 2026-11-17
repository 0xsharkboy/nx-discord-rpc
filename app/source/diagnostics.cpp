#include "diagnostics.hpp"

#include <string_view>

namespace app::diag {

namespace {

using drpc::ipc::LinkState;
using drpc::ipc::RelayHealth;

Step make(StepId id, Result result, Reason reason, int code = 0) { return {id, result, reason, code}; }

// What a step shows when the one before it did not pass.
Step after(StepId id, const Step& previous) {
    if (previous.result == Result::Pending) return make(id, Result::Pending, Reason::Checking);
    return make(id, Result::Skipped, Reason::NotChecked);
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

Step network(const Facts& f) {
    if (!f.network) return make(StepId::Network, Result::Pending, Reason::Checking);
    if (!*f.network) return make(StepId::Network, Result::Failed, Reason::NoNetwork);
    return make(StepId::Network, Result::Ok, Reason::NetworkOk);
}

Step relay(const Facts& f, const Step& net) {
    if (net.result != Result::Ok) return after(StepId::Relay, net);
    if (f.status.relay_host[0] == '\0' || f.status.health == RelayHealth::NoUrl)
        return make(StepId::Relay, Result::Failed, Reason::NoUrl);
    if (!f.healthz) return make(StepId::Relay, Result::Pending, Reason::Checking);
    const Healthz& h = *f.healthz;
    if (h.status == 0) return make(StepId::Relay, Result::Failed, Reason::RelayUnreachable);
    if (h.status != 200) return make(StepId::Relay, Result::Failed, Reason::RelayHttp, h.status);
    if (trim(h.body) != "ok") return make(StepId::Relay, Result::Failed, Reason::RelayWrongAnswer);
    return make(StepId::Relay, Result::Ok, Reason::RelayOk);
}

Step key(const Facts& f, const Step& rel) {
    if (rel.result != Result::Ok) return after(StepId::Key, rel);
    switch (f.status.link) {
        case LinkState::NoKey:
            return make(StepId::Key, Result::Failed, Reason::NoKey);
        case LinkState::Unauthorized:
            return make(StepId::Key, Result::Failed, Reason::KeyRejected);
        case LinkState::Linked:
        case LinkState::LinkBroken:
            break;
    }
    return make(StepId::Key, Result::Ok, Reason::KeyOk);
}

Step link(const Facts& f, const Step& k) {
    if (k.result != Result::Ok) return after(StepId::Link, k);
    if (f.status.link == LinkState::LinkBroken) return make(StepId::Link, Result::Failed, Reason::LinkBroken);
    return make(StepId::Link, Result::Ok, Reason::LinkOk);
}

// The relay answered the probe a moment ago, so a failed last update is one the sysmodule retries: a Warning.
Step presence(const Facts& f, const Step& l) {
    if (l.result != Result::Ok) return after(StepId::Presence, l);
    const drpc::ipc::Status& s = f.status;
    if (!s.shown) return make(StepId::Presence, Result::Info, Reason::NothingToShow);
    if (s.health == RelayHealth::Backoff)
        return make(StepId::Presence, Result::Warning, Reason::Retrying, static_cast<int>(s.backoff_s));
    // The sysmodule sees no network: whatever it sent last is stale.
    if (s.health == RelayHealth::Offline) return make(StepId::Presence, Result::Warning, Reason::NotDelivered);
    const int32_t code = s.last_relay_status;
    if (code >= 200 && code < 300) return make(StepId::Presence, Result::Ok, Reason::PresenceShown);
    if (code == 0) return make(StepId::Presence, Result::Info, Reason::NotSentYet);
    if (code < 0) return make(StepId::Presence, Result::Warning, Reason::NotDelivered);
    if (code == 429) return make(StepId::Presence, Result::Warning, Reason::RateLimited);
    if (code >= 500) return make(StepId::Presence, Result::Warning, Reason::RelayError, code);
    return make(StepId::Presence, Result::Failed, Reason::Refused, code);
}

}  // namespace

Steps diagnose(const Facts& facts) {
    Steps steps{};
    steps[0] = network(facts);
    steps[1] = relay(facts, steps[0]);
    steps[2] = key(facts, steps[1]);
    steps[3] = link(facts, steps[2]);
    steps[4] = presence(facts, steps[3]);
    return steps;
}

bool finished(const Steps& steps) {
    for (const Step& s : steps) {
        if (s.result == Result::Pending) return false;
    }
    return true;
}

bool needs_help(const Steps& steps) {
    for (const Step& s : steps) {
        if (s.result == Result::Failed || s.result == Result::Warning) return true;
    }
    return false;
}

const char* step_key(StepId id) {
    switch (id) {
        case StepId::Network: return "app/diag/step/network";
        case StepId::Relay: return "app/diag/step/relay";
        case StepId::Key: return "app/diag/step/key";
        case StepId::Link: return "app/diag/step/link";
        case StepId::Presence: return "app/diag/step/presence";
    }
    return "app/diag/step/network";
}

const char* reason_key(Reason reason) {
    switch (reason) {
        case Reason::Checking: return "app/diag/reason/checking";
        case Reason::NotChecked: return "app/diag/reason/not_checked";
        case Reason::NetworkOk: return "app/diag/reason/network_ok";
        case Reason::NoNetwork: return "app/diag/reason/no_network";
        case Reason::RelayOk: return "app/diag/reason/relay_ok";
        case Reason::NoUrl: return "app/diag/reason/no_url";
        case Reason::RelayUnreachable: return "app/diag/reason/relay_unreachable";
        case Reason::RelayHttp: return "app/diag/reason/relay_http";
        case Reason::RelayWrongAnswer: return "app/diag/reason/relay_wrong_answer";
        case Reason::KeyOk: return "app/diag/reason/key_ok";
        case Reason::NoKey: return "app/diag/reason/no_key";
        case Reason::KeyRejected: return "app/diag/reason/key_rejected";
        case Reason::LinkOk: return "app/diag/reason/link_ok";
        case Reason::LinkBroken: return "app/diag/reason/link_broken";
        case Reason::PresenceShown: return "app/diag/reason/presence_shown";
        case Reason::NothingToShow: return "app/diag/reason/nothing_to_show";
        case Reason::NotSentYet: return "app/diag/reason/not_sent_yet";
        case Reason::NotDelivered: return "app/diag/reason/not_delivered";
        case Reason::Retrying: return "app/diag/reason/retrying";
        case Reason::RateLimited: return "app/diag/reason/rate_limited";
        case Reason::RelayError: return "app/diag/reason/relay_error";
        case Reason::Refused: return "app/diag/reason/refused";
    }
    return "app/diag/reason/checking";
}

bool reason_has_code(Reason reason) {
    return reason == Reason::RelayHttp || reason == Reason::Retrying || reason == Reason::RelayError ||
           reason == Reason::Refused;
}

drpc::RelayUrl probe_target(std::string_view ini, std::string_view status_host) {
    // Like the sysmodule's parse_settings: a UTF-8 BOM (Windows Notepad) is not part of the first key.
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (ini.starts_with(kBom)) ini.remove_prefix(kBom.size());
    std::optional<std::string_view> configured;
    while (!ini.empty()) {
        const size_t nl = ini.find('\n');
        const std::string_view line = trim(ini.substr(0, nl));
        ini = nl == std::string_view::npos ? std::string_view{} : ini.substr(nl + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)) != "relay_url") continue;
        configured = trim(line.substr(eq + 1));  // the last one wins, as in the sysmodule
    }
    if (configured) {
        if (auto url = drpc::parse_relay_url(*configured); url && url->host == status_host) return *url;
    }
    return drpc::RelayUrl{std::string(status_host), 443, ""};
}

}  // namespace app::diag
