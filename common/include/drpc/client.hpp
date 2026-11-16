#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "drpc/protocol.hpp"

namespace drpc {

enum class ConnectError { None, NotRunning, VersionMismatch };

class Client {
public:
    virtual ~Client() = default;
    virtual ConnectError connect() = 0;
    virtual uint32_t remote_version() const = 0;
    virtual bool status(ipc::Status&) = 0;
    virtual bool settings(ipc::Settings&) = 0;
    virtual bool set_settings(const ipc::Settings&) = 0;
    virtual bool link(ipc::Link&) = 0;
    virtual bool start_link() = 0;
    virtual bool cancel_link() = 0;
    virtual bool unlink() = 0;
    virtual bool forget_key() = 0;
};

std::unique_ptr<Client> make_switch_client();

// Scenarios: "linked-game", "linked-home", "nokey", "link-flow", "backoff", "missing", "mismatch",
// "link-denied", "link-expired", "link-ratelimited", "link-unreachable", "link-interrupted", "link-cancelled",
// "link-starting", "unauthorized", "broken", "offline", "nourl", "unlink-failed", "linked-unnamed", "long-names".
// Returns nullptr for an unknown scenario.
std::unique_ptr<Client> make_fake_client(std::string_view scenario);

// Overrides the monotonic clock (milliseconds) used by the fake's link-flow countdown; nullptr restores the real one.
void set_fake_clock(int64_t (*now_ms)());

}  // namespace drpc
