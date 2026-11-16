#pragma once

#include <optional>
#include <string>

#include "core/settings.hpp"

namespace drpc {

class ConfigStore {
public:
    Settings load();
    bool save(const Settings& s);
    // A valid key from api_key.txt; an invalid file is deleted with a warning.
    std::optional<std::string> import_api_key_file();
    void delete_api_key_file();
};

}  // namespace drpc
