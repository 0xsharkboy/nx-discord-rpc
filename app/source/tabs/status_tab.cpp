#include "tabs/status_tab.hpp"

#include "palette.hpp"
#include "views/presence_card.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;

}  // namespace

FooterLine footer_line(const drpc::ipc::Status& status) {
    using drpc::ipc::LinkState;
    using drpc::ipc::RelayHealth;
    if (status.health == RelayHealth::NoUrl) return {"app/status/footer/nourl"_i18n, Tone::Bad};
    if (status.health == RelayHealth::Offline) return {"app/status/footer/offline"_i18n, Tone::Warning};
    switch (status.link) {
        case LinkState::NoKey:
            return {"app/status/footer/nokey"_i18n, Tone::Neutral};
        case LinkState::LinkBroken:
            return {"app/status/footer/broken"_i18n, Tone::Bad};
        case LinkState::Unauthorized:
            return {"app/status/footer/unauthorized"_i18n, Tone::Bad};
        case LinkState::Linked:
            break;
    }
    if (status.health == RelayHealth::Backoff)
        return {brls::getStr("app/status/footer/backoff", status.backoff_s), Tone::Warning};
    return {brls::getStr("app/status/footer/ok", std::string(status.username)), Tone::Good};
}

NVGcolor tone_color(Tone tone) {
    switch (tone) {
        case Tone::Good:
            return palette::green();
        case Tone::Warning:
            return palette::yellow();
        case Tone::Bad:
            return palette::red();
        case Tone::Neutral:
            break;
    }
    return palette::text_faint();
}

StatusTab::StatusTab(drpc::Client& client) : brls::Box(brls::Axis::COLUMN), client_(client) {
    setPadding(48, 56, 40, 56);

    card_ = new PresenceCard();
    addView(card_);

    auto* footer = new brls::Box(brls::Axis::ROW);
    footer->setAlignItems(brls::AlignItems::CENTER);
    footer->setMarginTop(24);
    footer->setMarginLeft(8);
    addView(footer);

    dot_ = new brls::Box();
    dot_->setSize(brls::Size(12, 12));
    dot_->setCornerRadius(6);
    dot_->setShrink(0);
    footer->addView(dot_);

    footer_ = new brls::Label();
    footer_->setFontSize(20);
    footer_->setTextColor(palette::text_muted());
    footer_->setSingleLine(true);
    footer_->setMarginLeft(14);
    footer_->setShrink(1);
    footer->addView(footer_);

    refresh();
    timer_.setCallback([this] { refresh(); });
    timer_.start(kRefreshMs);
}

void StatusTab::refresh() {
    drpc::ipc::Status status{};
    if (!client_.status(status)) return;
    card_->update(status);
    const FooterLine line = footer_line(status);
    footer_->setText(line.text);
    dot_->setBackgroundColor(tone_color(line.tone));
}

}  // namespace app
