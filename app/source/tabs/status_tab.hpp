#pragma once

#include <borealis.hpp>

#include "drpc/client.hpp"

namespace app {

class PresenceCard;

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
