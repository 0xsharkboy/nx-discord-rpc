#pragma once

#include <borealis.hpp>

#include "update_manager.hpp"

namespace app {

// Status tab banner: a newer version, the download, and how the install ended. Hidden otherwise.
class UpdateBanner : public brls::Box {
public:
    explicit UpdateBanner(UpdateManager& updater);
    ~UpdateBanner() override;

private:
    void render();

    UpdateManager& updater_;
    brls::Event<>::Subscription subscription_;
    brls::Label* text_;
    brls::Box* track_;
    brls::Box* fill_;
    brls::Button* button_;
};

}  // namespace app
