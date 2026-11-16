#pragma once

#include <borealis.hpp>

#include "drpc/client.hpp"

namespace app {

class AccountTab : public brls::Box {
public:
    explicit AccountTab(drpc::Client& client);

private:
    void refresh();
    void confirm_unlink();
    void offer_forget();
    void keep_focus();

    drpc::Client& client_;
    brls::Box* avatar_;
    brls::Label* initial_;
    brls::Label* name_;
    brls::Label* state_;
    brls::Button* link_;
    brls::Button* unlink_;
    bool awaiting_unlink_ = false;
    bool first_refresh_ = true;
    brls::RepeatingTimer timer_;
};

}  // namespace app
