#include <doctest/doctest.h>

#include "core/retry.hpp"

using drpc::FailureLog;
using drpc::RetryGate;

TEST_CASE("RetryGate: first attempt is due at once, then once per interval") {
    RetryGate gate(5000);
    CHECK(gate.due(0));
    gate.attempted(100);
    CHECK_FALSE(gate.due(100));
    CHECK_FALSE(gate.due(5099));
    CHECK(gate.due(5100));
    gate.attempted(5100);
    CHECK_FALSE(gate.due(5101));
}

TEST_CASE("FailureLog: logs the first failure and each distinct outcome only") {
    FailureLog log;
    CHECK(log.failed(7));
    CHECK_FALSE(log.failed(7));
    CHECK_FALSE(log.failed(7));
    CHECK(log.failed(9));
    CHECK_FALSE(log.failed(9));
    CHECK(log.failed(7));
}

TEST_CASE("FailureLog: recovery is reported once, and a new failure logs again") {
    FailureLog log;
    CHECK_FALSE(log.recovered());
    log.failed(1);
    CHECK(log.recovered());
    CHECK_FALSE(log.recovered());
    CHECK(log.failed(1));
}
