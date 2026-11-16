#pragma once

#include <cstdint>

namespace drpc {

// Debounces config.ini writes: one save once 2000 ms passed without a change.
class SettingsSaver {
public:
    void changed(int64_t now_ms);
    bool due(int64_t now_ms) const;
    void saved();

private:
    bool pending_ = false;
    int64_t last_change_ms_ = 0;
};

}  // namespace drpc
