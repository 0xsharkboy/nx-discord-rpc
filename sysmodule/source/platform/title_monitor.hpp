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

// nullopt means no application is running (HOME Menu). Names are cached per title id.
std::optional<RunningTitle> current_title();

}  // namespace drpc
