#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

#include "drpc/protocol.hpp"

namespace drpc {

// Shared between the main loop and the IPC thread. Every method takes one
// mutex for a copy and does no I/O, so neither side can stall the other.
class SharedState {
public:
    void publish(const ipc::Status& status, const ipc::Link& link);
    ipc::Status status() const;
    ipc::Link link() const;

    void set_settings(const ipc::Settings& settings);
    ipc::Settings settings() const;
    // The latest value from set_settings not yet taken, once.
    std::optional<ipc::Settings> take_settings();

    // Only StartLink, CancelLink, Unlink and ForgetKey; anything else is ignored.
    // Repeated requests coalesce until taken.
    void request(ipc::Cmd cmd);
    bool take_request(ipc::Cmd cmd);

private:
    static constexpr size_t kCmdSlots = static_cast<size_t>(ipc::Cmd::ForgetKey) + 1;

    mutable std::mutex mutex_;
    ipc::Status status_{};
    ipc::Link link_{};
    ipc::Settings settings_{};
    bool settings_pending_ = false;
    std::array<bool, kCmdSlots> requests_{};
};

}  // namespace drpc
