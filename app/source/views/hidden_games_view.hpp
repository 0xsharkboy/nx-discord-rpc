#pragma once

#include <atomic>
#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "drpc/client.hpp"
#include "game_list.hpp"

namespace app {

// Settings > Hidden games: every installed game with a Shown / Playing a game / Hidden selector.
class HiddenGamesView : public brls::ScrollingFrame {
public:
    explicit HiddenGamesView(drpc::Client& client);
    ~HiddenGamesView() override;

    static void open(drpc::Client& client);

private:
    struct Row {
        uint64_t title_id;
        brls::SelectorCell* cell;
    };

    void load_games();
    void populate(const std::vector<InstalledGame>& games);
    void add_row(uint64_t title_id, const std::string& name, const RgbaIcon& icon);
    void choose(uint64_t title_id, brls::SelectorCell* cell, int selection);
    void refresh();
    void show_pending_message();

    drpc::Client& client_;
    // About 1 KB: kept on the heap, read again before every change so entries not on screen survive.
    std::unique_ptr<drpc::ipc::HiddenList> list_;
    bool loaded_ = false;
    std::string pending_message_;  // shown once the selector's dropdown has closed
    std::vector<Row> rows_;

    brls::Box* content_;
    brls::Box* status_;  // focusable while there is no row, so B still closes the screen
    brls::Label* status_text_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
    brls::RepeatingTimer refresh_;
};

}  // namespace app
