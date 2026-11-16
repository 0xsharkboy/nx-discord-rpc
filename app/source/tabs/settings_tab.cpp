#include "tabs/settings_tab.hpp"

#include "palette.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRetryMs = 1000;
constexpr float kDisabledAlpha = 0.4f;

brls::Header* make_header(const std::string& title) {
    auto* header = new brls::Header();
    header->setTitle(title);
    return header;
}

}  // namespace

SettingsTab::SettingsTab(drpc::Client& client) : client_(client) {
    auto* list = new brls::Box(brls::Axis::COLUMN);
    list->setPadding(32, 56, 40, 56);

    list->addView(make_header("app/settings/presence"_i18n));
    add_toggle(list, "app/settings/enabled"_i18n, &drpc::ipc::Settings::enabled);
    add_toggle(list, "app/settings/private_mode"_i18n, &drpc::ipc::Settings::private_mode);
    add_toggle(list, "app/settings/show_elapsed"_i18n, &drpc::ipc::Settings::show_elapsed);
    add_toggle(list, "app/settings/show_firmware"_i18n, &drpc::ipc::Settings::show_firmware);
    add_toggle(list, "app/settings/show_home_menu"_i18n, &drpc::ipc::Settings::show_home_menu);

    auto* advanced = make_header("app/settings/advanced"_i18n);
    advanced->setMarginTop(24);
    list->addView(advanced);
    add_toggle(list, "app/settings/debug_log"_i18n, &drpc::ipc::Settings::debug_log);

    drpc::ipc::Status status{};
    client_.status(status);
    auto* relay = new brls::DetailCell();
    relay->setText(status.relay_host[0] ? brls::getStr("app/settings/relay", std::string(status.relay_host))
                                        : "app/settings/relay_unset"_i18n);
    relay->setDetailText("app/settings/relay_hint"_i18n);
    relay->setDetailTextColor(palette::text_faint());
    list->addView(relay);

    setContentView(list);

    load();
    if (!loaded_) {
        retry_.setCallback([this] {
            load();
            if (loaded_) retry_.stop();
        });
        retry_.start(kRetryMs);
    }
}

void SettingsTab::add_toggle(brls::Box* list, const std::string& title, Field field) {
    auto* cell = new brls::BooleanCell();
    cell->init(title, false, [this, cell, field](bool on) { apply(cell, field, on); });
    list->addView(cell);
    toggles_.emplace_back(cell, field);
}

void SettingsTab::load() {
    drpc::ipc::Settings read{};
    loaded_ = client_.settings(read);
    if (loaded_) settings_ = read;
    sync();
}

void SettingsTab::apply(brls::BooleanCell* cell, Field field, bool on) {
    if (!loaded_) {
        cell->setOn(!on, false);
        return;
    }
    drpc::ipc::Settings next = settings_;
    next.*field = on ? 1 : 0;
    client_.set_settings(next);
    load();
}

void SettingsTab::sync() {
    for (const auto& [cell, field] : toggles_) {
        cell->setAlpha(loaded_ ? 1.0f : kDisabledAlpha);
        const bool on = loaded_ && settings_.*field != 0;
        if (cell->isOn() != on) cell->setOn(on, false);
    }
}

}  // namespace app
