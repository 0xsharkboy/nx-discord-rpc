#pragma once

#include <cstdint>
#include <string>

#include "drpc/protocol.hpp"

namespace app {

enum class Tone { Good, Warning, Bad, Neutral };

enum class Footer { NoUrl, Offline, NoKey, Broken, Unauthorized, Backoff, Ok, OkUnnamed };

struct FooterLine {
    Footer kind;
    Tone tone;
};

// Precedence: an unusable relay URL, then no network, then the link state, then the relay backoff.
FooterLine footer_line(const drpc::ipc::Status& status);

// "mm:ss" below an hour, "h:mm:ss" from an hour; negative values read as zero.
std::string format_elapsed(int64_t seconds);

// "m:ss".
std::string link_countdown(uint32_t seconds);

// Whole minutes, rounded up, at least one.
uint32_t retry_minutes(uint32_t seconds);

}  // namespace app
