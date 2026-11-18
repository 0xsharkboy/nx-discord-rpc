#pragma once

#include <string>

namespace app {

// Tells the sysmodule the app is open, so it never swaps in a staged update of the .nro under it (see
// core/nro_swap.hpp). beat() writes the clock now; the app calls it at start and every
// nro_swap::kHeartbeatMs, and stop() removes the file on a normal exit.
class Heartbeat {
public:
    Heartbeat();
    ~Heartbeat() { stop(); }
    Heartbeat(const Heartbeat&) = delete;
    Heartbeat& operator=(const Heartbeat&) = delete;

    void beat();
    void stop();

private:
    std::string path_;
};

}  // namespace app
