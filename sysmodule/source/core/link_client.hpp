#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "drpc/protocol.hpp"

namespace drpc {

struct LinkRequest {
    enum class Kind { Start, Poll, Unlink, FetchMe } kind;
    std::string link_id;  // Poll only
};

// status 0: network error.
struct LinkResponse {
    int status = 0;
    std::string body;
    std::optional<int> retry_after_s;
};

struct LinkEffect {
    std::optional<std::string> api_key;
    std::optional<std::string> username;
    bool clear_key = false;
};

struct LinkView {
    ipc::LinkPhase phase;
    ipc::LinkFailure failure;
    int64_t seconds_left;
    int retry_after_s;
    std::string user_code, verification_uri, username;
};

// Drives the relay's device flow and account calls. No I/O; time is a monotonic now_ms.
class LinkClient {
public:
    // Ignored while Starting or Pending.
    void start(int64_t now_ms);
    // Starting/Pending -> Idle; no more link requests.
    void cancel();
    // Ignored while an unlink is in flight.
    void unlink();
    // Starting/Pending -> Failed(Interrupted).
    void on_sleep();
    // One FetchMe, unless one is already queued or in flight.
    void request_username();
    // At most once per tick, one request in flight: answer it with on_response() before calling next() again.
    std::optional<LinkRequest> next(int64_t now_ms);
    LinkEffect on_response(const LinkRequest& req, const LinkResponse& resp, int64_t now_ms);
    LinkView view(int64_t now_ms) const;
    ipc::UnlinkResult unlink_result() const { return unlink_result_; }

private:
    void on_start(const LinkResponse& resp, int64_t now_ms);
    LinkEffect on_poll(const LinkResponse& resp, int64_t now_ms);
    LinkEffect on_unlink(const LinkResponse& resp);
    LinkEffect on_fetch_me(const LinkResponse& resp);
    void fail(ipc::LinkFailure failure, int retry_after_s = 0);
    void finish(ipc::LinkPhase phase);
    bool expired(int64_t now_ms) const;

    ipc::LinkPhase phase_ = ipc::LinkPhase::Idle;
    ipc::LinkFailure failure_ = ipc::LinkFailure::None;
    int retry_after_s_ = 0;
    bool start_sent_ = false;

    std::string link_id_, user_code_, verification_uri_, username_;
    int64_t interval_ms_ = 0;
    int64_t deadline_ms_ = 0;
    int64_t next_poll_ms_ = 0;

    ipc::UnlinkResult unlink_result_ = ipc::UnlinkResult::None;
    bool unlink_sent_ = false;
    bool fetch_wanted_ = false;
    bool fetch_in_flight_ = false;
};

}  // namespace drpc
