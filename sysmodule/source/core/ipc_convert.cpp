#include "core/ipc_convert.hpp"

#include <cstring>
#include <string>

#include "core/report.hpp"

namespace drpc {

ipc::Settings to_ipc(const Settings& s) {
    ipc::Settings w{};
    w.enabled = s.enabled;
    w.private_mode = s.private_mode;
    w.show_firmware = s.show_firmware;
    w.show_home_menu = s.show_home_menu;
    w.show_paused = s.show_paused;
    w.show_docked = s.show_docked;
    w.show_playtime = s.show_playtime;
    w.debug_log = s.debug_log;
    return w;
}

void apply_ipc(const ipc::Settings& in, Settings& out) {
    out.enabled = in.enabled != 0;
    out.private_mode = in.private_mode != 0;
    out.show_firmware = in.show_firmware != 0;
    out.show_home_menu = in.show_home_menu != 0;
    out.show_paused = in.show_paused != 0;
    out.show_docked = in.show_docked != 0;
    out.show_playtime = in.show_playtime != 0;
    out.debug_log = in.debug_log != 0;
}

void copy_str(char* dst, size_t cap, std::string_view src) {
    if (cap == 0) return;
    const std::string cut = truncate_utf8(src, cap - 1);
    std::memcpy(dst, cut.data(), cut.size());
    dst[cut.size()] = '\0';
}

}  // namespace drpc
