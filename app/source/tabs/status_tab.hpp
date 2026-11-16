#pragma once

#include <borealis.hpp>
#include <string>

#include "drpc/client.hpp"

namespace app {

class PresenceCard;

enum class Tone { Good, Warning, Bad, Neutral };

struct FooterLine {
    std::string text;
    Tone tone;
};

FooterLine footer_line(const drpc::ipc::Status& status);
NVGcolor tone_color(Tone tone);

class StatusTab : public brls::Box {
public:
    explicit StatusTab(drpc::Client& client);

private:
    void refresh();

    drpc::Client& client_;
    PresenceCard* card_;
    brls::Box* dot_;
    brls::Label* footer_;
    brls::RepeatingTimer timer_;
};

}  // namespace app
