#pragma once

#include <borealis.hpp>

#include "update_manager.hpp"

namespace app {

// Status tab banner: a newer version, the download, and how the install ended. Hidden otherwise.
//
// On the blocking screens (sysmodule not running, protocol mismatch) it is always shown, with Check for updates
// besides: an update needs no connection to the sysmodule, and is often what fixes a mismatch.
class UpdateBanner : public brls::Box {
public:
    enum class Place { Status, Blocking };

    explicit UpdateBanner(UpdateManager& updater, Place place = Place::Status);
    ~UpdateBanner() override;

    // Blocking screen only: the sysmodule is not running, so a staged update first needs Start now.
    void set_not_running(bool not_running);

private:
    void render();
    void clicked();

    UpdateManager& updater_;
    Place place_;
    bool not_running_ = false;
    brls::Event<>::Subscription subscription_;
    brls::Label* text_;
    brls::Box* track_;
    brls::Box* fill_;
    brls::Button* button_;
};

}  // namespace app
