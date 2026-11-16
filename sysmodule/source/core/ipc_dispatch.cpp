#include "core/ipc_dispatch.hpp"

#include <cstring>

#include "drpc/protocol.hpp"

namespace drpc {

namespace {

template <typename T>
DispatchResult raw(const T& value) {
    DispatchResult r{0, std::vector<uint8_t>(sizeof(T))};
    std::memcpy(r.raw_out.data(), &value, sizeof(T));
    return r;
}

template <typename T>
DispatchResult write_out(const T& value, std::span<uint8_t> out_buf) {
    if (out_buf.size() < sizeof(T)) return {kRcBadBuffer, {}};
    std::memcpy(out_buf.data(), &value, sizeof(T));
    return {0, {}};
}

bool is_bool(uint8_t v) { return v <= 1; }

DispatchResult set_settings(std::span<const uint8_t> in_raw, SharedState& state) {
    ipc::Settings s;
    if (in_raw.size() < sizeof(s)) return {kRcBadInput, {}};
    std::memcpy(&s, in_raw.data(), sizeof(s));
    if (!is_bool(s.enabled) || !is_bool(s.private_mode) ||
        !is_bool(s.show_firmware) || !is_bool(s.show_home_menu) || !is_bool(s.debug_log)) {
        return {kRcBadInput, {}};
    }
    s.reserved0 = 0;
    std::memset(s.reserved, 0, sizeof(s.reserved));
    state.set_settings(s);
    return {0, {}};
}

}  // namespace

DispatchResult dispatch(uint32_t cmd_id, std::span<const uint8_t> in_raw, std::span<uint8_t> out_buf,
                        SharedState& state) {
    const auto cmd = static_cast<ipc::Cmd>(cmd_id);
    switch (cmd) {
        case ipc::Cmd::GetProtocolVersion:
            return raw(ipc::kProtocolVersion);
        case ipc::Cmd::GetStatus:
            return write_out(state.status(), out_buf);
        case ipc::Cmd::GetLink:
            return write_out(state.link(), out_buf);
        case ipc::Cmd::GetSettings:
            return raw(state.settings());
        case ipc::Cmd::SetSettings:
            return set_settings(in_raw, state);
        case ipc::Cmd::StartLink:
        case ipc::Cmd::CancelLink:
        case ipc::Cmd::Unlink:
        case ipc::Cmd::ForgetKey:
            state.request(cmd);
            return {0, {}};
    }
    return {kRcUnknownCommand, {}};
}

}  // namespace drpc
