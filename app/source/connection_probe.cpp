#include "connection_probe.hpp"

#include <utility>

#ifdef __SWITCH__
#include <switch.h>

#include <cstdio>
#include <span>

#include "core/http.hpp"
#include "drpc/tls_transport.hpp"
#include "platform/paths.hpp"
#else
#include <chrono>
#include <thread>
#endif

namespace app {

#ifdef __SWITCH__

namespace {

constexpr int64_t kBudgetMs = 8'000;
constexpr size_t kMaxBody = 256;
constexpr size_t kMaxConfigBytes = 64 * 1024;

std::string read_config() {
    std::string ini;
    FILE* f = std::fopen(drpc::paths::kConfig, "rb");
    if (!f) return ini;
    ini.resize(kMaxConfigBytes);
    ini.resize(std::fread(ini.data(), 1, ini.size(), f));
    std::fclose(f);
    return ini;
}

}  // namespace

bool network_connected(drpc::ipc::RelayHealth) {
    NifmInternetConnectionType type{};
    u32 strength = 0;
    NifmInternetConnectionStatus status{};
    return R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
           status == NifmInternetConnectionStatus_Connected;
}

diag::Healthz probe_healthz(const std::string& relay_host, drpc::ipc::RelayHealth) {
    const drpc::RelayUrl url = diag::probe_target(read_config(), relay_host);
    drpc::TlsTransport tls;
    tls.set_time_budget(kBudgetMs);
    const std::string head = drpc::build_request("GET", url, "/healthz", "", "", 0);
    drpc::HttpResponse response =
        drpc::round_trip(tls, url, head, std::span<const uint8_t>(), kMaxBody, int(kBudgetMs));
    return diag::Healthz{response.status, std::move(response.body)};
}

#else

// Desktop builds have no TLS transport: the fake scenario decides, after a short wait like a real request.
bool network_connected(drpc::ipc::RelayHealth fake_health) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    return fake_health != drpc::ipc::RelayHealth::Offline;
}

diag::Healthz probe_healthz(const std::string&, drpc::ipc::RelayHealth fake_health) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (fake_health == drpc::ipc::RelayHealth::Backoff) return diag::Healthz{0, ""};
    return diag::Healthz{200, "ok"};
}

#endif

}  // namespace app
