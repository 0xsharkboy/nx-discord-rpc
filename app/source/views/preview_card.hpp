#pragma once

#include <borealis.hpp>
#include <chrono>
#include <cstdint>
#include <string>

#include "drpc/protocol.hpp"

namespace app {

// What Discord shows for this console: the game icon (read locally from the title data), name, details and an
// elapsed timer counted while the app is open. A masked game shows the console artwork, never its own icon.
class PreviewCard : public brls::Box {
public:
    PreviewCard();

    void update(const drpc::ipc::Status& status);
    void tick();

private:
    enum class Art { Logo, Idle, Icon };

    void show_art(bool shown, const std::string& title_id);
    void set_art(Art art);

    brls::Image* logo_;
    brls::Image* idle_;
    brls::Image* icon_;
    brls::Label* name_;
    brls::Label* nothing_;
    brls::Label* details_;
    brls::Label* timer_;
    std::string icon_title_;
    bool icon_ok_ = false;
    int64_t elapsed_s_ = -1;
    std::chrono::steady_clock::time_point fetched_at_;
};

}  // namespace app
