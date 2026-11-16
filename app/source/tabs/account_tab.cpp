#include "tabs/account_tab.hpp"

#include <cctype>

#include "palette.hpp"
#include "views/bold_label.hpp"
#include "views/link_view.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;
constexpr float kAvatarSide = 88;

brls::Button* make_button(const std::string& text, const brls::ButtonStyle* style) {
    auto* button = new brls::Button();
    button->setStyle(style);
    button->setText(text);
    button->setWidth(260);
    return button;
}

}  // namespace

AccountTab::AccountTab(drpc::Client& client) : brls::Box(brls::Axis::COLUMN), client_(client) {
    setPadding(32, 56, 40, 56);

    auto* header = new brls::Header();
    header->setTitle("app/account/heading"_i18n);
    addView(header);

    auto* card = new brls::Box(brls::Axis::ROW);
    card->setAlignItems(brls::AlignItems::CENTER);
    card->setBackgroundColor(palette::card());
    card->setCornerRadius(16);
    card->setPadding(28, 32, 28, 32);
    card->setMarginTop(16);
    addView(card);

    avatar_ = new brls::Box(brls::Axis::COLUMN);
    avatar_->setSize(brls::Size(kAvatarSide, kAvatarSide));
    avatar_->setCornerRadius(kAvatarSide / 2);
    avatar_->setShrink(0);
    avatar_->setAlignItems(brls::AlignItems::CENTER);
    avatar_->setJustifyContent(brls::JustifyContent::CENTER);
    card->addView(avatar_);

    initial_ = new BoldLabel();
    initial_->setFontSize(40);
    initial_->setTextColor(nvgRGB(0xFF, 0xFF, 0xFF));
    initial_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    avatar_->addView(initial_);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setMarginLeft(28);
    text->setGrow(1);
    text->setShrink(1);
    card->addView(text);

    name_ = new BoldLabel();
    name_->setFontSize(28);
    name_->setTextColor(palette::text());
    name_->setSingleLine(true);
    text->addView(name_);

    state_ = new brls::Label();
    state_->setFontSize(20);
    state_->setTextColor(palette::text_muted());
    state_->setIsWrapping(true);
    state_->setMarginTop(8);
    text->addView(state_);

    auto* buttons = new brls::Box(brls::Axis::ROW);
    buttons->setMarginTop(32);
    addView(buttons);

    link_ = make_button("app/account/link"_i18n, &brls::BUTTONSTYLE_PRIMARY);
    link_->setMarginRight(20);
    link_->registerClickAction([this](brls::View*) {
        if (awaiting_unlink_) return true;
        LinkView::open(client_, [this] { refresh(); });
        return true;
    });
    buttons->addView(link_);

    unlink_ = make_button("app/account/unlink"_i18n, &brls::BUTTONSTYLE_BORDERED);
    unlink_->registerClickAction([this](brls::View*) {
        if (!awaiting_unlink_) confirm_unlink();
        return true;
    });
    buttons->addView(unlink_);

    refresh();
    timer_.setCallback([this] { refresh(); });
    timer_.start(kRefreshMs);
}

void AccountTab::refresh() {
    using drpc::ipc::LinkState;
    using drpc::ipc::UnlinkResult;
    drpc::ipc::Status status{};
    if (!client_.status(status)) return;

    if (first_refresh_ && status.unlink == UnlinkResult::InProgress) awaiting_unlink_ = true;
    first_refresh_ = false;
    if (awaiting_unlink_ && status.unlink == UnlinkResult::Done) awaiting_unlink_ = false;
    if (awaiting_unlink_ && status.unlink == UnlinkResult::Failed) {
        awaiting_unlink_ = false;
        offer_forget();
    }

    const bool has_name = status.link != LinkState::NoKey && status.username[0] != '\0';
    name_->setText(has_name ? std::string(status.username) : "app/account/not_linked"_i18n);
    const char first = has_name ? status.username[0] : '?';
    initial_->setText(std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(first)))));

    std::string state;
    NVGcolor avatar = palette::accent();
    switch (status.link) {
        case LinkState::Linked:
            state = "app/account/linked"_i18n;
            break;
        case LinkState::NoKey:
            state = "app/account/nokey"_i18n;
            avatar = palette::text_faint();
            break;
        case LinkState::LinkBroken:
            state = "app/account/broken"_i18n;
            avatar = palette::red();
            break;
        case LinkState::Unauthorized:
            state = "app/account/unauthorized"_i18n;
            avatar = palette::red();
            break;
    }
    if (awaiting_unlink_) state = "app/account/unlinking"_i18n;
    state_->setText(state);
    avatar_->setBackgroundColor(avatar);

    link_->setVisibility(status.link != LinkState::Linked ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    unlink_->setVisibility(status.link != LinkState::NoKey ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    keep_focus();
}

void AccountTab::keep_focus() {
    brls::View* focus = brls::Application::getCurrentFocus();
    const bool lost = (focus == link_ && link_->getVisibility() == brls::Visibility::GONE) ||
                      (focus == unlink_ && unlink_->getVisibility() == brls::Visibility::GONE);
    if (!lost) return;
    brls::Application::giveFocus(focus == link_ ? unlink_ : link_);
}

void AccountTab::confirm_unlink() {
    auto* dialog = new brls::Dialog("app/account/unlink_confirm"_i18n);
    dialog->addButton("app/account/cancel"_i18n, [] {});
    dialog->addButton("app/account/unlink"_i18n, [this] {
        if (client_.unlink()) awaiting_unlink_ = true;
        refresh();
    });
    dialog->open();
}

void AccountTab::offer_forget() {
    auto* dialog = new brls::Dialog("app/account/unlink_failed"_i18n);
    dialog->addButton("app/account/cancel"_i18n, [] {});
    dialog->addButton("app/account/forget"_i18n, [this] {
        client_.forget_key();
        refresh();
    });
    dialog->open();
}

}  // namespace app
