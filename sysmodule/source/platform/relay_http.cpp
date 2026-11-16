#include "platform/relay_http.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/http.hpp"
#include "platform/icon_source.hpp"
#include "platform/log.hpp"
#include "platform/tls_transport.hpp"

namespace drpc {
namespace {

// The request runs on the main loop; it must stay well below the ~41 s Gateway heartbeat interval.
constexpr int64_t kBudgetMs = 8'000;

bool parse_title_id(std::string_view s, uint64_t& out) {
    if (s.size() != 16) return false;
    uint64_t v = 0;
    for (char c : s) {
        unsigned d;
        if (c >= '0' && c <= '9') d = unsigned(c - '0');
        else if (c >= 'A' && c <= 'F') d = unsigned(c - 'A') + 10;
        else if (c >= 'a' && c <= 'f') d = unsigned(c - 'a') + 10;
        else return false;
        v = (v << 4) | d;
    }
    out = v;
    return true;
}

Outcome network_error(std::string_view what) {
    log::debug(std::string{"Relay request: "} + std::string{what});
    return Outcome{0, std::nullopt};
}

}  // namespace

Outcome execute(const RelayUrl& url, const Request& req, std::string_view api_key, std::string_view admin_key) {
    std::string_view method, content_type;
    std::string_view bearer = api_key;
    std::string_view body_text;
    std::string path;
    std::optional<std::vector<uint8_t>> icon;

    switch (req.kind) {
        case Request::Kind::PutPresence:
            method = "PUT";
            path = "/v1/presence";
            content_type = "application/json";
            body_text = req.body;
            break;
        case Request::Kind::DeletePresence:
            method = "DELETE";
            path = "/v1/presence";
            break;
        case Request::Kind::PutIcon: {
            uint64_t tid = 0;
            if (!parse_title_id(req.title_id, tid)) return Outcome{-1, std::nullopt};
            icon = load_icon(tid);
            if (!icon) return Outcome{-1, std::nullopt};
            method = "PUT";
            path = "/v1/icons/" + req.title_id;
            content_type = "image/jpeg";
            if (req.use_admin_key) bearer = admin_key;
            break;
        }
    }

    const std::span<const uint8_t> body =
        icon ? std::span<const uint8_t>{*icon}
             : std::span<const uint8_t>{reinterpret_cast<const uint8_t*>(body_text.data()), body_text.size()};
    const std::string head = build_request(method, url, path, bearer, content_type, body.size());

    TlsTransport tls;
    tls.set_time_budget(kBudgetMs);
    if (!tls.connect(url.host, url.port)) return network_error("connection failed");
    if (!tls.write_all({reinterpret_cast<const uint8_t*>(head.data()), head.size()})) return network_error("write failed");
    if (!body.empty() && !tls.write_all(body)) return network_error("write failed");

    HttpResponseParser parser;
    std::array<uint8_t, 1024> buf;
    while (!parser.done() && !parser.failed()) {
        const int n = tls.read(buf, int(kBudgetMs));
        if (n <= 0) break;
        parser.feed({reinterpret_cast<const char*>(buf.data()), size_t(n)});
    }
    tls.close();
    if (!parser.done()) return network_error(parser.failed() ? "invalid response" : "no response in time");

    const HttpResponse response = parser.take();
    log::debug("Relay request: HTTP " + std::to_string(response.status));
    return Outcome{response.status, response.retry_after_s};
}

}  // namespace drpc
