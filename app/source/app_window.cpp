#include "app_window.hpp"

#include <borealis.hpp>
#include <utility>

#include "drpc/protocol.hpp"

using namespace brls::literals;

namespace app {

namespace {

const NVGcolor kAccent = nvgRGB(0x58, 0x65, 0xF2);
const NVGcolor kAccentLight = nvgRGB(0x8C, 0x96, 0xF7);

void apply_theme() {
    brls::Theme& dark = brls::Theme::getDarkTheme();
    dark.addColor("brls/background", nvgRGB(0x31, 0x33, 0x38));
    dark.addColor("brls/clear", nvgRGB(0x31, 0x33, 0x38));
    dark.addColor("brls/sidebar/background", nvgRGB(0x2B, 0x2D, 0x31));
    dark.addColor("brls/highlight/background", nvgRGB(0x1E, 0x1F, 0x22));
    dark.addColor("brls/accent", kAccent);
    dark.addColor("brls/click_pulse", nvgRGBA(0x58, 0x65, 0xF2, 38));
    dark.addColor("brls/highlight/color1", kAccent);
    dark.addColor("brls/highlight/color2", kAccentLight);
    dark.addColor("brls/sidebar/active_item", kAccent);
    dark.addColor("brls/button/primary_enabled_background", kAccent);
    dark.addColor("brls/button/primary_enabled_text", nvgRGB(0xFF, 0xFF, 0xFF));
    dark.addColor("brls/button/highlight_enabled_text", kAccentLight);
    dark.addColor("brls/button/highlight_disabled_text", kAccentLight);
    dark.addColor("brls/list/listItem_value_color", kAccentLight);
    dark.addColor("brls/slider/line_filled", kAccent);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
}

brls::View* empty_tab() { return new brls::Box(brls::Axis::COLUMN); }

}  // namespace

std::string blocking_message(drpc::ConnectError error, uint32_t remote_version) {
    switch (error) {
        case drpc::ConnectError::NotRunning:
            return "app/blocking/not_running"_i18n;
        case drpc::ConnectError::VersionMismatch:
            return brls::getStr("app/blocking/mismatch", drpc::ipc::kProtocolVersion, remote_version);
        case drpc::ConnectError::None:
            break;
    }
    return {};
}

AppWindow::AppWindow(std::unique_ptr<drpc::Client> client) : client_(std::move(client)) {}

void AppWindow::open() {
    apply_theme();
    brls::Application::setGlobalQuit(true);
    const drpc::ConnectError error = client_->connect();
    if (error != drpc::ConnectError::None) {
        show_blocking(blocking_message(error, client_->remote_version()));
        return;
    }
    show_tabs();
}

void AppWindow::show_blocking(const std::string& message) {
    auto* label = new brls::Label();
    label->setText(message);
    label->setFontSize(28);
    label->setIsWrapping(true);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMaxWidth(900);

    auto* body = new brls::Box(brls::Axis::COLUMN);
    body->setJustifyContent(brls::JustifyContent::CENTER);
    body->setAlignItems(brls::AlignItems::CENTER);
    body->setPadding(40, 80, 40, 80);
    body->setFocusable(true);
    body->setHideHighlight(true);
    body->setHideClickAnimation(true);
    body->addView(label);
    body->getAppletFrameItem()->title = "app/title"_i18n;

    auto* frame = new brls::AppletFrame(body);
    frame->setFooterVisibility(brls::Visibility::GONE);
    brls::Application::pushActivity(new brls::Activity(frame));
}

void AppWindow::show_tabs() {
    auto* tabs = new brls::TabFrame();
    tabs->addTab("app/tabs/status"_i18n, empty_tab);
    tabs->addTab("app/tabs/settings"_i18n, empty_tab);
    tabs->addTab("app/tabs/account"_i18n, empty_tab);
    tabs->addSeparator();
    tabs->addTab("app/tabs/about"_i18n, empty_tab);
    tabs->getAppletFrameItem()->title = "app/title"_i18n;

    brls::Application::pushActivity(new brls::Activity(new brls::AppletFrame(tabs)));
}

}  // namespace app
