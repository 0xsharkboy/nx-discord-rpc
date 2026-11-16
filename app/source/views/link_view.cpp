#include "views/link_view.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "palette.hpp"
#include "presentation.hpp"
#include "views/bold_label.hpp"
#include "views/qr_view.hpp"

using namespace brls::literals;

namespace app {

enum class BadgeKind { Success, Warning, Error };

class ResultBadge : public brls::View {
public:
    ResultBadge() { setSize(brls::Size(96, 96)); }

    void set_kind(BadgeKind kind) { kind_ = kind; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*) override {
        const float cx = x + width / 2, cy = y + height / 2, r = std::min(width, height) / 2;
        NVGcolor color = palette::green();
        if (kind_ == BadgeKind::Warning) color = palette::yellow();
        if (kind_ == BadgeKind::Error) color = palette::red();

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, color);
        nvgFill(vg);

        nvgSave(vg);
        nvgStrokeColor(vg, nvgRGB(0xFF, 0xFF, 0xFF));
        nvgStrokeWidth(vg, r * 0.16f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgBeginPath(vg);
        switch (kind_) {
            case BadgeKind::Success:
                nvgMoveTo(vg, cx - r * 0.38f, cy + r * 0.02f);
                nvgLineTo(vg, cx - r * 0.10f, cy + r * 0.30f);
                nvgLineTo(vg, cx + r * 0.40f, cy - r * 0.26f);
                break;
            case BadgeKind::Warning:
                nvgMoveTo(vg, cx, cy - r * 0.42f);
                nvgLineTo(vg, cx, cy + r * 0.10f);
                nvgMoveTo(vg, cx, cy + r * 0.40f);
                nvgLineTo(vg, cx, cy + r * 0.41f);
                break;
            case BadgeKind::Error:
                nvgMoveTo(vg, cx - r * 0.30f, cy - r * 0.30f);
                nvgLineTo(vg, cx + r * 0.30f, cy + r * 0.30f);
                nvgMoveTo(vg, cx + r * 0.30f, cy - r * 0.30f);
                nvgLineTo(vg, cx - r * 0.30f, cy + r * 0.30f);
                break;
        }
        nvgStroke(vg);
        nvgRestore(vg);
    }

private:
    BadgeKind kind_ = BadgeKind::Success;
};

namespace {

constexpr brls::Time kPollMs = 1000;

brls::Label* make_label(float size, NVGcolor color, bool wrap = false) {
    auto* label = new brls::Label();
    label->setFontSize(size);
    label->setTextColor(color);
    if (wrap) {
        label->setIsWrapping(true);
    } else {
        label->setSingleLine(true);
    }
    return label;
}

brls::Button* make_button(const std::string& text, const brls::ButtonStyle* style) {
    auto* button = new brls::Button();
    button->setStyle(style);
    button->setText(text);
    button->setWidth(240);
    return button;
}

brls::Box* make_screen() {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setJustifyContent(brls::JustifyContent::CENTER);
    box->setGrow(1);
    box->setVisibility(brls::Visibility::GONE);
    return box;
}

}  // namespace

LinkView::LinkView(drpc::Client& client, std::function<void()> on_close)
    : brls::Box(brls::Axis::COLUMN), client_(client), on_close_(std::move(on_close)) {
    setGrow(1);
    setPadding(24, 64, 24, 64);

    busy_ = make_screen();
    auto* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    spinner->setWidth(72);
    spinner->setHeight(72);
    busy_->addView(spinner);
    auto* starting = make_label(24, palette::text_muted());
    starting->setText("app/link/starting"_i18n);
    starting->setMarginTop(28);
    busy_->addView(starting);
    busy_cancel_ = make_button("app/link/cancel"_i18n, &brls::BUTTONSTYLE_BORDERED);
    busy_cancel_->setMarginTop(40);
    busy_cancel_->registerClickAction([this](brls::View*) {
        cancel();
        return true;
    });
    busy_->addView(busy_cancel_);
    addView(busy_);

    pending_ = make_screen();
    pending_->setAxis(brls::Axis::ROW);
    qr_ = new QrView(340);
    qr_->setShrink(0);
    pending_->addView(qr_);
    auto* info = new brls::Box(brls::Axis::COLUMN);
    info->setMarginLeft(56);
    info->setWidth(460);
    info->setShrink(1);
    pending_->addView(info);
    auto* scan = make_label(20, palette::text_muted(), true);
    scan->setText("app/link/scan"_i18n);
    info->addView(scan);
    uri_ = make_label(22, palette::accent_light(), true);
    uri_->setMarginTop(8);
    info->addView(uri_);
    auto* enter = make_label(20, palette::text_muted());
    enter->setText("app/link/enter"_i18n);
    enter->setMarginTop(28);
    info->addView(enter);
    code_ = new BoldLabel();
    code_->setFontSize(60);
    code_->setTextColor(palette::text());
    code_->setSingleLine(true);
    code_->setMarginTop(6);
    info->addView(code_);
    expires_ = make_label(20, palette::text_faint());
    expires_->setMarginTop(4);
    info->addView(expires_);
    cancel_ = make_button("app/link/cancel"_i18n, &brls::BUTTONSTYLE_BORDERED);
    cancel_->setMarginTop(36);
    cancel_->registerClickAction([this](brls::View*) {
        cancel();
        return true;
    });
    info->addView(cancel_);
    addView(pending_);

    result_ = make_screen();
    badge_ = new ResultBadge();
    result_->addView(badge_);
    result_title_ = new BoldLabel();
    result_title_->setFontSize(32);
    result_title_->setTextColor(palette::text());
    result_title_->setSingleLine(true);
    result_title_->setMarginTop(28);
    result_->addView(result_title_);
    result_body_ = make_label(22, palette::text_muted(), true);
    result_body_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    result_body_->setMaxWidth(720);
    result_body_->setMarginTop(12);
    result_->addView(result_body_);
    auto* buttons = new brls::Box(brls::Axis::ROW);
    buttons->setMarginTop(40);
    retry_ = make_button("app/link/retry"_i18n, &brls::BUTTONSTYLE_PRIMARY);
    retry_->setMarginRight(20);
    retry_->registerClickAction([this](brls::View*) {
        retry();
        return true;
    });
    buttons->addView(retry_);
    close_ = make_button("app/link/close"_i18n, &brls::BUTTONSTYLE_BORDERED);
    close_->registerClickAction([this](brls::View*) {
        close();
        return true;
    });
    buttons->addView(close_);
    result_->addView(buttons);
    addView(result_);

    registerAction(
        "hints/back"_i18n, brls::BUTTON_B,
        [this](brls::View*) {
            if (screen_ == Screen::Result)
                close();
            else
                cancel();
            return true;
        },
        false, false, brls::SOUND_BACK);

}

void LinkView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    if (started_) return;
    started_ = true;
    start();
    timer_.setCallback([this] { poll(); });
    timer_.start(kPollMs);
}

