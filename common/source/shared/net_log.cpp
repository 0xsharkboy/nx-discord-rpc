#include "drpc/net_log.hpp"

namespace drpc {
namespace {

NetLogFn g_hook = nullptr;

}  // namespace

void set_net_log(NetLogFn fn) { g_hook = fn; }

void net_log(std::string_view message) {
    if (g_hook) g_hook(message);
}

}  // namespace drpc
