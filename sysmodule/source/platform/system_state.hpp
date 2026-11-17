#pragma once

#include <cstdint>
#include <optional>

#include "core/focus.hpp"

namespace drpc {

// What holds focus over the running application, from the pdm play event log. nullopt when it cannot tell.
std::optional<Focus> query_focus(uint64_t app_program_id);

// True when docked (apm Boost performance mode); nullopt when apm cannot answer.
std::optional<bool> query_docked();

// Total playtime of the last opened user on this title, in seconds. nullopt when no user or any call fails.
std::optional<uint64_t> query_playtime_s(uint64_t title_id);

}  // namespace drpc
