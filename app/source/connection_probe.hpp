#pragma once

#include <string>

#include "diagnostics.hpp"
#include "drpc/protocol.hpp"

namespace app {

// The connection test's own checks. Both block (healthz for up to 8 s): call them off the UI thread.
// On desktop, the fake's relay health stands in for the network: Offline means no network, Backoff an
// unreachable relay.
bool network_connected(drpc::ipc::RelayHealth fake_health);

// GET /healthz over verified TLS, on the relay config.ini names when it matches relay_host (see
// diag::probe_target).
diag::Healthz probe_healthz(const std::string& relay_host, drpc::ipc::RelayHealth fake_health);

}  // namespace app
