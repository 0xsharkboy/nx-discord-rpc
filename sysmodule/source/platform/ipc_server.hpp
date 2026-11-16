#pragma once

#include <switch.h>

#include "core/shared_state.hpp"

namespace drpc {

// Registers the drpc service and serves it from its own thread. The thread
// only touches SharedState: no logging, SD or network.
bool ipc_server_start(SharedState& state);
void ipc_server_stop();
// Result code of the last server failure since the previous call, 0 if none.
Result ipc_server_take_error();

}  // namespace drpc
