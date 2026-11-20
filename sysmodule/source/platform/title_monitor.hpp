#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace drpc {

struct RunningTitle {
    uint64_t title_id;
    std::string name;
    std::string english_name;  // empty when the game has no English name
};

// nullopt means no application is running (HOME Menu). title_id is the application: a sub-program of a
// game (program index 1 to 0xFF) reports its parent game. Names are cached per program id.
std::optional<RunningTitle> current_title();

}  // namespace drpc
