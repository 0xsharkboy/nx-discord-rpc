#include "tabs/account_tab.hpp"

#include <cctype>
#include <cstring>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#ifndef __SWITCH__
#include <filesystem>
#endif

#include "avatar_cache.hpp"
#include "avatar_download.hpp"
#include "drpc/discord_ids.hpp"
#include "palette.hpp"
#include "views/bold_label.hpp"
#include "views/link_view.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;
constexpr float kAvatarSide = 88;
constexpr float kPhotoSide = kAvatarSide - 8;

// Shared by every AccountTab, since the tab frame builds a new one on each visit. UI thread only.
struct AvatarState {
    std::set<std::string> attempted;  // keys downloaded, or tried, in this run
    bool in_flight = false;
    bool cleared = false;     // the cache was cleared since the last linked status
    uint32_t generation = 0;  // bumped on unlink: drops the downloads started before it
    uint32_t stores = 0;      // bumped on every picture stored
};

AvatarState& avatar_state() {
    static AvatarState state;
    return state;
}

AvatarCache& avatar_cache() {
#ifdef __SWITCH__
    static AvatarCache cache("sdmc:/config/nx-discord-rpc");
#else
    static AvatarCache cache((std::filesystem::temp_directory_path() / "nx-discord-rpc").string());
#endif
    return cache;
}

template <size_t N>
std::string_view field(const char (&s)[N]) {
    const void* end = std::memchr(s, '\0', N);
    return {s, end ? static_cast<size_t>(static_cast<const char*>(end) - s) : N};
}

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

    // Image clips to its rounded bounds, so half the side as radius draws a disc inside the ring.
    photo_ = new brls::Image();
    photo_->setSize(brls::Size(kPhotoSide, kPhotoSide));
    photo_->setCornerRadius(kPhotoSide / 2);
    photo_->setScalingType(brls::ImageScalingType::FILL);
    photo_->setVisibility(brls::Visibility::GONE);
    avatar_->addView(photo_);

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
    std::string title = "app/account/heading"_i18n;
    if (has_name) title = status.username;
    if (status.link == LinkState::NoKey) title = "app/account/not_linked"_i18n;
    name_->setText(title);
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
        case LinkState::Outdated:  // until its own line: shown like a refused key
        case LinkState::Unauthorized:
            state = "app/account/unauthorized"_i18n;
            avatar = palette::red();
            break;
    }
    if (awaiting_unlink_) state = "app/account/unlinking"_i18n;
    state_->setText(state);
    avatar_->setBackgroundColor(avatar);
    refresh_photo(status);

    link_->setVisibility(status.link != LinkState::Linked ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    unlink_->setVisibility(status.link != LinkState::NoKey ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    keep_focus();
}

void AccountTab::refresh_photo(const drpc::ipc::Status& status) {
    AvatarState& st = avatar_state();
    if (status.link == drpc::ipc::LinkState::NoKey) {
        if (!st.cleared) {
            avatar_cache().clear();
            st.attempted.clear();
            ++st.generation;
            st.cleared = true;
        }
        wanted_key_.clear();
        hide_photo();
        return;
    }
    st.cleared = false;

    const std::string_view id = field(status.discord_id);
    const std::string_view avatar = field(status.avatar);
    const std::optional<std::string> path = drpc::avatar_cdn_path(id, avatar);
    if (!path) {
        wanted_key_.clear();
        hide_photo();
        return;
    }
    const std::string key = avatar_cache_key(id, avatar);
    // Read the card only when the key changes or a download stored something, not on every tick.
    if (key != wanted_key_ || st.stores != seen_stores_) {
        wanted_key_ = key;
        seen_stores_ = st.stores;
        if (shown_key_ != key) {
            if (auto png = avatar_cache().load(key)) show_photo(*png, key);
            else hide_photo();
        }
    }
    if (shown_key_ != key && !st.in_flight && st.attempted.insert(key).second) start_download(key, *path);
}

void AccountTab::start_download(const std::string& key, const std::string& path) {
    AvatarState& st = avatar_state();
    st.in_flight = true;
    const uint32_t generation = st.generation;
    std::weak_ptr<bool> alive = alive_;
    AccountTab* tab = this;  // dereferenced only on the UI thread, once alive is checked
    brls::async([key, path, generation, alive, tab] {
        std::shared_ptr<const std::vector<uint8_t>> png;
        if (auto bytes = download_avatar(path)) png = std::make_shared<const std::vector<uint8_t>>(std::move(*bytes));
        brls::sync([key, generation, alive, tab, png] {
            AvatarState& st = avatar_state();
            st.in_flight = false;
            if (!png || generation != st.generation) return;
            if (avatar_cache().store(key, *png)) ++st.stores;
            if (alive.expired() || tab->wanted_key_ != key) return;
            tab->show_photo(*png, key);
            tab->seen_stores_ = st.stores;
        });
    });
}

void AccountTab::show_photo(std::span<const uint8_t> png, const std::string& key) {
    photo_->clear();
    photo_->setImageFromMem(png.data(), static_cast<int>(png.size()));
    if (photo_->getTexture() == 0) {
        hide_photo();
        return;
    }
    shown_key_ = key;
    photo_->setVisibility(brls::Visibility::VISIBLE);
    initial_->setVisibility(brls::Visibility::GONE);
}

void AccountTab::hide_photo() {
    if (shown_key_.empty() && photo_->getVisibility() == brls::Visibility::GONE) return;
    shown_key_.clear();
    photo_->clear();
    photo_->setVisibility(brls::Visibility::GONE);
    initial_->setVisibility(brls::Visibility::VISIBLE);
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
