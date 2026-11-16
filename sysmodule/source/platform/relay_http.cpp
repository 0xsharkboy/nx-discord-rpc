#include "platform/relay_http.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/http.hpp"
#include "drpc/tls_transport.hpp"
#include "platform/icon_source.hpp"
#include "platform/log.hpp"

namespace drpc {
namespace {

// The request blocks the main loop.
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

// One request over a new TLS connection. Status 0 on a network error; the body is empty unless complete.
HttpResponse relay_round_trip(const RelayUrl& url, std::string_view head, std::span<const uint8_t> body) {
    TlsTransport tls;
    tls.set_time_budget(kBudgetMs);
    const HttpResponse response = round_trip(tls, url, head, body, kHttpMaxBody, int(kBudgetMs));
    if (response.note) log::debug(std::string{"Relay request: "} + response.note);
    if (response.status != 0) log::debug("Relay request: HTTP " + std::to_string(response.status));
    return response;
}

// The relay's link ids are opaque tokens; anything else must not reach the request line.
bool valid_link_id(std::string_view id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
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
    const HttpResponse response = relay_round_trip(url, head, body);
    return Outcome{response.status, response.retry_after_s};
}

LinkResponse execute_link(const RelayUrl& url, const LinkRequest& req, std::string_view api_key) {
    std::string_view method;
    std::string_view bearer;
    std::string path;

    switch (req.kind) {
        case LinkRequest::Kind::Start:
            method = "POST";
            path = "/v1/link";
            break;
        case LinkRequest::Kind::Poll:
            if (!valid_link_id(req.link_id)) return LinkResponse{-1, {}, std::nullopt};
            method = "POST";
            path = "/v1/link/" + req.link_id + "/poll";
            break;
        case LinkRequest::Kind::Unlink:
            method = "DELETE";
            path = "/v1/me";
            bearer = api_key;
            break;
        case LinkRequest::Kind::FetchMe:
            method = "GET";
            path = "/v1/me";
            bearer = api_key;
            break;
    }

    const std::string head = build_request(method, url, path, bearer, {}, 0);
    HttpResponse response = relay_round_trip(url, head, {});
    return LinkResponse{response.status, std::move(response.body), response.retry_after_s};
}

}  // namespace drpc
