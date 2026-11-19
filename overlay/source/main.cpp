#define TESLA_INIT_IMPL
#include <tesla.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "drpc/client.hpp"
#include "drpc/hidden_titles.hpp"

namespace {

using drpc::ConnectError;
namespace ipc = drpc::ipc;

constexpr int64_t kRefreshMs = 1000;
constexpr u16 kLineHeight = 40;
constexpr u16 kLinkHeight = 64;
constexpr s32 kPadding = 10;
constexpr int64_t kNoticeMs = 2000;

drpc::Client* g_client = nullptr;
ConnectError g_connect_error = ConnectError::None;
uint32_t g_remote_version = 0;

int64_t now_ms() { return static_cast<int64_t>(armTicksToNs(armGetSystemTick()) / 1000000ULL); }

std::string link_line(const ipc::Status& s) {
    switch (s.link) {
        case ipc::LinkState::Linked:
            return s.username[0] ? std::string("Linked as ") + s.username : std::string("Linked");
        case ipc::LinkState::NoKey:
            return "Not linked\nOpen the NX Discord RPC app";
        case ipc::LinkState::Unauthorized:
            return "Relay rejected the key";
        case ipc::LinkState::LinkBroken:
            return "Discord link broken";
        case ipc::LinkState::Outdated:
            // Two lines at most, like Not linked; min_version is "" when the relay named none.
            if (!s.min_version[0]) return "Update required\nUpdate from the NX Discord RPC app";
            return std::string("Update required (") + s.min_version + " or newer)\nUpdate from the NX Discord RPC app";
    }
    return {};
}

std::string presence_line(const ipc::Status& s) {
    if (!s.shown || !s.name[0]) return "Nothing shown";
    std::string line = s.name;
    if (s.details[0]) line += std::string(" · ") + s.details;
    return line;
}

u32 text_width(tsl::gfx::Renderer* r, const std::string& text, float size) {
    return r->drawString(text.c_str(), false, 0, 0, size, tsl::style::color::ColorTransparent).first;
}

std::string fit(tsl::gfx::Renderer* r, const std::string& text, float size, s32 max_width) {
    if (static_cast<s32>(text_width(r, text, size)) <= max_width) return text;
    return r->limitStringLength(text, false, size, max_width - static_cast<s32>(text_width(r, "…", size)));
}

void wrap_paragraph(tsl::gfx::Renderer* r, const std::string& text, float size, s32 max_width,
                    std::vector<std::string>& lines) {
    std::string line;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(start, end - start);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && static_cast<s32>(text_width(r, candidate, size)) > max_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        start = end + 1;
    }
    if (!line.empty()) lines.push_back(line);
}

std::vector<std::string> wrap(tsl::gfx::Renderer* r, const std::string& text, float size, s32 max_width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        wrap_paragraph(r, text.substr(start, end - start), size, max_width, lines);
        start = end + 1;
    }
    return lines;
}

s32 draw_wrapped(tsl::gfx::Renderer* r, const std::string& text, s32 x, s32 y, float size, s32 max_width,
                 tsl::Color color, size_t max_lines = 0) {
    std::vector<std::string> lines = wrap(r, text, size, max_width);
    if (max_lines && lines.size() > max_lines) {
        std::string rest = lines[max_lines - 1];
        for (size_t i = max_lines; i < lines.size(); ++i) rest += " " + lines[i];
        lines.resize(max_lines);
        lines.back() = rest;
    }
    const s32 step = static_cast<s32>(size) + 4;
    for (const std::string& line : lines) {
        r->drawString(fit(r, line, size, max_width).c_str(), false, x, y, size, color);
        y += step;
    }
    return y;
}

std::string unavailable_message(std::string& second) {
    if (g_connect_error == ConnectError::VersionMismatch) {
        const std::string remote = std::to_string(g_remote_version);
        const std::string local = std::to_string(ipc::kProtocolVersion);
        if (drpc::sysmodule_outdated(g_remote_version))
            second = "Update the NX Discord RPC sysmodule (protocol " + remote + ") to match this overlay (protocol " +
                     local + ").";
        else
            second = "Update this overlay (protocol " + local + ") to match the sysmodule (protocol " + remote + ").";
        return "Version mismatch.";
    }
    second.clear();
    return "NX Discord RPC is not running.";
}

