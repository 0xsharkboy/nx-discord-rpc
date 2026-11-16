#include "views/presence_card.hpp"

#include <cstdio>

#include "palette.hpp"
#include "title_icon.hpp"
#include "views/bold_label.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr float kIconSide = 144;

brls::Image* make_icon() {
    auto* image = new brls::Image();
    image->setSize(brls::Size(kIconSide, kIconSide));
    image->setScalingType(brls::ImageScalingType::FILL);
    image->setCornerRadius(24);
    return image;
}

brls::Label* make_line(float size, NVGcolor color) {
    auto* label = new brls::Label();
    label->setFontSize(size);
    label->setTextColor(color);
    label->setSingleLine(true);
    return label;
}

}  // namespace

std::string format_elapsed(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    const long long h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    char buffer[32];
    if (h > 0)
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", h, m, s);
    else
        std::snprintf(buffer, sizeof buffer, "%02lld:%02lld", m, s);
    return brls::getStr("app/status/elapsed", std::string(buffer));
}

PresenceCard::PresenceCard() : brls::Box(brls::Axis::COLUMN) {
    setBackgroundColor(palette::card());
    setCornerRadius(16);
    setPadding(28, 32, 32, 32);

    heading_ = make_line(15, palette::text_faint());
    heading_->setText("app/status/heading"_i18n);
    heading_->setMarginBottom(20);
    addView(heading_);

    auto* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    addView(row);

    auto* frame = new brls::Box(brls::Axis::COLUMN);
    frame->setSize(brls::Size(kIconSide, kIconSide));
    frame->setShrink(0);
    logo_ = make_icon();
    logo_->setImageFromRes("img/switch_logo.png");
    icon_ = make_icon();
    icon_->setVisibility(brls::Visibility::GONE);
    frame->addView(logo_);
    frame->addView(icon_);
    row->addView(frame);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setMarginLeft(28);
    text->setGrow(1);
    text->setShrink(1);
    text->setJustifyContent(brls::JustifyContent::CENTER);
    row->addView(text);

    name_ = new BoldLabel();
    name_->setFontSize(30);
    name_->setTextColor(palette::text());
    name_->setSingleLine(true);
    text->addView(name_);

    nothing_ = new brls::Label();
    nothing_->setFontSize(24);
    nothing_->setTextColor(palette::text_muted());
    nothing_->setIsWrapping(true);
    nothing_->setText("app/status/nothing"_i18n);
    nothing_->setVisibility(brls::Visibility::GONE);
    text->addView(nothing_);

    details_ = make_line(22, palette::text_muted());
    details_->setMarginTop(10);
    text->addView(details_);

    timer_ = make_line(22, palette::accent_light());
    timer_->setMarginTop(10);
    text->addView(timer_);
}

void PresenceCard::show_icon(const std::string& title_id) {
    if (title_id == icon_title_) return;
    icon_title_ = title_id;
    const auto& jpeg = title_icon::get(title_id);
    if (jpeg.empty()) {
        icon_->setVisibility(brls::Visibility::GONE);
        logo_->setVisibility(brls::Visibility::VISIBLE);
        return;
    }
    icon_->setImageFromMem(jpeg.data(), static_cast<int>(jpeg.size()));
    logo_->setVisibility(brls::Visibility::GONE);
    icon_->setVisibility(brls::Visibility::VISIBLE);
}

void PresenceCard::update(const drpc::ipc::Status& status) {
    const bool shown = status.shown && status.name[0] != '\0';
    show_icon(shown && status.is_game ? status.title_id : "");
    logo_->setAlpha(shown ? 1.0f : 0.35f);

    if (!shown) {
        name_->setVisibility(brls::Visibility::GONE);
        nothing_->setVisibility(brls::Visibility::VISIBLE);
        details_->setVisibility(brls::Visibility::GONE);
        elapsed_s_ = -1;
        timer_->setVisibility(brls::Visibility::GONE);
        return;
    }

    nothing_->setVisibility(brls::Visibility::GONE);
    name_->setVisibility(brls::Visibility::VISIBLE);
    name_->setText(status.name);
    details_->setText(status.details);
    details_->setVisibility(status.details[0] ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    elapsed_s_ = status.elapsed_s;
    fetched_at_ = std::chrono::steady_clock::now();
    tick();
}

void PresenceCard::tick() {
    if (elapsed_s_ < 0) {
        timer_->setVisibility(brls::Visibility::GONE);
        return;
    }
    const auto since = std::chrono::steady_clock::now() - fetched_at_;
    const int64_t seconds = elapsed_s_ + std::chrono::duration_cast<std::chrono::seconds>(since).count();
    timer_->setText(format_elapsed(seconds));
    timer_->setVisibility(brls::Visibility::VISIBLE);
}

}  // namespace app
