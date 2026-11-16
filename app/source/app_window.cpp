#include "app_window.hpp"

#include <borealis.hpp>
#include <utility>

#include "drpc/protocol.hpp"
#include "palette.hpp"
#include "tabs/about_tab.hpp"
#include "tabs/account_tab.hpp"
#include "tabs/settings_tab.hpp"
#include "tabs/status_tab.hpp"

using namespace brls::literals;

namespace app {

namespace {

void apply_theme() {
    const NVGcolor accent = palette::accent();
    const NVGcolor accent_light = palette::accent_light();
    brls::Theme& dark = brls::Theme::getDarkTheme();
    dark.addColor("brls/background", nvgRGB(0x31, 0x33, 0x38));
    dark.addColor("brls/clear", nvgRGB(0x31, 0x33, 0x38));
    dark.addColor("brls/sidebar/background", nvgRGB(0x2B, 0x2D, 0x31));
    dark.addColor("brls/highlight/background", nvgRGB(0x1E, 0x1F, 0x22));
    dark.addColor("brls/accent", accent);
    dark.addColor("brls/click_pulse", nvgRGBA(0x58, 0x65, 0xF2, 38));
    dark.addColor("brls/highlight/color1", accent);
    dark.addColor("brls/highlight/color2", accent_light);
    dark.addColor("brls/sidebar/active_item", accent);
    dark.addColor("brls/button/primary_enabled_background", accent);
    dark.addColor("brls/button/primary_enabled_text", nvgRGB(0xFF, 0xFF, 0xFF));
    dark.addColor("brls/button/highlight_enabled_text", accent_light);
    dark.addColor("brls/button/highlight_disabled_text", accent_light);
    dark.addColor("brls/list/listItem_value_color", accent_light);
    dark.addColor("brls/slider/line_filled", accent);
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
}

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
    drpc::Client& client = *client_;
    tabs->addTab("app/tabs/status"_i18n, [&client] { return new StatusTab(client); });
    tabs->addTab("app/tabs/settings"_i18n, [&client] { return new SettingsTab(client); });
    tabs->addTab("app/tabs/account"_i18n, [&client] { return new AccountTab(client); });
    tabs->addSeparator();
    tabs->addTab("app/tabs/about"_i18n, [&client] { return new AboutTab(client); });
    tabs->getAppletFrameItem()->title = "app/title"_i18n;

    brls::Application::pushActivity(new brls::Activity(new brls::AppletFrame(tabs)));
}

}  // namespace app
