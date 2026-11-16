#include <doctest/doctest.h>

#include "core/version.hpp"

TEST_CASE("kVersion matches the VERSION file") {
    CHECK(drpc::kVersion == NXDRPC_TEST_VERSION);
}
