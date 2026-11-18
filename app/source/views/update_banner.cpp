#include "views/update_banner.hpp"

#include "palette.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr float kButtonWidth = 180;
constexpr float kBlockingButtonWidth = 320;  // fits "Check for updates" and its French
constexpr float kTrackHeight = 8;

}  // namespace

UpdateBanner::UpdateBanner(UpdateManager& updater, Place place)
    : brls::Box(brls::Axis::ROW), updater_(updater), place_(place) {
    // Blocking screens have nothing else to focus while the button is hidden: the banner takes the focus then.
    setHideHighlight(true);
    setHideClickAnimation(true);
    setAlignItems(brls::AlignItems::CENTER);
    setBackgroundColor(palette::card());
    setCornerRadius(12);
    setPadding(14, 20, 14, 24);

    auto* column = new brls::Box(brls::Axis::COLUMN);
    column->setGrow(1);
    column->setShrink(1);
    addView(column);

    text_ = new brls::Label();
    text_->setFontSize(18);
    text_->setTextColor(palette::text());
    text_->setIsWrapping(true);
    // The banner can hold the focus on a blocking screen: a label scrolls its text then, unless told not to.
    text_->setAutoAnimate(false);
    column->addView(text_);

    track_ = new brls::Box(brls::Axis::ROW);
    track_->setHeight(kTrackHeight);
    track_->setCornerRadius(kTrackHeight / 2);
    track_->setBackgroundColor(palette::card_inner());
    track_->setMarginTop(12);
    column->addView(track_);

    fill_ = new brls::Box();
    fill_->setHeight(kTrackHeight);
    fill_->setCornerRadius(kTrackHeight / 2);
    fill_->setBackgroundColor(palette::accent());
    track_->addView(fill_);

    button_ = new brls::Button();
    button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    button_->setWidth(place == Place::Blocking ? kBlockingButtonWidth : kButtonWidth);
    button_->setShrink(0);
    button_->setMarginLeft(24);
    button_->registerClickAction([this](brls::View*) {
        clicked();
        return true;
    });
    addView(button_);

    subscription_ = updater_.changed().subscribe([this] { render(); });
    render();
}

UpdateBanner::~UpdateBanner() { updater_.changed().unsubscribe(subscription_); }

void UpdateBanner::set_not_running(bool not_running) {
    not_running_ = not_running;
    render();
}

void UpdateBanner::clicked() {
    switch (updater_.state().phase) {
        case UpdatePhase::Idle:
        case UpdatePhase::Ready:
        case UpdatePhase::UpToDate:
        case UpdatePhase::CheckFailed:
            updater_.check(true);
            break;
        default:
            updater_.install();
            break;
    }
}

void UpdateBanner::render() {
    const UpdateState& s = updater_.state();
    const bool blocking = place_ == Place::Blocking;
    std::string text;
    std::string button;
    bool progress = false;
    switch (s.phase) {
        case UpdatePhase::Available:
            text = brls::getStr("app/update/available", s.tag, std::string("v") + APP_VERSION_STRING);
            button = "app/update/update"_i18n;
            break;
        case UpdatePhase::ManualOnly:
            text = brls::getStr("app/update/manual", s.tag);
            break;
        case UpdatePhase::Downloading:
            text = brls::getStr("app/update/downloading", s.tag, std::to_string(s.percent()));
            progress = true;
            break;
        case UpdatePhase::Done:
            text = s.start_failed ? "app/update/done_start_failed"_i18n : "app/update/done"_i18n;
            break;
        case UpdatePhase::Ready:
            // Not running: the sysmodule that swaps the staged .nro in must be started first.
            text = blocking && not_running_ ? "app/update/ready_start"_i18n : "app/update/ready"_i18n;
            // A way out should the swap never happen (see UpdateManager::check).
            if (blocking) button = "app/update/check"_i18n;
            break;
        case UpdatePhase::Failed:
            text = "app/update/failed"_i18n;
            button = "app/update/try_again"_i18n;
            break;
        case UpdatePhase::Partial:
            text = "app/update/partial"_i18n;
            if (s.start_failed) text += " " + "app/update/partial_start_failed"_i18n;
            button = "app/update/try_again"_i18n;
            break;
        case UpdatePhase::Idle:
            if (!blocking) break;
            // After a quiet check that found nothing newer, say so rather than "may be available".
            text = s.checked ? brls::getStr("app/update/up_to_date", std::string("v") + APP_VERSION_STRING)
                             : "app/update/blocking_idle"_i18n;
            button = "app/update/check"_i18n;
            break;
        case UpdatePhase::Checking:
            if (blocking) text = "app/update/checking"_i18n;
            break;
        case UpdatePhase::UpToDate:
            if (!blocking) break;
            text = brls::getStr("app/update/up_to_date", std::string("v") + APP_VERSION_STRING);
            button = "app/update/check"_i18n;
            break;
        case UpdatePhase::CheckFailed:
            if (!blocking) break;
            text = "app/update/check_failed"_i18n;
            button = "app/update/try_again"_i18n;
            break;
    }
    // A manual check from Ready must not hide that the staged update still waits for the app to close.
    if (s.staged && !text.empty() &&
        (s.phase == UpdatePhase::UpToDate || s.phase == UpdatePhase::CheckFailed || s.phase == UpdatePhase::Idle))
        text = "app/update/ready"_i18n + " " + text;
    if (text.empty()) {
        // The button may hold the focus (it was just clicked): hand it back before hiding.
        if (button_->isFocused()) brls::Application::giveFocus(getParent());
        setVisibility(brls::Visibility::GONE);
        return;
    }
    setVisibility(brls::Visibility::VISIBLE);
    text_->setText(text);
    const bool bad = s.phase == UpdatePhase::Failed || s.phase == UpdatePhase::Partial ||
                     (blocking && s.phase == UpdatePhase::CheckFailed);
    const bool good = (s.phase == UpdatePhase::Done && !s.start_failed) || s.phase == UpdatePhase::Ready;
    const bool warn = s.phase == UpdatePhase::Done && s.start_failed;
    setBorderColor(bad ? palette::red() : warn ? palette::yellow() : good ? palette::green() : palette::accent());
    setBorderThickness(2);
    track_->setVisibility(progress ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    fill_->setWidthPercentage(float(s.percent()));
    if (button.empty()) {
        const bool had_focus = button_->isFocused();
        // The mismatch screen has nothing else to focus: the banner itself takes it (with no highlight). The
        // not-running screen keeps Start now, where the focus goes back.
        const bool self_focus = blocking && !not_running_;
        setFocusable(self_focus);
        if (had_focus) brls::Application::giveFocus(self_focus ? this : getParent());
        button_->setVisibility(brls::Visibility::GONE);
    } else {
        button_->setText(button);
        button_->setVisibility(brls::Visibility::VISIBLE);
        if (blocking) {
            const bool had_focus = brls::Application::getCurrentFocus() == this;
            setFocusable(false);
            if (had_focus) brls::Application::giveFocus(button_);
        }
    }
}

}  // namespace app
