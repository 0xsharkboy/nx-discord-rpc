#pragma once

#include <borealis.hpp>

#include "drpc/client.hpp"
#include "update_manager.hpp"

namespace app {

class PreviewCard;

class StatusTab : public brls::Box {
public:
    StatusTab(drpc::Client& client, UpdateManager& updater);

private:
    void refresh();

    drpc::Client& client_;
    UpdateManager& updater_;
    PreviewCard* card_;
    brls::Box* dot_;
    brls::Label* footer_;
    brls::RepeatingTimer timer_;
};

}  // namespace app
