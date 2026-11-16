#pragma once

#include <cstdint>

namespace drpc {

enum class PowerAction { CloseThenAck, Ack, WakeThenAck };

// psc_state holds a PscPmState value.
constexpr PowerAction power_action(uint32_t psc_state) {
    switch (psc_state) {
        case 2:  // ReadySleep
        case 5:  // ReadyShutdown
            return PowerAction::CloseThenAck;
        case 3:  // ReadySleepCritical
            return PowerAction::Ack;
        default:
            // Waking goes through several states; any of them means the network may come back.
            return PowerAction::WakeThenAck;
    }
}

}  // namespace drpc
