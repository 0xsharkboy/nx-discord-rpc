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

// What Discord shows, as far as the console knows it.
struct Preview {
    bool shown = false;      // a presence that reaches Discord: one is set and the account is linked
    std::string icon_title;  // the title id whose icon Discord shows; empty for a masked game or no game
    bool timer = false;      // Discord counts elapsed time (the sysmodule sends none while paused)
};

// Reads only what the sysmodule reports to the relay, never running_title_id: a Private or Hidden game stays masked.
Preview preview_of(const drpc::ipc::Status& status);

// "mm:ss" below an hour, "h:mm:ss" from an hour; negative values read as zero.
std::string format_elapsed(int64_t seconds);

// "m:ss".
std::string link_countdown(uint32_t seconds);

// Whole minutes, rounded up, at least one.
uint32_t retry_minutes(uint32_t seconds);

}  // namespace app
