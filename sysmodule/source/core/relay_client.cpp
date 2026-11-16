#include "core/relay_client.hpp"

#include <algorithm>
#include <utility>

#include "core/http.hpp"

namespace drpc {

namespace {

constexpr int64_t kHeartbeatMs = 60000;
constexpr int64_t kRelayTimeoutMs = 150000;
constexpr int64_t kBackoffMaxMs = 300000;
constexpr int64_t kRetryAfterMaxS = 3600;
constexpr int kIconMaxAttempts = 3;

bool is_retryable(int status) { return status == 0 || is_transient_http_status(status); }
bool is_success(int status) { return status >= 200 && status < 300; }

}  // namespace

void RelayClient::Backoff::fail(const Outcome& out, int64_t now_ms) {
    int64_t window_ms = delay_ms;
    if (out.retry_after_s) {
        window_ms = std::max(window_ms, std::clamp<int64_t>(*out.retry_after_s, 0, kRetryAfterMaxS) * 1000);
    }
    until_ms = now_ms + window_ms;
    delay_ms = std::min(delay_ms * 2, kBackoffMaxMs);
}

void RelayClient::set_key(bool has_api_key, bool has_admin_key) {
    has_admin_key_ = has_admin_key;
    if (!has_api_key) {
        link_ = LinkState::NoKey;
        return;
    }
    link_ = LinkState::Ok;
    backoff_.reset();
    icon_backoff_.reset();
    wake_pending_ = true;
}

void RelayClient::set_report(std::optional<Report> report, std::optional<int64_t> elapsed_s) {
    desired_ = std::move(report);
    elapsed_s_ = elapsed_s;
}

void RelayClient::on_wake() {
    wake_pending_ = true;
    backoff_.reset();
}

std::optional<int64_t> RelayClient::backoff_left_ms(int64_t now_ms) const {
    if (!backoff_.open(now_ms)) return std::nullopt;
    return backoff_.until_ms - now_ms;
}

std::optional<Request> RelayClient::next(int64_t now_ms) {
    if (link_ != LinkState::Ok || backoff_.open(now_ms)) return std::nullopt;

    if (desired_) {
        bool due = wake_pending_ || !acked_ || *acked_ != *desired_ || !heartbeat_from_ms_ ||
                   now_ms - *heartbeat_from_ms_ >= kHeartbeatMs;
        if (due) {
            sent_ = desired_;
            return Request{Request::Kind::PutPresence, report_json(*desired_, elapsed_s_), {}, false};
        }
    } else if (shown_) {
        if (last_put_204_ms_ && now_ms - *last_put_204_ms_ >= kRelayTimeoutMs) {
            forget_shown();
        } else {
            return Request{Request::Kind::DeletePresence, {}, {}, false};
        }
    }

    if (!icon_queue_.empty() && !icon_backoff_.open(now_ms)) {
        return Request{Request::Kind::PutIcon, {}, icon_queue_.front(),
                       has_admin_key_ && !admin_key_dropped_};
    }
    return std::nullopt;
}

void RelayClient::on_outcome(const Request& req, const Outcome& out, int64_t now_ms) {
    switch (req.kind) {
        case Request::Kind::PutPresence:
            on_presence_outcome(out, now_ms);
            break;
        case Request::Kind::DeletePresence:
            on_delete_outcome(out, now_ms);
            break;
        case Request::Kind::PutIcon:
            on_icon_outcome(req, out, now_ms);
            break;
    }
}

void RelayClient::on_presence_outcome(const Outcome& out, int64_t now_ms) {
    if (is_retryable(out.status)) {
        backoff_.fail(out, now_ms);
        return;
    }
    backoff_.reset();
    if (out.status == 401) {
        link_ = LinkState::Unauthorized;
        return;
    }
    if (out.status == 409) {
        link_ = LinkState::LinkBroken;
        return;
    }
    acked_ = sent_;
    heartbeat_from_ms_ = now_ms;
    wake_pending_ = false;
    if (!is_success(out.status)) return;

    last_put_204_ms_ = now_ms;
    shown_ = true;
    if (sent_ && sent_->title_id && icon_seen_.insert(*sent_->title_id).second) {
        icon_queue_.push_back(*sent_->title_id);
    }
}

void RelayClient::on_delete_outcome(const Outcome& out, int64_t now_ms) {
    if (is_retryable(out.status)) {
        backoff_.fail(out, now_ms);
        return;
    }
    backoff_.reset();
    if (out.status == 401) {
        link_ = LinkState::Unauthorized;
    } else if (out.status == 409) {
        link_ = LinkState::LinkBroken;
    } else {
        forget_shown();
    }
}

void RelayClient::on_icon_outcome(const Request& req, const Outcome& out, int64_t now_ms) {
    if (icon_queue_.empty() || icon_queue_.front() != req.title_id) return;
    if (out.status == 401) {
        if (req.use_admin_key) {
            admin_key_dropped_ = true;
        } else {
            link_ = LinkState::Unauthorized;
        }
        return;
    }
    if (is_retryable(out.status)) {
        icon_backoff_.fail(out, now_ms);
        if (++icon_attempts_ >= kIconMaxAttempts) pop_icon();
        return;
    }
    // 409 included: link_broken accounts can still vote for icons.
    icon_backoff_.reset();
    pop_icon();
}

void RelayClient::forget_shown() {
    shown_ = false;
    acked_.reset();
    heartbeat_from_ms_.reset();
}

void RelayClient::pop_icon() {
    icon_queue_.pop_front();
    icon_attempts_ = 0;
}

}  // namespace drpc
