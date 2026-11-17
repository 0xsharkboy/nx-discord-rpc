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
        !is_bool(s.show_firmware) || !is_bool(s.show_home_menu) || !is_bool(s.debug_log) ||
        !is_bool(s.show_paused) || !is_bool(s.show_docked) || !is_bool(s.show_playtime)) {
        return {kRcBadInput, {}};
    }
    state.set_settings(s);
    return {0, {}};
}

DispatchResult get_hidden(std::span<uint8_t> out_buf, const SharedState& state) {
    if (out_buf.size() < sizeof(ipc::HiddenList)) return {kRcBadBuffer, {}};
    ipc::HiddenList list;
    state.hidden(list);
    std::memcpy(out_buf.data(), &list, sizeof list);
    return {0, {}};
}

// The buffer is the client's memory: copied once, then only the copy is read.
DispatchResult set_hidden(std::span<const uint8_t> in_buf, SharedState& state) {
    if (in_buf.size() < sizeof(ipc::HiddenList)) return {kRcBadInput, {}};
    ipc::HiddenList in;
    std::memcpy(&in, in_buf.data(), sizeof in);
    if (!ipc::valid_hidden(in)) return {kRcBadInput, {}};
    ipc::HiddenList clean{};
    clean.count = in.count;
    for (uint32_t i = 0; i < in.count; ++i) {
        clean.items[i].title_id = in.items[i].title_id;
        clean.items[i].visibility = in.items[i].visibility;
    }
    state.set_hidden(clean);
    return {0, {}};
}

}  // namespace

DispatchResult dispatch(uint32_t cmd_id, std::span<const uint8_t> in_raw, std::span<const uint8_t> in_buf,
                        std::span<uint8_t> out_buf, SharedState& state) {
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
        case ipc::Cmd::GetHidden:
            return get_hidden(out_buf, state);
        case ipc::Cmd::SetHidden:
            return set_hidden(in_buf, state);
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
