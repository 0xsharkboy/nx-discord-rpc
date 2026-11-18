#include "tabs/settings_tab.hpp"

#include "app_settings.hpp"
#include "boot_flag.hpp"
#include "palette.hpp"
#include "views/hidden_games_view.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;
constexpr float kDisabledAlpha = 0.4f;

brls::Header* make_header(const std::string& title) {
    auto* header = new brls::Header();
    header->setTitle(title);
    return header;
}

// Puts a small note under the cell's title, inside the cell, so its separator stays below both.
void add_hint(brls::BooleanCell* cell, const std::string& hint) {
    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1);
    text->setJustifyContent(brls::JustifyContent::CENTER);
    brls::Label* title = cell->title;
    title->setGrow(0);
    cell->removeView(title, false);
    text->addView(title);

    auto* note = new brls::Label();
    note->setText(hint);
    note->setFontSize(16);
    note->setTextColor(palette::text_faint());
    text->addView(note);

    cell->addView(text, 0);
}

}  // namespace

SettingsTab::SettingsTab(drpc::Client& client, UpdateManager& updater) : client_(client), updater_(updater) {
    auto* list = new brls::Box(brls::Axis::COLUMN);
    list->setPadding(32, 56, 40, 56);

    list->addView(make_header("app/settings/presence"_i18n));
    add_toggle(list, "app/settings/enabled"_i18n, &drpc::ipc::Settings::enabled);
    add_toggle(list, "app/settings/private_mode"_i18n, &drpc::ipc::Settings::private_mode);
    add_toggle(list, "app/settings/show_firmware"_i18n, &drpc::ipc::Settings::show_firmware);
    add_toggle(list, "app/settings/show_home_menu"_i18n, &drpc::ipc::Settings::show_home_menu);
    add_toggle(list, "app/settings/show_paused"_i18n, &drpc::ipc::Settings::show_paused);
    add_toggle(list, "app/settings/show_docked"_i18n, &drpc::ipc::Settings::show_docked);
    add_toggle(list, "app/settings/show_playtime"_i18n, &drpc::ipc::Settings::show_playtime);
    add_hidden_games(list);

    auto* advanced = make_header("app/settings/advanced"_i18n);
    advanced->setMarginTop(24);
    list->addView(advanced);
    add_toggle(list, "app/settings/debug_log"_i18n, &drpc::ipc::Settings::debug_log);
    add_boot_toggle(list);

    drpc::ipc::Status status{};
    client_.status(status);
    auto* relay = new brls::DetailCell();
    relay->setText(status.relay_host[0] ? brls::getStr("app/settings/relay", std::string(status.relay_host))
                                        : "app/settings/relay_unset"_i18n);
    relay->setDetailText("app/settings/relay_hint"_i18n);
    relay->setDetailTextColor(palette::text_faint());
    list->addView(relay);

    add_updates(list);

    setContentView(list);

    load();
    refresh_.setCallback([this] { load(); });
    refresh_.start(kRefreshMs);
}

void SettingsTab::add_toggle(brls::Box* list, const std::string& title, Field field) {
    auto* cell = new brls::BooleanCell();
    cell->init(title, false, [this, cell, field](bool on) { apply(cell, field, on); });
    list->addView(cell);
    toggles_.emplace_back(cell, field);
}

// Opens the per-game list; the detail counts the games that are not Shown.
void SettingsTab::add_hidden_games(brls::Box* list) {
    hidden_games_ = new brls::DetailCell();
    hidden_games_->setText("app/settings/hidden_games"_i18n);
    hidden_games_->registerClickAction([this](brls::View*) {
        HiddenGamesView::open(client_);
        return true;
    });
    list->addView(hidden_games_);
}

// Not a sysmodule setting: the cell reads and writes Atmosphere's boot2 flag file directly, so it stays out of
// toggles_ and works even when the settings cannot be read over IPC.
void SettingsTab::add_boot_toggle(brls::Box* list) {
    auto* cell = new brls::BooleanCell();
    cell->init("app/settings/start_at_boot"_i18n, boot_flag_enabled(boot_flag_path()), [cell](bool on) {
        if (set_boot_flag(boot_flag_path(), on)) return;
        cell->setOn(!on, false);
        auto* dialog = new brls::Dialog("app/settings/start_at_boot_failed"_i18n);
        dialog->addButton("app/settings/close"_i18n, [] {});
        dialog->open();
    });

    add_hint(cell, "app/settings/start_at_boot_hint"_i18n);
    list->addView(cell);
}

void SettingsTab::load() {
    drpc::ipc::Settings read{};
    loaded_ = client_.settings(read);
    if (loaded_) settings_ = read;
    hidden_loaded_ = client_.hidden(*hidden_);
    sync();
}

void SettingsTab::apply(brls::BooleanCell* cell, Field field, bool on) {
    drpc::ipc::Settings next{};
    if (!loaded_ || !client_.settings(next)) {
        cell->setOn(!on, false);
        load();
        return;
    }
    next.*field = on ? 1 : 0;
    client_.set_settings(next);
    load();
}

void SettingsTab::sync() {
    hidden_games_->setAlpha(hidden_loaded_ ? 1.0f : kDisabledAlpha);
    std::string count;
    if (!hidden_loaded_) count.clear();
    else if (hidden_->count == 0) count = "app/settings/hidden_none"_i18n;
    else if (hidden_->count == 1) count = "app/settings/hidden_one"_i18n;
    else count = brls::getStr("app/settings/hidden_many", std::to_string(hidden_->count));
    if (hidden_games_->detail->getFullText() != count) hidden_games_->setDetailText(count);
    for (const auto& [cell, field] : toggles_) {
        cell->setAlpha(loaded_ ? 1.0f : kDisabledAlpha);
        const bool on = loaded_ && settings_.*field != 0;
        if (cell->isOn() != on) cell->setOn(on, false);
    }
}

