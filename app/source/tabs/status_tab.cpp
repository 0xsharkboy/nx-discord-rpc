#include "tabs/status_tab.hpp"

#include <string>

#include "palette.hpp"
#include "presentation.hpp"
#include "views/presence_card.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;

std::string footer_text(Footer kind, const drpc::ipc::Status& status) {
    switch (kind) {
        case Footer::NoUrl:
            return "app/status/footer/nourl"_i18n;
        case Footer::Offline:
            return "app/status/footer/offline"_i18n;
        case Footer::NoKey:
            return "app/status/footer/nokey"_i18n;
        case Footer::Broken:
            return "app/status/footer/broken"_i18n;
        case Footer::Unauthorized:
            return "app/status/footer/unauthorized"_i18n;
        case Footer::Backoff:
            return brls::getStr("app/status/footer/backoff", status.backoff_s);
        case Footer::OkUnnamed:
            return "app/status/footer/ok_unnamed"_i18n;
        case Footer::Ok:
            break;
    }
    return brls::getStr("app/status/footer/ok", std::string(status.username));
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

}  // namespace

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
    if (!client_.status(status)) {
        card_->tick();
        return;
    }
    card_->update(status);
    const FooterLine line = footer_line(status);
    footer_->setText(footer_text(line.kind, status));
    dot_->setBackgroundColor(tone_color(line.tone));
}

}  // namespace app
