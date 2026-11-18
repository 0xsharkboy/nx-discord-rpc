#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "drpc/client.hpp"
#include "update_manager.hpp"

namespace app {

class SettingsTab : public brls::ScrollingFrame {
public:
    SettingsTab(drpc::Client& client, UpdateManager& updater);
    ~SettingsTab() override;

private:
    using Field = uint8_t drpc::ipc::Settings::*;

    void add_toggle(brls::Box* list, const std::string& title, Field field);
    void add_boot_toggle(brls::Box* list);
    void add_hidden_games(brls::Box* list);
    void add_updates(brls::Box* list);
    void render_update();
    void update_clicked();
    void apply(brls::BooleanCell* cell, Field field, bool on);
    void load();
    void sync();

    drpc::Client& client_;
    drpc::ipc::Settings settings_{};
    bool loaded_ = false;
    std::vector<std::pair<brls::BooleanCell*, Field>> toggles_;
    brls::DetailCell* hidden_games_ = nullptr;
    std::unique_ptr<drpc::ipc::HiddenList> hidden_ = std::make_unique<drpc::ipc::HiddenList>();
    bool hidden_loaded_ = false;
    brls::RepeatingTimer refresh_;
    UpdateManager& updater_;
    brls::Event<>::Subscription update_sub_;
    brls::DetailCell* update_cell_ = nullptr;
    // Set when an action here starts a check or an install: its result then opens a dialog.
    bool awaiting_ = false;
};

}  // namespace app
