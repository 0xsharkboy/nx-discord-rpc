#include "core/shared_state.hpp"

namespace drpc {

namespace {

bool is_request(ipc::Cmd cmd) {
    switch (cmd) {
        case ipc::Cmd::StartLink:
        case ipc::Cmd::CancelLink:
        case ipc::Cmd::Unlink:
        case ipc::Cmd::ForgetKey:
            return true;
        default:
            return false;
    }
}

}  // namespace

void SharedState::publish(const ipc::Status& status, const ipc::Link& link) {
    std::lock_guard lock(mutex_);
    status_ = status;
    link_ = link;
    for (size_t i = 0; i < kCmdSlots; ++i) {
        if (requests_[i]) show_request(static_cast<ipc::Cmd>(i));
    }
}

ipc::Status SharedState::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

ipc::Link SharedState::link() const {
    std::lock_guard lock(mutex_);
    return link_;
}

void SharedState::set_settings(const ipc::Settings& settings) {
    std::lock_guard lock(mutex_);
    settings_ = settings;
    settings_pending_ = true;
}

void SharedState::seed_settings(const ipc::Settings& settings) {
    std::lock_guard lock(mutex_);
    if (!settings_pending_) settings_ = settings;
}

ipc::Settings SharedState::settings() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

std::optional<ipc::Settings> SharedState::take_settings() {
    std::lock_guard lock(mutex_);
    if (!settings_pending_) return std::nullopt;
    settings_pending_ = false;
    return settings_;
}

void SharedState::request(ipc::Cmd cmd) {
    if (!is_request(cmd)) return;
    std::lock_guard lock(mutex_);
    requests_[static_cast<size_t>(cmd)] = true;
    show_request(cmd);
}

void SharedState::show_request(ipc::Cmd cmd) {
    const bool linking = link_.phase == ipc::LinkPhase::Starting || link_.phase == ipc::LinkPhase::Pending;
    switch (cmd) {
        case ipc::Cmd::StartLink:
            if (!linking) {
                link_ = ipc::Link{};
                link_.phase = ipc::LinkPhase::Starting;
            }
            break;
        case ipc::Cmd::CancelLink:
            if (linking) link_ = ipc::Link{};
            break;
        case ipc::Cmd::Unlink:
            status_.unlink = ipc::UnlinkResult::InProgress;
            break;
        case ipc::Cmd::ForgetKey:
            status_.link = ipc::LinkState::NoKey;
            status_.username[0] = '\0';
            status_.unlink = ipc::UnlinkResult::None;
            link_ = ipc::Link{};
            break;
        default:
            break;
    }
}

bool SharedState::take_request(ipc::Cmd cmd) {
    if (!is_request(cmd)) return false;
    std::lock_guard lock(mutex_);
    const bool pending = requests_[static_cast<size_t>(cmd)];
    requests_[static_cast<size_t>(cmd)] = false;
    return pending;
}

}  // namespace drpc
