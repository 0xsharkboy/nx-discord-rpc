#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "core/report.hpp"

namespace drpc {

struct Request {
    enum class Kind { PutPresence, DeletePresence, PutIcon } kind;
    std::string body;      // PutPresence only
    std::string title_id;  // PutIcon only
};

// status 0: network error; -1: icon unavailable locally (retried like a network error).
struct Outcome {
    int status = 0;
    std::optional<int> retry_after_s;
    std::optional<bool> need_icon;  // PutPresence answered 200 with a boolean need_icon
};

// The boolean need_icon of a 200 PUT /v1/presence answer; nullopt when absent, not a boolean or not JSON.
std::optional<bool> parse_need_icon(std::string_view body);

enum class LinkState { NoKey, Ok, Unauthorized, LinkBroken };

// Decides what to send to the relay and when. No I/O; time is a monotonic now_ms.
class RelayClient {
public:
    // A key change: call on boot and on key import only. Resets Unauthorized/LinkBroken.
    void set_key(bool has_api_key);
    // Desired state; elapsed is computed by the caller and sent with each PUT.
    void set_report(std::optional<Report> report, std::optional<int64_t> elapsed_s);
    // One request in flight: do not call next() again before on_outcome() for its request.
    std::optional<Request> next(int64_t now_ms);
    void on_outcome(const Request& req, const Outcome& out, int64_t now_ms);
    // Re-send at once after sleep, ignoring any open backoff window.
    void on_wake();
    LinkState link_state() const { return link_; }
    // Time left in the presence backoff window, nullopt when it is closed.
    std::optional<int64_t> backoff_left_ms(int64_t now_ms) const;
    // The HTTP status of the last presence PUT or DELETE answered: 0 before any (and after set_key), -1 for a
    // network error. Icon uploads do not count.
    int last_presence_status() const { return last_presence_status_; }

private:
    struct Backoff {
        int64_t until_ms = 0;
        int64_t delay_ms = 5000;
        bool open(int64_t now_ms) const { return now_ms < until_ms; }
        void fail(const Outcome& out, int64_t now_ms);
        void reset() { *this = Backoff{}; }
    };

    // Icon upload state of one title, kept until reboot.
    struct TitleIcon {
        std::optional<int64_t> uploaded_ms;  // last 2xx upload
        bool stopped = false;                // a final rejection: no more uploads this session
        int failures = 0;                    // network errors and 5xx since the last success
        int64_t retry_ms = 0;                // no upload before this instant
    };

    void on_presence_outcome(const Outcome& out, int64_t now_ms);
    void on_delete_outcome(const Outcome& out, int64_t now_ms);
    void on_icon_outcome(const Request& req, const Outcome& out, int64_t now_ms);
    void on_need_icon(const std::string& title_id, int64_t now_ms);
    void queue_icon(const std::string& title_id);
    void unqueue_icon(const std::string& title_id);
    void forget_shown();

    LinkState link_ = LinkState::NoKey;
    int last_presence_status_ = 0;

    std::optional<Report> desired_;
    std::optional<int64_t> elapsed_s_;
    std::optional<Report> sent_;  // the report carried by the last PutPresence handed out
    std::optional<Report> acked_;
    std::optional<int64_t> heartbeat_from_ms_;
    std::optional<int64_t> last_put_204_ms_;
    bool shown_ = false;
    bool wake_pending_ = false;
    Backoff backoff_;

    std::deque<std::string> icon_queue_;  // title ids waiting for an upload, each at most once; sent only while shown
    std::set<std::string> icon_seen_;     // titles answered 204 (no need_icon): one upload per session
    std::map<std::string, TitleIcon> icons_;
    int icon_attempts_ = 0;  // consecutive 429s
    Backoff icon_backoff_;   // 429 only: the upload rate limit is per account
};

}  // namespace drpc