void LinkView::open(drpc::Client& client, std::function<void()> on_close) {
    auto* view = new LinkView(client, std::move(on_close));
    view->getAppletFrameItem()->title = "app/link/title"_i18n;
    brls::Application::pushActivity(new brls::Activity(new brls::AppletFrame(view)));
}

void LinkView::start() {
    if (closing_) return;
    seen_active_ = false;
    drpc::ipc::Link link{};
    const bool known = client_.link(link);
    const bool active = known && (link.phase == drpc::ipc::LinkPhase::Starting ||
                                  link.phase == drpc::ipc::LinkPhase::Pending);
    if (!active && !client_.start_link()) {
        link = {};
        link.phase = drpc::ipc::LinkPhase::Failed;
        link.failure = drpc::ipc::LinkFailure::Other;
        render(link);
        return;
    }
    poll();
}

void LinkView::poll() {
    if (closing_) return;
    drpc::ipc::Link link{};
    if (!client_.link(link)) {
        link = {};
        link.phase = drpc::ipc::LinkPhase::Failed;
        link.failure = drpc::ipc::LinkFailure::Other;
    }
    render(link);
}

void LinkView::render(const drpc::ipc::Link& link) {
    using drpc::ipc::LinkPhase;
    switch (link.phase) {
        case LinkPhase::Idle:
            if (screen_ == Screen::Result) break;
            if (seen_active_) {
                close();
            } else {
                drpc::ipc::Link failed{};
                failed.phase = LinkPhase::Failed;
                failed.failure = drpc::ipc::LinkFailure::Other;
                show_result(failed);
            }
            break;
        case LinkPhase::Starting:
            seen_active_ = true;
            show(Screen::Busy);
            break;
        case LinkPhase::Pending:
            seen_active_ = true;
            qr_->set_text(link.verification_uri);
            uri_->setText(link.verification_uri);
            code_->setText(link.user_code);
            expires_->setText(brls::getStr("app/link/expires", link_countdown(link.seconds_left)));
            show(Screen::Pending);
            break;
        case LinkPhase::Linked:
        case LinkPhase::Denied:
        case LinkPhase::Expired:
        case LinkPhase::Failed:
            show_result(link);
            break;
    }
}

