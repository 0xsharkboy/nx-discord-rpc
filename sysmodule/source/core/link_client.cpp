#include "core/link_client.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "core/http.hpp"
#include "core/json_extract.hpp"
#include "core/settings.hpp"

namespace drpc {

namespace {

using ipc::LinkFailure;
using ipc::LinkPhase;
using ipc::UnlinkResult;

constexpr int64_t kDefaultExpiresS = 300;
constexpr int64_t kDefaultIntervalS = 5;
constexpr int64_t kMinIntervalS = 2;
constexpr int64_t kMaxRetryAfterS = 3600;

bool is_retryable(int status) { return status == 0 || is_transient_http_status(status); }

std::optional<std::string> string_at(const json::Extractor& e, std::string_view path) {
    const json::Capture* c = e.find(path);
    if (!c || !c->is_string) return std::nullopt;
    return c->value;
}

std::optional<int64_t> int_at(const json::Extractor& e, std::string_view path) {
    const json::Capture* c = e.find(path);
    if (!c || c->is_string) return std::nullopt;
    int64_t v = 0;
    auto [end, ec] = std::from_chars(c->value.data(), c->value.data() + c->value.size(), v);
    if (ec != std::errc{} || end != c->value.data() + c->value.size()) return std::nullopt;
    return v;
}

}  // namespace

void LinkClient::start(int64_t) {
    if (phase_ == LinkPhase::Starting || phase_ == LinkPhase::Pending) return;
    finish(LinkPhase::Starting);
    username_.clear();
}

void LinkClient::cancel() {
    if (phase_ == LinkPhase::Starting || phase_ == LinkPhase::Pending) finish(LinkPhase::Idle);
}

void LinkClient::unlink() {
    if (unlink_result_ == UnlinkResult::InProgress) return;
    unlink_result_ = UnlinkResult::InProgress;
    unlink_sent_ = false;
}

void LinkClient::on_sleep() {
    if (phase_ == LinkPhase::Starting || phase_ == LinkPhase::Pending) fail(LinkFailure::Interrupted);
}

void LinkClient::request_username() {
    if (!fetch_in_flight_) fetch_wanted_ = true;
}

std::optional<LinkRequest> LinkClient::next(int64_t now_ms) {
    if (unlink_result_ == UnlinkResult::InProgress && !unlink_sent_) {
        unlink_sent_ = true;
        return LinkRequest{LinkRequest::Kind::Unlink, {}};
    }
    if (phase_ == LinkPhase::Pending && expired(now_ms)) finish(LinkPhase::Expired);
    if (phase_ == LinkPhase::Starting && !start_sent_) {
        start_sent_ = true;
        return LinkRequest{LinkRequest::Kind::Start, {}};
    }
    if (phase_ == LinkPhase::Pending && now_ms >= next_poll_ms_) {
        return LinkRequest{LinkRequest::Kind::Poll, link_id_};
    }
    if (fetch_wanted_ && !fetch_in_flight_) {
        fetch_wanted_ = false;
        fetch_in_flight_ = true;
        return LinkRequest{LinkRequest::Kind::FetchMe, {}};
    }
    return std::nullopt;
}

LinkEffect LinkClient::on_response(const LinkRequest& req, const LinkResponse& resp, int64_t now_ms) {
    switch (req.kind) {
        case LinkRequest::Kind::Start:
            if (phase_ == LinkPhase::Starting && start_sent_) on_start(resp, now_ms);
            return {};
        case LinkRequest::Kind::Poll:
            if (phase_ == LinkPhase::Pending && req.link_id == link_id_) return on_poll(resp, now_ms);
            return {};
        case LinkRequest::Kind::Unlink:
            return on_unlink(resp);
        case LinkRequest::Kind::FetchMe:
            return on_fetch_me(resp);
    }
    return {};
}

void LinkClient::on_start(const LinkResponse& resp, int64_t now_ms) {
    if (resp.status == 429) {
        fail(LinkFailure::RateLimited, static_cast<int>(std::clamp<int64_t>(resp.retry_after_s.value_or(0), 0,
                                                                            kMaxRetryAfterS)));
        return;
    }
    if (is_retryable(resp.status)) {
        fail(LinkFailure::Unreachable);
        return;
    }
    json::Extractor e({"link_id", "user_code", "verification_uri_complete", "expires_in", "interval"});
    e.feed(resp.body);
    bool parsed = e.finish();
    auto link_id = string_at(e, "link_id");
    auto user_code = string_at(e, "user_code");
    auto uri = string_at(e, "verification_uri_complete");
    if (resp.status != 200 || !parsed || !link_id || link_id->empty() || !user_code || user_code->empty() || !uri ||
        uri->empty()) {
        fail(LinkFailure::Other);
        return;
    }
    int64_t expires_s = int_at(e, "expires_in").value_or(kDefaultExpiresS);
    if (expires_s <= 0) expires_s = kDefaultExpiresS;
    int64_t interval_s = std::max(int_at(e, "interval").value_or(kDefaultIntervalS), kMinIntervalS);

    phase_ = LinkPhase::Pending;
    link_id_ = std::move(*link_id);
    user_code_ = std::move(*user_code);
    verification_uri_ = std::move(*uri);
    interval_ms_ = interval_s * 1000;
    deadline_ms_ = now_ms + expires_s * 1000;
    next_poll_ms_ = now_ms + interval_ms_;
}

LinkEffect LinkClient::on_poll(const LinkResponse& resp, int64_t now_ms) {
    if (is_retryable(resp.status)) {
        int64_t wait_ms = interval_ms_;
        if (resp.retry_after_s) {
            wait_ms = std::max(wait_ms, std::clamp<int64_t>(*resp.retry_after_s, 0, kMaxRetryAfterS) * 1000);
        }
        next_poll_ms_ = now_ms + wait_ms;
        return {};
    }
    json::Extractor e({"state", "api_key", "username"});
    e.feed(resp.body);
    bool parsed = e.finish();
    auto state = string_at(e, "state");
    if (resp.status != 200 || !parsed || !state) {
        fail(LinkFailure::Other);
        return {};
    }
    if (*state == "pending") {
        next_poll_ms_ = now_ms + interval_ms_;
        return {};
    }
    if (*state == "denied") {
        finish(LinkPhase::Denied);
        return {};
    }
    if (*state == "expired") {
        finish(LinkPhase::Expired);
        return {};
    }
    if (*state == "linked") {
        auto key = normalize_api_key(string_at(e, "api_key").value_or(""));
        if (!key) {
            fail(LinkFailure::Other);
            return {};
        }
        std::string username = normalize_username(string_at(e, "username").value_or(""));
        finish(LinkPhase::Linked);
        username_ = username;
        return LinkEffect{std::move(*key), std::move(username), false};
    }
    fail(LinkFailure::Other);
    return {};
}

LinkEffect LinkClient::on_unlink(const LinkResponse& resp) {
    unlink_sent_ = false;
    if (resp.status == 204 || resp.status == 401) {
        unlink_result_ = UnlinkResult::Done;
        if (phase_ == LinkPhase::Linked) finish(LinkPhase::Idle);
        return LinkEffect{std::nullopt, std::nullopt, true};
    }
    unlink_result_ = UnlinkResult::Failed;
    return {};
}

LinkEffect LinkClient::on_fetch_me(const LinkResponse& resp) {
    fetch_in_flight_ = false;
    if (resp.status != 200) return {};
    json::Extractor e({"username"});
    e.feed(resp.body);
    if (!e.finish()) return {};
    auto raw = string_at(e, "username");
    if (!raw) return {};
    std::string username = normalize_username(*raw);
    if (username.empty()) return {};
    if (phase_ == LinkPhase::Linked) username_ = username;
    return LinkEffect{std::nullopt, std::move(username), false};
}

void LinkClient::fail(LinkFailure failure, int retry_after_s) {
    finish(LinkPhase::Failed);
    failure_ = failure;
    retry_after_s_ = retry_after_s;
}

void LinkClient::finish(LinkPhase phase) {
    phase_ = phase;
    failure_ = LinkFailure::None;
    retry_after_s_ = 0;
    start_sent_ = false;
    link_id_.clear();
    user_code_.clear();
    verification_uri_.clear();
    if (phase != LinkPhase::Linked) username_.clear();
    interval_ms_ = deadline_ms_ = next_poll_ms_ = 0;
}

bool LinkClient::expired(int64_t now_ms) const { return now_ms >= deadline_ms_; }

LinkView LinkClient::view(int64_t now_ms) const {
    if (phase_ == LinkPhase::Pending && expired(now_ms)) {
        return LinkView{LinkPhase::Expired, LinkFailure::None, 0, 0, {}, {}, {}};
    }
    int64_t seconds_left = phase_ == LinkPhase::Pending ? (deadline_ms_ - now_ms + 999) / 1000 : 0;
    return LinkView{phase_, failure_, seconds_left, retry_after_s_, user_code_, verification_uri_, username_};
}

}  // namespace drpc
