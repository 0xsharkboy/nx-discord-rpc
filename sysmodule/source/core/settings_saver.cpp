#include "core/settings_saver.hpp"

namespace drpc {

namespace {

constexpr int64_t kQuietMs = 2000;

}  // namespace

void SettingsSaver::changed(int64_t now_ms) {
    pending_ = true;
    last_change_ms_ = now_ms;
}

bool SettingsSaver::due(int64_t now_ms) const { return pending_ && now_ms - last_change_ms_ >= kQuietMs; }

void SettingsSaver::saved() { pending_ = false; }

}  // namespace drpc
