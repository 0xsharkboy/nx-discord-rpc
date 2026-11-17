#pragma once

#include <switch.h>

#include <optional>
#include <span>

namespace drpc {

struct InitFailure {
    const char* service;
    Result rc;
};

// Set when a service other than sm/fs failed in __appInit; main() logs it and exits cleanly.
std::optional<InitFailure> init_failure();

// Services the sysmodule runs without; each one only feeds an optional detail.
enum class OptionalService { PdmQuery, Account, Apm, Count };

bool service_up(OptionalService service);

// The optional services that failed to start, for main() to log once.
std::span<const InitFailure> optional_init_failures();

}  // namespace drpc
