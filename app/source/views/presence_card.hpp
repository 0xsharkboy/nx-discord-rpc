#pragma once

#include <borealis.hpp>
#include <chrono>
#include <cstdint>
#include <string>

#include "drpc/protocol.hpp"

namespace app {

std::string format_elapsed(int64_t seconds);

class PresenceCard : public brls::Box {
public:
    PresenceCard();

    void update(const drpc::ipc::Status& status);

private:
    void tick();
    void show_icon(const std::string& title_id);

    brls::Image* logo_;
    brls::Image* icon_;
    brls::Label* heading_;
    brls::Label* name_;
    brls::Label* nothing_;
    brls::Label* details_;
    brls::Label* timer_;
    std::string icon_title_;
    int64_t elapsed_s_ = -1;
    std::chrono::steady_clock::time_point fetched_at_;
};

}  // namespace app