SettingsTab::~SettingsTab() { updater_.changed().unsubscribe(update_sub_); }

// The app's own setting (app.ini) and the manual check. Last in the list, so the rows above keep their places.
void SettingsTab::add_updates(brls::Box* list) {
    auto* header = make_header("app/settings/updates"_i18n);
    header->setMarginTop(24);
    list->addView(header);

    auto* toggle = new brls::BooleanCell();
    toggle->init("app/settings/check_updates"_i18n, load_app_settings(app_settings_path()).check_updates,
                 [toggle](bool on) {
                     AppSettings s = load_app_settings(app_settings_path());
                     s.check_updates = on;
                     if (save_app_settings(app_settings_path(), s)) return;
                     toggle->setOn(!on, false);
                     auto* dialog = new brls::Dialog("app/settings/check_updates_failed"_i18n);
                     dialog->addButton("app/settings/close"_i18n, [] {});
                     dialog->open();
                 });
    add_hint(toggle, "app/settings/check_updates_hint"_i18n);
    list->addView(toggle);

    update_cell_ = new brls::DetailCell();
    update_cell_->setDetailTextColor(palette::accent_light());
    update_cell_->registerClickAction([this](brls::View*) {
        update_clicked();
        return true;
    });
    list->addView(update_cell_);

    update_sub_ = updater_.changed().subscribe([this] { render_update(); });
    render_update();
}

void SettingsTab::update_clicked() {
    switch (updater_.state().phase) {
        case UpdatePhase::Checking:
        case UpdatePhase::Downloading:
        case UpdatePhase::Done:
            return;
        case UpdatePhase::Available:
        case UpdatePhase::Failed:
        case UpdatePhase::Partial:
            awaiting_ = true;
            updater_.install();
            return;
        case UpdatePhase::Idle:
        case UpdatePhase::Ready:
        case UpdatePhase::UpToDate:
        case UpdatePhase::ManualOnly:
        case UpdatePhase::CheckFailed:
            break;
    }
    awaiting_ = true;
    updater_.check(true);
}

void SettingsTab::render_update() {
    const UpdateState& s = updater_.state();
    const std::string current = std::string("v") + APP_VERSION_STRING;
    std::string title = "app/settings/check_now"_i18n;
    std::string detail;
    std::string message;  // the dialog, when this tab asked
    bool offer_install = false;
    switch (s.phase) {
        case UpdatePhase::Idle:
            break;
        case UpdatePhase::Checking:
            detail = "app/update/short/checking"_i18n;
            break;
        case UpdatePhase::UpToDate:
            detail = "app/update/short/up_to_date"_i18n;
            message = brls::getStr("app/update/up_to_date", current);
            break;
        case UpdatePhase::Available:
            title = brls::getStr("app/settings/update_to", s.tag);
            detail = "app/update/short/available"_i18n;
            message = brls::getStr("app/update/available", s.tag, current);
            offer_install = true;
            break;
        case UpdatePhase::ManualOnly:
            detail = brls::getStr("app/update/short/manual", s.tag);
            message = brls::getStr("app/update/manual", s.tag);
            break;
        case UpdatePhase::CheckFailed:
            detail = "app/update/short/check_failed"_i18n;
            message = "app/update/check_failed"_i18n;
            break;
        case UpdatePhase::Downloading:
            title = brls::getStr("app/settings/update_to", s.tag);
            detail = brls::getStr("app/update/short/downloading", std::to_string(s.percent()));
            break;
        case UpdatePhase::Done:
            detail = "app/update/short/done"_i18n;
            message = s.start_failed ? "app/update/done_start_failed"_i18n : "app/update/done"_i18n;
            break;
        case UpdatePhase::Ready:
            detail = "app/update/short/done"_i18n;
            break;
        case UpdatePhase::Failed:
            title = brls::getStr("app/settings/update_to", s.tag);
            detail = "app/update/short/failed"_i18n;
            message = "app/update/failed"_i18n;
            break;
        case UpdatePhase::Partial:
            title = brls::getStr("app/settings/update_to", s.tag);
            detail = "app/update/short/partial"_i18n;
            message = "app/update/partial"_i18n;
            if (s.start_failed) message += " " + "app/update/partial_start_failed"_i18n;
            break;
    }
    // A manual check from Ready must not hide that the staged update still waits for the app to close.
    if (s.staged && !message.empty() &&
        (s.phase == UpdatePhase::UpToDate || s.phase == UpdatePhase::CheckFailed))
        message = "app/update/ready"_i18n + " " + message;
    if (update_cell_->title->getFullText() != title) update_cell_->setText(title);
    if (update_cell_->detail->getFullText() != detail) update_cell_->setDetailText(detail);
    update_cell_->detail->setTextColor(s.phase == UpdatePhase::Failed || s.phase == UpdatePhase::Partial ||
                                               s.phase == UpdatePhase::CheckFailed
                                           ? palette::red()
                                           : palette::accent_light());

    if (!awaiting_ || message.empty()) return;
    awaiting_ = false;
    auto* dialog = new brls::Dialog(message);
    if (offer_install) {
        dialog->addButton("app/update/later"_i18n, [] {});
        dialog->addButton("app/update/update"_i18n, [this] {
            awaiting_ = true;
            updater_.install();
        });
    } else {
        dialog->addButton("app/settings/close"_i18n, [] {});
    }
    dialog->open();
}

}  // namespace app
