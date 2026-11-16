#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "drpc/client.hpp"

namespace app {

class SettingsTab : public brls::ScrollingFrame {
public:
    explicit SettingsTab(drpc::Client& client);

private:
    using Field = uint8_t drpc::ipc::Settings::*;

    void add_toggle(brls::Box* list, const std::string& title, Field field);
    void apply(brls::BooleanCell* cell, Field field, bool on);
    void load();
    void sync();

    drpc::Client& client_;
    drpc::ipc::Settings settings_{};
    bool loaded_ = false;
    std::vector<std::pair<brls::BooleanCell*, Field>> toggles_;
    brls::RepeatingTimer refresh_;
};

}  // namespace app
