#pragma once

#include <string_view>

namespace drpc {

// Diagnostics from the shared network code. Dropped until a hook is installed.
using NetLogFn = void (*)(std::string_view);

void set_net_log(NetLogFn fn);
void net_log(std::string_view message);

}  // namespace drpc
