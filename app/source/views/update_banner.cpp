#include "views/update_banner.hpp"

#include "palette.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr float kButtonWidth = 180;
constexpr float kTrackHeight = 8;

}  // namespace

UpdateBanner::UpdateBanner(UpdateManager& updater) : brls::Box(brls::Axis::ROW), updater_(updater) {
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
    button_->setWidth(kButtonWidth);
    button_->setShrink(0);
    button_->setMarginLeft(24);
    button_->registerClickAction([this](brls::View*) {
        updater_.install();
        return true;
    });
    addView(button_);

    subscription_ = updater_.changed().subscribe([this] { render(); });
    render();
}

UpdateBanner::~UpdateBanner() { updater_.changed().unsubscribe(subscription_); }

void UpdateBanner::render() {
    const UpdateState& s = updater_.state();
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
            text = "app/update/done"_i18n;
            break;
        case UpdatePhase::Failed:
            text = "app/update/failed"_i18n;
            button = "app/update/try_again"_i18n;
            break;
        case UpdatePhase::Partial:
            text = "app/update/partial"_i18n;
            button = "app/update/try_again"_i18n;
            break;
        case UpdatePhase::Idle:
        case UpdatePhase::Checking:
        case UpdatePhase::UpToDate:
        case UpdatePhase::CheckFailed:
            break;
    }
    if (text.empty()) {
        // The button may hold the focus (it was just clicked): hand it back before hiding.
        if (button_->isFocused()) brls::Application::giveFocus(getParent());
        setVisibility(brls::Visibility::GONE);
        return;
    }
    setVisibility(brls::Visibility::VISIBLE);
    text_->setText(text);
    const bool bad = s.phase == UpdatePhase::Failed || s.phase == UpdatePhase::Partial;
    setBorderColor(bad ? palette::red() : s.phase == UpdatePhase::Done ? palette::green() : palette::accent());
    setBorderThickness(2);
    track_->setVisibility(progress ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    fill_->setWidthPercentage(float(s.percent()));
    if (button.empty()) {
        if (button_->isFocused()) brls::Application::giveFocus(getParent());
        button_->setVisibility(brls::Visibility::GONE);
    } else {
        button_->setText(button);
        button_->setVisibility(brls::Visibility::VISIBLE);
    }
}

}  // namespace app
