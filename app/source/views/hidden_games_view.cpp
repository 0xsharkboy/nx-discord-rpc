#include "views/hidden_games_view.hpp"

#include <set>
#include <utility>

#include "drpc/hidden_titles.hpp"
#include "installed_games.hpp"
#include "palette.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr brls::Time kRefreshMs = 1000;
constexpr float kDisabledAlpha = 0.4f;
constexpr float kRowHeight = 84;
constexpr float kIconSize = 56;

std::vector<std::string> choices() {
    // Indexed by TitleVisibility.
    return {"app/hidden/shown"_i18n, "app/hidden/private"_i18n, "app/hidden/hidden"_i18n};
}

brls::View* make_icon(const RgbaIcon& rgba) {
    auto* frame = new brls::Box();
    frame->setSize(brls::Size(kIconSize, kIconSize));
    frame->setMarginRight(20);
    frame->setCornerRadius(6);
    frame->setBackgroundColor(palette::card());
    frame->setShrink(0);
    if (rgba.pixels.empty()) return frame;
    const int texture = nvgCreateImageRGBA(brls::Application::getNVGContext(), rgba.width, rgba.height, 0,
                                           rgba.pixels.data());
    if (texture == 0) return frame;
    auto* image = new brls::Image();
    image->setSize(brls::Size(kIconSize, kIconSize));
    image->setScalingType(brls::ImageScalingType::FILL);
    image->setCornerRadius(6);
    image->innerSetImage(texture);  // the Image frees it
    frame->addView(image);
    return frame;
}

}  // namespace

HiddenGamesView::HiddenGamesView(drpc::Client& client)
    : client_(client), list_(std::make_unique<drpc::ipc::HiddenList>()) {
    content_ = new brls::Box(brls::Axis::COLUMN);
    content_->setPadding(32, 56, 40, 56);

    auto* intro = new brls::Label();
    intro->setText("app/hidden/intro"_i18n);
    intro->setFontSize(18);
    intro->setTextColor(palette::text_muted());
    intro->setIsWrapping(true);
    intro->setMarginBottom(16);
    content_->addView(intro);

    status_ = new brls::Box(brls::Axis::ROW);
    status_->setFocusable(true);
    status_->setHideHighlight(true);
    status_->setHideClickAnimation(true);
    status_->setPaddingTop(24);
    status_text_ = new brls::Label();
    status_text_->setText("app/hidden/loading"_i18n);
    status_text_->setFontSize(20);
    status_text_->setTextColor(palette::text_faint());
    status_->addView(status_text_);
    content_->addView(status_);

    setContentView(content_);

    refresh();
    load_games();
    refresh_.setCallback([this] { refresh(); });
    refresh_.start(kRefreshMs);
}

HiddenGamesView::~HiddenGamesView() {
    cancel_->store(true);
    alive_.reset();
}

void HiddenGamesView::open(drpc::Client& client) {
    auto* view = new HiddenGamesView(client);
    view->getAppletFrameItem()->title = "app/hidden/title"_i18n;
    brls::Application::pushActivity(new brls::Activity(new brls::AppletFrame(view)));
}

void HiddenGamesView::load_games() {
    std::weak_ptr<bool> alive = alive_;
    std::shared_ptr<std::atomic<bool>> cancel = cancel_;
    HiddenGamesView* view = this;  // dereferenced only on the UI thread, once alive is checked
    brls::async([alive, cancel, view] {
        auto games = std::make_shared<const std::vector<InstalledGame>>(list_installed_games(*cancel));
        brls::sync([alive, view, games] {
            if (alive.expired()) return;
            view->populate(*games);
        });
    });
}

void HiddenGamesView::populate(const std::vector<InstalledGame>& games) {
    refresh();
    std::set<uint64_t> seen;
    for (const InstalledGame& game : games) {
        if (!seen.insert(game.title_id).second) continue;
        add_row(game.title_id, game.name.empty() ? title_id_hex(game.title_id) : game.name, game.icon);
    }
    // Listed games that are not installed any more stay reachable, so their slot can be freed.
    if (loaded_) {
        for (uint32_t i = 0; i < list_->count && i < drpc::ipc::kMaxHiddenTitles; ++i) {
            const uint64_t id = list_->items[i].title_id;
            if (seen.insert(id).second)
                add_row(id, brls::getStr("app/hidden/not_installed", title_id_hex(id)), RgbaIcon{});
        }
    }

    if (rows_.empty()) {
        status_text_->setText("app/hidden/empty"_i18n);
        return;
    }
    if (brls::Application::getCurrentFocus() == status_) brls::Application::giveFocus(rows_.front().cell);
    status_->setFocusable(false);
    status_->setVisibility(brls::Visibility::GONE);
    refresh();
}

void HiddenGamesView::add_row(uint64_t title_id, const std::string& name, const RgbaIcon& icon) {
    auto* cell = new brls::SelectorCell();
    const int selection = static_cast<int>(drpc::visibility_of(*list_, title_id));
    cell->init(
        name, choices(), selection, [this, title_id, cell](int chosen) { choose(title_id, cell, chosen); },
        [this](int) { show_pending_message(); });
    cell->setHeight(kRowHeight);
    cell->title->setSingleLine(true);
    cell->addView(make_icon(icon), 0);
    content_->addView(cell);
    rows_.push_back({title_id, cell});
}

void HiddenGamesView::choose(uint64_t title_id, brls::SelectorCell* cell, int selection) {
    if (selection < 0 || selection > static_cast<int>(drpc::TitleVisibility::Hidden)) return;
    const auto want = static_cast<drpc::TitleVisibility>(selection);
    // Start from the sysmodule's list, never from the rows: the overlay may have changed it meanwhile.
    auto next = std::make_unique<drpc::ipc::HiddenList>();
    if (!client_.hidden(*next)) {
        pending_message_ = "app/hidden/save_failed"_i18n;
        cell->setSelection(static_cast<int>(drpc::visibility_of(*list_, title_id)), true);
        return;
    }
    const drpc::TitleVisibility before = drpc::visibility_of(*next, title_id);
    if (drpc::set_visibility(*next, title_id, want) == drpc::SetVisibility::Full) {
        pending_message_ = "app/hidden/full"_i18n;
        cell->setSelection(static_cast<int>(before), true);
        return;
    }
    if (!client_.set_hidden(*next)) pending_message_ = "app/hidden/save_failed"_i18n;
    refresh();
}

// The dropdown pops its own activity after the choice: a dialog opened earlier would be the one popped.
void HiddenGamesView::show_pending_message() {
    if (pending_message_.empty()) return;
    auto* dialog = new brls::Dialog(pending_message_);
    pending_message_.clear();
    dialog->addButton("app/settings/close"_i18n, [] {});
    dialog->open();
}

void HiddenGamesView::refresh() {
    loaded_ = client_.hidden(*list_);
    for (const Row& row : rows_) {
        row.cell->setAlpha(loaded_ ? 1.0f : kDisabledAlpha);
        if (!loaded_) continue;  // keep the last known state, dimmed
        const int want = static_cast<int>(drpc::visibility_of(*list_, row.title_id));
        if (row.cell->getSelection() != want) row.cell->setSelection(want, true);
    }
}

}  // namespace app
