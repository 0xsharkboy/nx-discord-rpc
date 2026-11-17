#include "app_window.hpp"

#include <borealis.hpp>
#include <cstdio>
#include <utility>

#include "drpc/protocol.hpp"
#include "palette.hpp"
#include "sysmodule_launcher.hpp"
#include "tabs/about_tab.hpp"
#include "tabs/account_tab.hpp"
#include "tabs/settings_tab.hpp"
#include "tabs/status_tab.hpp"

using namespace brls::literals;

namespace app {

namespace {

// After a launch, connect every 500 ms for up to 5 s.
constexpr long kStartPollMs = 500;
constexpr int kStartPollAttempts = 10;

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
            if (drpc::sysmodule_outdated(remote_version))
                return brls::getStr("app/blocking/update_sysmodule", remote_version, drpc::ipc::kProtocolVersion);
            return brls::getStr("app/blocking/update_app", drpc::ipc::kProtocolVersion, remote_version);
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
        show_blocking(blocking_message(error, client_->remote_version()),
                      error == drpc::ConnectError::NotRunning);
        return;
    }
    show_tabs();
}

void AppWindow::show_blocking(const std::string& message, bool offer_start) {
    auto* label = new brls::Label();
    label->setText(message);
    label->setFontSize(28);
    label->setIsWrapping(true);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMaxWidth(1040);

    auto* body = new brls::Box(brls::Axis::COLUMN);
    body->setJustifyContent(brls::JustifyContent::CENTER);
    body->setAlignItems(brls::AlignItems::CENTER);
    body->setPadding(40, 80, 40, 80);
    body->addView(label);
    body->getAppletFrameItem()->title = "app/title"_i18n;

    if (offer_start) {
        auto* button = new brls::Button();
        button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        button->setText("app/blocking/start_now"_i18n);
        button->setWidth(240);
        button->setMarginTop(40);
        button->registerClickAction([this](brls::View*) {
            start_sysmodule();
            return true;
        });
        body->addView(button);

        auto* note = new brls::Label();
        note->setFontSize(22);
        note->setIsWrapping(true);
        note->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        note->setMaxWidth(1040);
        note->setMarginTop(24);
        note->setTextColor(palette::text_muted());
        note->setVisibility(brls::Visibility::INVISIBLE);
        body->addView(note);

        message_ = label;
        start_button_ = button;
        start_note_ = note;
    } else {
        body->setFocusable(true);
        body->setHideHighlight(true);
        body->setHideClickAnimation(true);
    }

    auto* frame = new brls::AppletFrame(body);
    frame->setFooterVisibility(brls::Visibility::GONE);
    blocking_ = new brls::Activity(frame);
    brls::Application::pushActivity(blocking_);
}

brls::AppletFrame* AppWindow::make_tabs() {
    auto* tabs = new brls::TabFrame();
    drpc::Client& client = *client_;
    tabs->addTab("app/tabs/status"_i18n, [&client] { return new StatusTab(client); });
    tabs->addTab("app/tabs/settings"_i18n, [&client] { return new SettingsTab(client); });
    tabs->addTab("app/tabs/account"_i18n, [&client] { return new AccountTab(client); });
    tabs->addSeparator();
    tabs->addTab("app/tabs/about"_i18n, [&client] { return new AboutTab(client); });
    tabs->getAppletFrameItem()->title = "app/title"_i18n;
    return new brls::AppletFrame(tabs);
}

void AppWindow::show_tabs() { brls::Application::pushActivity(new brls::Activity(make_tabs())); }

void AppWindow::start_sysmodule() {
    if (starting_ || !start_button_) return;
    starting_ = true;
    start_note_->setText("app/blocking/starting"_i18n);
    start_note_->setTextColor(palette::text_muted());
    start_note_->setVisibility(brls::Visibility::VISIBLE);
    // Every step below runs from brls::sync or brls::delay, never from the click itself, since a success
    // replaces the screen that holds the button.
    brls::async([this] {
        const LaunchResult launch = launch_sysmodule();
        brls::sync([this, launch] {
            if (launch.status != LaunchStatus::Failed) {
                brls::delay(kStartPollMs, [this] { poll_start(1); });
                return;
            }
            // The launch may fail because the sysmodule came up in the meantime: check once before reporting.
            if (client_->connect() == drpc::ConnectError::None) {
                connected();
                return;
            }
            if (launch.code == 0) {
                start_failed("app/blocking/start_unavailable"_i18n);
                return;
            }
            char code[16];
            std::snprintf(code, sizeof code, "0x%X", static_cast<unsigned>(launch.code));
            start_failed(brls::getStr("app/blocking/start_failed", std::string(code)));
        });
    });
}

void AppWindow::poll_start(int attempt) {
    const drpc::ConnectError error = client_->connect();
    if (error == drpc::ConnectError::None) {
        connected();
        return;
    }
    if (error == drpc::ConnectError::VersionMismatch) {
        // It started, but this app cannot talk to it: show the same message as at startup, with nothing to retry.
        message_->setText(blocking_message(error, client_->remote_version()));
        brls::Box* body = start_button_->getParent();
        body->setFocusable(true);
        body->setHideHighlight(true);
        body->setHideClickAnimation(true);
        brls::Application::giveFocus(body);
        start_note_->setVisibility(brls::Visibility::GONE);
        start_button_->setVisibility(brls::Visibility::GONE);
        start_note_ = nullptr;
        start_button_ = nullptr;
        starting_ = false;
        return;
    }
    if (attempt >= kStartPollAttempts) {
        start_failed("app/blocking/start_timeout"_i18n);
        return;
    }
    brls::delay(kStartPollMs, [this, attempt] { poll_start(attempt + 1); });
}

void AppWindow::connected() {
    brls::Activity* activity = blocking_;
    blocking_ = nullptr;
    message_ = nullptr;
    start_note_ = nullptr;
    start_button_ = nullptr;
    starting_ = false;

    // The not-running screen was the first activity, which borealis never pops: swap its content for the
    // tabs instead, so the tabs end up as the root, as on a normal start. The focus moves to a view that
    // stays alive first, since replacing the content deletes the focused button.
    static brls::Box* parking = [] {
        auto* box = new brls::Box();
        box->setFocusable(true);
        return box;
    }();
    brls::Application::giveFocus(parking);
    activity->setContentView(make_tabs());
    activity->registerExitAction();  // the global quit action lived on the old content
    activity->willAppear(true);
    brls::Application::giveFocus(activity->getDefaultFocus());
}

void AppWindow::start_failed(const std::string& message) {
    starting_ = false;
    start_note_->setText(message);
    start_note_->setTextColor(palette::red());
    start_note_->setVisibility(brls::Visibility::VISIBLE);
}

}  // namespace app
