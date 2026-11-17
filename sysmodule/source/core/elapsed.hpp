#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace drpc {

// Seconds on the current title, minus paused time. The wall clock keeps counting across sleep, like the
// console's play timer; the monotonic clock is the fallback when no wall clock is available.
class Elapsed {
public:
    // Both functions return milliseconds; unix_ms returns 0 when no wall clock is available.
    Elapsed(std::function<int64_t()> unix_ms, std::function<int64_t()> mono_ms)
        : unix_ms_(std::move(unix_ms)), mono_ms_(std::move(mono_ms)) {}

    void restart() {
        paused_ = false;
        start_unix_ms_ = unix_ms_();
        start_mono_ms_ = mono_ms_();
    }

    void pause() {
        if (paused_) return;
        paused_ = true;
        pause_unix_ms_ = unix_ms_();
        pause_mono_ms_ = mono_ms_();
    }

    void resume() {
        if (!paused_) return;
        paused_ = false;
        start_mono_ms_ += mono_ms_() - pause_mono_ms_;
        const int64_t now_unix = unix_ms_();
        if (start_unix_ms_ != 0 && pause_unix_ms_ != 0 && now_unix != 0)
            start_unix_ms_ += now_unix - pause_unix_ms_;
        else
            start_unix_ms_ = 0;  // wall clock unusable across the pause: fall back to monotonic
    }

    bool paused() const { return paused_; }

    int64_t seconds() const {
        const int64_t now_unix = paused_ ? pause_unix_ms_ : unix_ms_();
        const int64_t now_mono = paused_ ? pause_mono_ms_ : mono_ms_();
        if (start_unix_ms_ != 0 && now_unix != 0) return (now_unix - start_unix_ms_) / 1000;
        return (now_mono - start_mono_ms_) / 1000;
    }

private:
    std::function<int64_t()> unix_ms_, mono_ms_;
    bool paused_ = false;
    int64_t start_unix_ms_ = 0, start_mono_ms_ = 0;
    int64_t pause_unix_ms_ = 0, pause_mono_ms_ = 0;
};

}  // namespace drpc
