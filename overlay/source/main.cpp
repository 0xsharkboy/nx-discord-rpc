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
constexpr u16 kLinkHeight = 64;
constexpr s32 kPadding = 10;

drpc::Client* g_client = nullptr;
ConnectError g_connect_error = ConnectError::None;
uint32_t g_remote_version = 0;

int64_t now_ms() { return static_cast<int64_t>(armTicksToNs(armGetSystemTick()) / 1000000ULL); }

std::string link_line(const ipc::Status& s) {
    switch (s.link) {
        case ipc::LinkState::Linked:
            return s.username[0] ? std::string("Linked as ") + s.username : std::string("Linked");
        case ipc::LinkState::NoKey:
            return "Not linked\nOpen the nx-discord-rpc app";
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
            second = "Update the nx-discord-rpc sysmodule (protocol " + remote + ") to match this overlay (protocol " +
                     local + ").";
        else
            second = "Update this overlay (protocol " + local + ") to match the sysmodule (protocol " + remote + ").";
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
                draw_wrapped(r, "Lost contact with nx-discord-rpc", x + kPadding, y + 40, 22, w - 2 * kPadding,
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
