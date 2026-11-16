#pragma once

#include <switch.h>

#include <optional>

namespace drpc {

struct InitFailure {
    const char* service;
    Result rc;
};

// Set when a service other than sm/fs failed in __appInit; main() logs it and exits cleanly.
std::optional<InitFailure> init_failure();

}  // namespace drpc
