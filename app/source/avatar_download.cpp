#include "avatar_download.hpp"

#include <span>
#include <string>

#include "avatar_cache.hpp"

#ifdef __SWITCH__
#include "core/http.hpp"
#include "core/relay_url.hpp"
#include "drpc/discord_ids.hpp"
#include "drpc/tls_transport.hpp"
#else
#include <cstdio>
#include <cstdlib>
#endif

namespace app {

#ifdef __SWITCH__

namespace {

constexpr int64_t kBudgetMs = 8'000;

}  // namespace

std::optional<std::vector<uint8_t>> download_avatar(std::string_view path) {
    const drpc::RelayUrl url{std::string(drpc::kAvatarCdnHost), 443, ""};
    drpc::TlsTransport tls;
    tls.set_time_budget(kBudgetMs);
    const std::string head = drpc::build_request("GET", url, path, "", "", 0);
    const drpc::HttpResponse response =
        drpc::round_trip(tls, url, head, std::span<const uint8_t>(), kAvatarMaxBytes, int(kBudgetMs));
    if (response.status != 200) return std::nullopt;
    std::vector<uint8_t> png(response.body.begin(), response.body.end());
    if (!is_png(png)) return std::nullopt;
    return png;
}

#else

// Desktop builds have no TLS transport; NXDRPC_AVATAR_FILE stands in for the CDN.
std::optional<std::vector<uint8_t>> download_avatar(std::string_view) {
    const char* file = std::getenv("NXDRPC_AVATAR_FILE");
    if (!file || !*file) return std::nullopt;
    FILE* f = std::fopen(file, "rb");
    if (!f) return std::nullopt;
    std::vector<uint8_t> png(kAvatarMaxBytes + 1);
    const size_t n = std::fread(png.data(), 1, png.size(), f);
    std::fclose(f);
    if (n > kAvatarMaxBytes) return std::nullopt;
    png.resize(n);
    if (!is_png(png)) return std::nullopt;
    return png;
}

#endif

}  // namespace app
