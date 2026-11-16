#pragma once

#include <borealis.hpp>
#include <cstdint>
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
    void apply(Field field, bool on);
    void sync();

    drpc::Client& client_;
    drpc::ipc::Settings settings_{};
    std::vector<std::pair<brls::BooleanCell*, Field>> toggles_;
};

}  // namespace app
