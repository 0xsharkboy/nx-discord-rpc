#pragma once

#include <cstdint>

namespace drpc {

// Spaces out attempts at a fixed interval; the first one is due immediately.
class RetryGate {
public:
    explicit RetryGate(int64_t interval_ms) : interval_ms_(interval_ms) {}

    bool due(int64_t now_ms) const { return !tried_ || now_ms - last_ms_ >= interval_ms_; }
    void attempted(int64_t now_ms) {
        tried_ = true;
        last_ms_ = now_ms;
    }

private:
    int64_t interval_ms_;
    bool tried_ = false;
    int64_t last_ms_ = 0;
};

// Decides which outcomes of a repeated operation deserve a log line.
class FailureLog {
public:
    // True for the first failure and whenever the outcome differs from the last one.
    bool failed(uint32_t outcome) {
        const bool log = !failing_ || outcome != last_;
        failing_ = true;
        last_ = outcome;
        return log;
    }
    // True once, on the first success after a failure.
    bool recovered() {
        const bool log = failing_;
        failing_ = false;
        return log;
    }

private:
    bool failing_ = false;
    uint32_t last_ = 0;
};

}  // namespace drpc
