#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>

#include "core/report.hpp"

namespace drpc {

struct Request {
    enum class Kind { PutPresence, DeletePresence, PutIcon } kind;
    std::string body;            // PutPresence only
    std::string title_id;        // PutIcon only
    bool use_admin_key = false;  // PutIcon only
};

// status 0: network error; -1: icon unavailable locally.
struct Outcome {
    int status = 0;
    std::optional<int> retry_after_s;
};

enum class LinkState { NoKey, Ok, Unauthorized, LinkBroken };

// Decides what to send to the relay and when. No I/O; time is a monotonic now_ms.
class RelayClient {
public:
    // A new key resets Unauthorized/LinkBroken.
    void set_key(bool has_api_key, bool has_admin_key);
    // Desired state; elapsed is computed by the caller and sent with each PUT.
    void set_report(std::optional<Report> report, std::optional<int64_t> elapsed_s);
    std::optional<Request> next(int64_t now_ms);
    void on_outcome(const Request& req, const Outcome& out, int64_t now_ms);
    // Re-send at once after sleep.
    void on_wake();
    LinkState link_state() const { return link_; }

private:
    struct Backoff {
        int64_t until_ms = 0;
        int64_t delay_ms = 5000;
        bool open(int64_t now_ms) const { return now_ms < until_ms; }
        void fail(const Outcome& out, int64_t now_ms);
        void reset() { *this = Backoff{}; }
    };

    void on_presence_outcome(const Outcome& out, int64_t now_ms);
    void on_delete_outcome(const Outcome& out, int64_t now_ms);
    void on_icon_outcome(const Request& req, const Outcome& out, int64_t now_ms);
    void forget_shown();
    void pop_icon();

    LinkState link_ = LinkState::NoKey;
    bool has_admin_key_ = false;
    bool admin_key_dropped_ = false;

    std::optional<Report> desired_;
    std::optional<int64_t> elapsed_s_;
    std::optional<Report> sent_;  // the report carried by the last PutPresence handed out
    std::optional<Report> acked_;
    std::optional<int64_t> heartbeat_from_ms_;
    std::optional<int64_t> last_put_204_ms_;
    bool shown_ = false;
    bool wake_pending_ = false;
    Backoff backoff_;

    std::deque<std::string> icon_queue_;
    std::set<std::string> icon_seen_;
    int icon_attempts_ = 0;
    Backoff icon_backoff_;
};

}  // namespace drpc
