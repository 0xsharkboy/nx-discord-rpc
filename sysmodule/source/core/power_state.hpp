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
        case 1:  // ReadyAwaken (MinimumAwake)
        case 4:  // ReadyAwakenCritical (EssentialServicesAwake)
            // Background wakes (downloads, notifications) stop here with the display off.
            return PowerAction::Ack;
        default:
            return PowerAction::WakeThenAck;
    }
}

}  // namespace drpc
