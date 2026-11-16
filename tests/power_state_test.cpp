#include <doctest/doctest.h>

#include "core/power_state.hpp"

using namespace drpc;

TEST_CASE("sleep and shutdown close the connection before the acknowledgement") {
    CHECK(power_action(2) == PowerAction::CloseThenAck);  // ReadySleep
    CHECK(power_action(5) == PowerAction::CloseThenAck);  // ReadyShutdown
}

TEST_CASE("the critical sleep step keeps the console asleep") {
    CHECK(power_action(3) == PowerAction::Ack);  // ReadySleepCritical
}

TEST_CASE("only the full wake and unknown states count as awake") {
    CHECK(power_action(0) == PowerAction::WakeThenAck);  // Awake
    CHECK(power_action(42) == PowerAction::WakeThenAck);
}

TEST_CASE("intermediate waking steps keep the console asleep") {
    CHECK(power_action(1) == PowerAction::Ack);  // ReadyAwaken, also reached by background wakes
    CHECK(power_action(4) == PowerAction::Ack);  // ReadyAwakenCritical
}
