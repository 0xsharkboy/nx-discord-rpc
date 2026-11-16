#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "drpc/client.hpp"

namespace {

using drpc::ConnectError;
namespace ipc = drpc::ipc;

constexpr int64_t kRefreshMs = 1000;
constexpr u16 kLineHeight = 40;

drpc::Client* g_client = nullptr;
ConnectError g_connect_error = ConnectError::None;
uint32_t g_remote_version = 0;

int64_t now_ms() { return static_cast<int64_t>(armTicksToNs(armGetSystemTick()) / 1000000ULL); }

std::string link_line(const ipc::Status& s) {
    switch (s.link) {
        case ipc::LinkState::Linked:
            return s.username[0] ? std::string("Linked as ") + s.username : std::string("Linked");
        case ipc::LinkState::NoKey:
            return "Not linked — open the nx-discord-rpc app";
        case ipc::LinkState::Unauthorized:
            return "Relay rejected the key";
        case ipc::LinkState::LinkBroken:
            return "Discord link broken";
    }
    return {};
}

std::string presence_line(const ipc::Status& s) {
    if (!s.shown || !s.name[0]) return "Nothing shown";
    std::string line = s.name;
    if (s.details[0]) line += std::string(" · ") + s.details;
    return line;
}

std::string unavailable_message(std::string& second) {
    if (g_connect_error == ConnectError::VersionMismatch) {
        second = "Sysmodule protocol " + std::to_string(g_remote_version) + ", overlay protocol " +
                 std::to_string(ipc::kProtocolVersion) + ": install the same release of both.";
        return "Version mismatch.";
    }
    second.clear();
    return "nx-discord-rpc is not running.";
}

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("nx-discord-rpc", OVERLAY_VERSION);

        if (g_connect_error != ConnectError::None) {
            frame->setContent(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32 x, s32 y, s32, s32) {
                std::string second;
                const std::string first = unavailable_message(second);
                r->drawString(first.c_str(), false, x + 10, y + 40, 22, r->a(tsl::style::color::ColorText));
                if (!second.empty())
                    r->drawString(second.c_str(), false, x + 10, y + 75, 18, r->a(tsl::style::color::ColorDescription));
            }));
            return frame;
        }

        ipc::Settings cur{};
        if (!g_client->settings(cur)) {
            frame->setContent(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32 x, s32 y, s32, s32) {
                r->drawString("Lost contact with nx-discord-rpc", false, x + 10, y + 40, 22, r->a(tsl::style::color::ColorText));
            }));
            return frame;
        }

        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32, s32) {
            r->drawString(link_text_.c_str(), false, x + 10, y + 28, 20, r->a(tsl::style::color::ColorText));
        }), kLineHeight);
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32, s32) {
            r->drawString(presence_text_.c_str(), false, x + 10, y + 28, 18, r->a(tsl::style::color::ColorDescription));
        }), kLineHeight);

        add_toggle(list, "Enabled", cur.enabled, &ipc::Settings::enabled);
        add_toggle(list, "Private mode", cur.private_mode, &ipc::Settings::private_mode);
        add_toggle(list, "Show elapsed time", cur.show_elapsed, &ipc::Settings::show_elapsed);
        add_toggle(list, "Show firmware", cur.show_firmware, &ipc::Settings::show_firmware);
        add_toggle(list, "Show HOME Menu", cur.show_home_menu, &ipc::Settings::show_home_menu);

        frame->setContent(list);
        refresh();
        return frame;
    }

    void update() override {
        if (g_connect_error != ConnectError::None) return;
        const int64_t now = now_ms();
        if (last_refresh_ms_ >= 0 && now - last_refresh_ms_ < kRefreshMs) return;
        refresh();
    }

private:
    void add_toggle(tsl::elm::List* list, const char* label, bool initial, uint8_t ipc::Settings::*field) {
        auto* item = new tsl::elm::ToggleListItem(label, initial);
        item->setStateChangedListener([item, field](bool state) {
            ipc::Settings s{};
            if (!g_client->settings(s)) {
                item->setState(!state);
                return;
            }
            s.*field = state ? 1 : 0;
            if (!g_client->set_settings(s)) item->setState(!state);
        });
        list->addItem(item);
        toggles_.push_back({item, field});
    }

    void refresh() {
        last_refresh_ms_ = now_ms();
        ipc::Status s{};
        if (!g_client->status(s)) {
            link_text_ = "Lost contact with nx-discord-rpc";
            presence_text_.clear();
            return;
        }
        link_text_ = link_line(s);
        presence_text_ = presence_line(s);

        ipc::Settings cur{};
        if (!g_client->settings(cur)) return;
        for (const Toggle& t : toggles_) {
            const bool want = cur.*t.field != 0;
            if (t.item->getState() != want) t.item->setState(want);
        }
    }

    struct Toggle {
        tsl::elm::ToggleListItem* item;
        uint8_t ipc::Settings::*field;
    };

    std::vector<Toggle> toggles_;
    std::string link_text_;
    std::string presence_text_;
    int64_t last_refresh_ms_ = -1;
};

class DrpcOverlay : public tsl::Overlay {
public:
    void initServices() override {
        client_ = drpc::make_switch_client();
        g_client = client_.get();
        g_connect_error = client_->connect();
        g_remote_version = client_->remote_version();
    }

    void exitServices() override {
        g_client = nullptr;
        client_.reset();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override { return initially<MainGui>(); }

private:
    std::unique_ptr<drpc::Client> client_;
};

}  // namespace

int main(int argc, char** argv) { return tsl::loop<DrpcOverlay>(argc, argv); }