void LinkView::show_result(const drpc::ipc::Link& link) {
    using drpc::ipc::LinkFailure;
    using drpc::ipc::LinkPhase;
    const bool fresh =
        screen_ != Screen::Result || link.phase != result_phase_ || link.failure != result_failure_;
    result_phase_ = link.phase;
    result_failure_ = link.failure;
    const auto now = std::chrono::steady_clock::now();
    if (fresh) retry_at_ = now;

    BadgeKind kind = BadgeKind::Error;
    std::string title = "app/link/failed_title"_i18n;
    std::string body;
    switch (link.phase) {
        case LinkPhase::Linked:
            kind = BadgeKind::Success;
            title = brls::getStr("app/link/linked_title", std::string(link.username));
            body = "app/link/linked_body"_i18n;
            break;
        case LinkPhase::Denied:
            title = "app/link/denied_title"_i18n;
            body = "app/link/denied_body"_i18n;
            break;
        case LinkPhase::Expired:
            kind = BadgeKind::Warning;
            title = "app/link/expired_title"_i18n;
            body = "app/link/expired_body"_i18n;
            break;
        default:
            switch (link.failure) {
                case LinkFailure::RateLimited: {
                    kind = BadgeKind::Warning;
                    if (fresh) retry_at_ = now + std::chrono::seconds(link.retry_after_s);
                    const auto left = std::chrono::duration_cast<std::chrono::seconds>(retry_at_ - now).count();
                    body = left > 0 ? brls::getStr("app/link/rate_limited_body",
                                                   retry_minutes(static_cast<uint32_t>(left)))
                                    : "app/link/rate_limited_ready"_i18n;
                    break;
                }
                case LinkFailure::Unreachable:
                    body = "app/link/unreachable_body"_i18n;
                    break;
                case LinkFailure::Interrupted:
                    kind = BadgeKind::Warning;
                    title = "app/link/interrupted_title"_i18n;
                    body = "app/link/interrupted_body"_i18n;
                    break;
                case LinkFailure::None:
                case LinkFailure::Other:
                    body = "app/link/other_body"_i18n;
                    break;
            }
            break;
    }
    const bool success = link.phase == LinkPhase::Linked;
    const bool can_retry = now >= retry_at_;
    badge_->set_kind(kind);
    result_title_->setText(title);
    result_body_->setText(body);
    retry_->setVisibility(success ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    retry_->setState(can_retry ? brls::ButtonState::ENABLED : brls::ButtonState::DISABLED);
    if (fresh) screen_ = Screen::None;
    show(Screen::Result);
}

void LinkView::retry() {
    if (screen_ != Screen::Result || std::chrono::steady_clock::now() < retry_at_) return;
    start();
}

void LinkView::show(Screen screen) {
    if (screen == screen_) return;
    screen_ = screen;
    busy_->setVisibility(screen == Screen::Busy ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    pending_->setVisibility(screen == Screen::Pending ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    result_->setVisibility(screen == Screen::Result ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    brls::View* focus = busy_cancel_;
    if (screen == Screen::Pending) focus = cancel_;
    if (screen == Screen::Result) {
        const bool retry_ready = retry_->getVisibility() == brls::Visibility::VISIBLE &&
                                 std::chrono::steady_clock::now() >= retry_at_;
        focus = retry_ready ? retry_ : close_;
    }
    brls::Application::giveFocus(focus);
}

void LinkView::cancel() {
    client_.cancel_link();
    close();
}

void LinkView::close() {
    if (closing_) return;
    closing_ = true;
    timer_.stop();
    auto on_close = on_close_;
    brls::Application::popActivity(brls::TransitionAnimation::FADE, [on_close] {
        if (on_close) on_close();
    });
}

}  // namespace app
