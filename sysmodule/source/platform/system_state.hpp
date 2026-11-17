#pragma once

#include <cstdint>
#include <optional>

#include "core/focus.hpp"

namespace drpc {

// What holds focus over the running application, from the pdm play event log. nullopt when it cannot tell.
std::optional<Focus> query_focus(uint64_t app_program_id);

// True when docked (apm Boost performance mode); nullopt when apm cannot answer.
std::optional<bool> query_docked();

// Total playtime of this title on the console (all profiles), in seconds. nullopt when pdm cannot answer.
std::optional<uint64_t> query_playtime_s(uint64_t title_id);

}  // namespace drpc