class MainGui : public tsl::Gui {
public:
    tsl::elm::Element* createUI() override {
        auto* frame = new tsl::elm::OverlayFrame("NX Discord RPC", OVERLAY_VERSION);

        if (g_connect_error != ConnectError::None) {
            frame->setContent(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32) {
                std::string second;
                const std::string first = unavailable_message(second);
                const s32 next = draw_wrapped(r, first, x + kPadding, y + 40, 22, w - 2 * kPadding,
                                              r->a(tsl::style::color::ColorText));
                if (!second.empty())
                    draw_wrapped(r, second, x + kPadding, next + 12, 18, w - 2 * kPadding,
                                 r->a(tsl::style::color::ColorDescription));
            }));
            return frame;
        }

        ipc::Settings cur{};
        if (!g_client->settings(cur)) {
            frame->setContent(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32) {
                draw_wrapped(r, "Lost contact with NX Discord RPC", x + kPadding, y + 40, 22, w - 2 * kPadding,
                             r->a(tsl::style::color::ColorText));
            }));
            return frame;
        }

        auto* list = new tsl::elm::List();
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32) {
            draw_wrapped(r, link_text_, x + kPadding, y + 28, 20, w - 2 * kPadding, r->a(tsl::style::color::ColorText), 2);
        }), kLinkHeight);
        list->addItem(new tsl::elm::CustomDrawer([this](tsl::gfx::Renderer* r, s32 x, s32 y, s32 w, s32) {
            r->drawString(fit(r, presence_text_, 18, w - 2 * kPadding).c_str(), false, x + kPadding, y + 28, 18,
                          r->a(tsl::style::color::ColorDescription));
        }), kLineHeight);

        add_toggle(list, "Enabled", cur.enabled, &ipc::Settings::enabled);
        add_toggle(list, "Private mode", cur.private_mode, &ipc::Settings::private_mode);
        add_this_game(list);
        add_toggle(list, "Show firmware", cur.show_firmware, &ipc::Settings::show_firmware);
        add_toggle(list, "Show HOME Menu", cur.show_home_menu, &ipc::Settings::show_home_menu);
        add_toggle(list, "Show paused", cur.show_paused, &ipc::Settings::show_paused);
        add_toggle(list, "Show docked", cur.show_docked, &ipc::Settings::show_docked);
        add_toggle(list, "Show playtime", cur.show_playtime, &ipc::Settings::show_playtime);

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
    // Cycles the running game through Shown, Playing a game and Hidden (its entry in the hidden list).
    void add_this_game(tsl::elm::List* list) {
        this_game_ = new tsl::elm::ListItem("This game");
        this_game_->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            cycle_this_game();
            return true;
        });
        list->addItem(this_game_);
    }

    void cycle_this_game() {
        if (running_id_ == 0) return;
        static ipc::HiddenList list;  // 1 KB, off the stack
        if (!g_client->hidden(list)) return notice("Not saved");
        const drpc::TitleVisibility next = drpc::next_visibility(drpc::visibility_of(list, running_id_));
        if (drpc::set_visibility(list, running_id_, next) == drpc::SetVisibility::Full) return notice("List full (64)");
        if (!g_client->set_hidden(list)) return notice("Not saved");
        notice_until_ms_ = -1;
        show_visibility(next);
    }

    void notice(const char* text) {
        notice_until_ms_ = now_ms() + kNoticeMs;
        this_game_->setValue(text, true);
    }

    void show_visibility(drpc::TitleVisibility v) {
        switch (v) {
            case drpc::TitleVisibility::Shown:
                this_game_->setValue("Shown");
                return;
            case drpc::TitleVisibility::Private:
                this_game_->setValue("Playing a game");
                return;
            case drpc::TitleVisibility::Hidden:
                this_game_->setValue("Hidden");
                return;
        }
    }

    void refresh_this_game() {
        if (!this_game_) return;  // not built when the settings could not be read
        if (notice_until_ms_ >= 0 && now_ms() < notice_until_ms_) return;
        notice_until_ms_ = -1;
        if (running_id_ == 0) {
            this_game_->setValue("No game running", true);
            return;
        }
        static ipc::HiddenList list;
        if (!g_client->hidden(list)) {
            this_game_->setValue("Unavailable", true);
            return;
        }
        show_visibility(drpc::visibility_of(list, running_id_));
    }

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
            link_text_ = "Lost contact with NX Discord RPC";
            presence_text_.clear();
            running_id_ = 0;
            if (this_game_) this_game_->setValue("Unavailable", true);
            return;
        }
        link_text_ = link_line(s);
        presence_text_ = presence_line(s);
        running_id_ = s.running_title_id >= ipc::kMinReportableTitleId ? s.running_title_id : 0;
        refresh_this_game();

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
    tsl::elm::ListItem* this_game_ = nullptr;
    uint64_t running_id_ = 0;  // the running game whatever its visibility, 0 when none
    int64_t notice_until_ms_ = -1;
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
