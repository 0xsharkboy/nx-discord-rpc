#pragma once

#include "core/settings.hpp"

namespace drpc {

class ConfigStore {
public:
    Settings load();
    bool save(const Settings& s);
};

}  // namespace drpc
