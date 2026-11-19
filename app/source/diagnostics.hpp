#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "core/relay_url.hpp"
#include "drpc/protocol.hpp"

// The connection test: maps what the app checked itself (network, GET /healthz) and the sysmodule's
// Status to one result per step. Pure, so the host tests cover it; the strings live in i18n.
namespace app::diag {

// In the order the test runs and shows them.
enum class StepId : uint8_t { Network, Relay, Key, Link, Presence };
inline constexpr size_t kStepCount = 5;

enum class Result : uint8_t {
    Pending,  // still being checked
    Ok,
    Info,     // nothing wrong, but nothing to confirm either
    Warning,  // works, but the last update did not get through yet
    Failed,
    Skipped,  // not checked, since an earlier step failed
};

// Each value has a string, app/diag/reason/<name> (see reason_key).
enum class Reason : uint8_t {
    Checking,
    NotChecked,
    NetworkOk,
    NoNetwork,
    RelayOk,
    NoUrl,
    RelayUnreachable,
    RelayHttp,         // code: the HTTP status
    RelayWrongAnswer,  // 200, but not the relay's "ok" (a captive portal, another server)
    KeyOk,
    NoKey,
    KeyRejected,
    LinkOk,
    LinkBroken,
    PresenceShown,
    NothingToShow,
    NotSentYet,
    NotDelivered,
    Retrying,     // code: seconds until the sysmodule tries again
    RateLimited,
    RelayError,   // code: the HTTP status
    Refused,      // code: the HTTP status
    NoSysmodule,  // the sysmodule never answered, so its Status (relay host, link) is unknown
    Outdated,         // version: the relay refuses this app's version and names its minimum (Status.min_version)
    OutdatedUnnamed,  // the relay refuses this app's version without naming a minimum
    Last = OutdatedUnnamed,
};

struct Step {
    StepId id;
    Result result;
    Reason reason;
    int code = 0;
};

using Steps = std::array<Step, kStepCount>;

// GET /healthz as the app saw it: status 0 when no answer came back.
struct Healthz {
    int status = 0;
    std::string body;
};

struct Facts {
    drpc::ipc::Status status;              // the sysmodule's, read when the test finished
    bool no_status = false;                // the sysmodule could not be asked for its Status even once
    std::optional<bool> network;           // the console's internet connection; nullopt until checked
    std::optional<Healthz> healthz;        // nullopt until checked (never made without a relay host)
};

Steps diagnose(const Facts& facts);

// No step is still Pending.
bool finished(const Steps& steps);
// A step Failed or has a Warning: worth showing the status page.
bool needs_help(const Steps& steps);

// "app/diag/step/<name>" and "app/diag/reason/<name>". A Reason's string takes code as its {} when it has one.
const char* step_key(StepId id);
const char* reason_key(Reason reason);
bool reason_has_code(Reason reason);
// The string takes Status.min_version as its {} instead of a code.
bool reason_has_version(Reason reason);

// Where to send GET /healthz: config.ini's relay_url when it names the host the sysmodule uses (its port and
// base path then count), else that host on 443. Status only carries the host.
drpc::RelayUrl probe_target(std::string_view config_ini, std::string_view status_host);

}  // namespace app::diag
