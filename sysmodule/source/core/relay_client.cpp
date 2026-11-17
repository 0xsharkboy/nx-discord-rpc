#include "core/relay_client.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "core/http.hpp"
#include "core/json_extract.hpp"

namespace drpc {

namespace {

constexpr int64_t kHeartbeatMs = 60000;
constexpr int64_t kRelayTimeoutMs = 150000;
constexpr int64_t kBackoffMaxMs = 300000;
constexpr int64_t kRetryAfterMaxS = 3600;
constexpr int kIconMaxRateLimited = 3;
// After an upload success, a need_icon is honoured again only after this long.
constexpr int64_t kIconReuploadMs = 600000;
// Upload retries after a network error or a 5xx; the last one repeats.
constexpr int64_t kIconRetryMs[] = {60000, 300000, 900000};
constexpr int kIconRetrySteps = int(std::size(kIconRetryMs));

bool is_retryable(int status) { return status == 0 || is_transient_http_status(status); }
bool is_success(int status) { return status >= 200 && status < 300; }

}  // namespace

std::optional<bool> parse_need_icon(std::string_view body) {
    json::Extractor e({"need_icon"}, 8);
    e.feed(body);
    if (!e.finish()) return std::nullopt;
    const json::Capture* c = e.find("need_icon");
    if (!c || c->is_string) return std::nullopt;
    if (c->value == "true") return true;
    if (c->value == "false") return false;
    return std::nullopt;
}

void RelayClient::Backoff::fail(const Outcome& out, int64_t now_ms) {
    int64_t window_ms = delay_ms;
    if (out.retry_after_s) {
        window_ms = std::max(window_ms, std::clamp<int64_t>(*out.retry_after_s, 0, kRetryAfterMaxS) * 1000);
    }
    until_ms = now_ms + window_ms;
    delay_ms = std::min(delay_ms * 2, kBackoffMaxMs);
}

void RelayClient::set_key(bool has_api_key) {
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
            return Request{Request::Kind::PutPresence, report_json(*desired_, elapsed_s_), {}};
        }
    } else if (shown_) {
        if (last_put_204_ms_ && now_ms - *last_put_204_ms_ >= kRelayTimeoutMs) {
            forget_shown();
        } else {
            return Request{Request::Kind::DeletePresence, {}, {}};
        }
    }

    // Only the title the desired report names: no upload for a title no longer shown, nor in private
    // mode, on the HOME Menu or when disabled. Others stay queued with their state for when they come back.
    if (icon_backoff_.open(now_ms) || !desired_ || !desired_->title_id) return std::nullopt;
    const std::string& shown = *desired_->title_id;
    if (std::find(icon_queue_.begin(), icon_queue_.end(), shown) == icon_queue_.end()) return std::nullopt;
    if (const auto it = icons_.find(shown); it != icons_.end() && now_ms < it->second.retry_ms) return std::nullopt;
    return Request{Request::Kind::PutIcon, {}, shown};
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
    if (!sent_ || !sent_->title_id) return;
    const std::string& title_id = *sent_->title_id;
    if (out.need_icon) {
        if (*out.need_icon) on_need_icon(title_id, now_ms);
    } else if (out.status == 204 && sent_->icon && icon_seen_.insert(title_id).second) {
        // A relay older than v0.4.0 ignores the hash: one upload per title per session.
        if (!icons_[title_id].stopped) queue_icon(title_id);
    }
}

void RelayClient::on_need_icon(const std::string& title_id, int64_t now_ms) {
    const TitleIcon& icon = icons_[title_id];
    if (icon.stopped) return;
    if (icon.uploaded_ms && now_ms - *icon.uploaded_ms < kIconReuploadMs) return;
    queue_icon(title_id);
}

void RelayClient::queue_icon(const std::string& title_id) {
    if (std::find(icon_queue_.begin(), icon_queue_.end(), title_id) == icon_queue_.end()) {
        icon_queue_.push_back(title_id);
    }
}

void RelayClient::unqueue_icon(const std::string& title_id) {
    icon_queue_.erase(std::remove(icon_queue_.begin(), icon_queue_.end(), title_id), icon_queue_.end());
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
    // No queue check: an outcome that lands after the title left the screen still counts for it.
    if (out.status == 401) {
        link_ = LinkState::Unauthorized;
        return;
    }
    if (out.status == 429) {
        icon_backoff_.fail(out, now_ms);
        if (++icon_attempts_ >= kIconMaxRateLimited) {
            icon_attempts_ = 0;
            unqueue_icon(req.title_id);
        }
        return;
    }
    icon_attempts_ = 0;
    icon_backoff_.reset();

    TitleIcon& icon = icons_[req.title_id];
    if (is_retryable(out.status)) {
        // Network error or 5xx (503 storage_full included): the title stays queued until its window ends.
        icon.retry_ms = now_ms + kIconRetryMs[std::min(icon.failures, kIconRetrySteps - 1)];
        ++icon.failures;
        return;
    }
    if (is_success(out.status)) {
        icon.uploaded_ms = now_ms;
        icon.failures = 0;
        icon.retry_ms = 0;
    } else {
        // 400 invalid_icon or blocked, 413 too_large, -1 no icon on the console, or any other final answer.
        icon.stopped = true;
    }
    unqueue_icon(req.title_id);
}

void RelayClient::forget_shown() {
    shown_ = false;
    acked_.reset();
    heartbeat_from_ms_.reset();
}

}  // namespace drpc
