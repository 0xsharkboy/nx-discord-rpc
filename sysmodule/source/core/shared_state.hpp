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
    // Sets the value GetSettings serves without making it pending for take_settings.
    // Ignored while a set_settings value is pending, so it never replaces a newer one.
    void seed_settings(const ipc::Settings& settings);
    ipc::Settings settings() const;
    // The latest value from set_settings not yet taken, once.
    std::optional<ipc::Settings> take_settings();

    // The hidden list, like the settings. Out parameters: the list is about 1 KB, kept off the callers' stacks.
    void set_hidden(const ipc::HiddenList& list);
    // Ignored while a set_hidden value is pending.
    void seed_hidden(const ipc::HiddenList& list);
    void hidden(ipc::HiddenList& out) const;
    // The latest value from set_hidden not yet taken, once; false when there is none.
    bool take_hidden(ipc::HiddenList& out);

    // Only StartLink, CancelLink, Unlink and ForgetKey; anything else is ignored.
    // Status and Link reflect a request from the moment it is made, even across a publish, until it is taken.
    // Repeated requests coalesce until taken.
    void request(ipc::Cmd cmd);
    bool take_request(ipc::Cmd cmd);

private:
    // What a request will do once the main loop takes it, shown at once. Called with mutex_ held.
    void show_request(ipc::Cmd cmd);

    static constexpr size_t kCmdSlots = static_cast<size_t>(ipc::Cmd::ForgetKey) + 1;

    mutable std::mutex mutex_;
    ipc::Status status_{};
    ipc::Link link_{};
    ipc::Settings settings_{};
    bool settings_pending_ = false;
    ipc::HiddenList hidden_{};
    bool hidden_pending_ = false;
    std::array<bool, kCmdSlots> requests_{};
};

}  // namespace drpc
