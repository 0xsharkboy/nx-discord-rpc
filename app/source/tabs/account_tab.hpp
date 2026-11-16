#pragma once

#include <borealis.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "drpc/client.hpp"

namespace app {

class AccountTab : public brls::Box {
public:
    explicit AccountTab(drpc::Client& client);

private:
    void refresh();
    void refresh_photo(const drpc::ipc::Status& status);
    void start_download(const std::string& key, const std::string& path);
    void show_photo(std::span<const uint8_t> png, const std::string& key);
    void hide_photo();
    void confirm_unlink();
    void offer_forget();
    void keep_focus();

    drpc::Client& client_;
    brls::Box* avatar_;
    brls::Label* initial_;
    brls::Image* photo_;
    brls::Label* name_;
    brls::Label* state_;
    brls::Button* link_;
    brls::Button* unlink_;
    bool awaiting_unlink_ = false;
    bool first_refresh_ = true;
    std::string wanted_key_;  // the picture the status asks for; empty when none
    std::string shown_key_;   // the picture on screen; empty when the initial shows
    uint32_t seen_stores_ = 0;
    // Downloads finish on the UI thread, possibly after this tab is gone: they check this first.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    brls::RepeatingTimer timer_;
};

}  // namespace app
