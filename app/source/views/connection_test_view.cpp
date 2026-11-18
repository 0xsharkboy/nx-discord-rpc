#include "views/connection_test_view.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include "connection_probe.hpp"
#include "drpc/status_page.hpp"
#include "palette.hpp"
#include "views/bold_label.hpp"
#include "views/qr_view.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr float kDotSide = 14;
constexpr float kQrSide = 168;
constexpr float kSideWidth = 400;
constexpr float kBusyAlpha = 0.5f;

brls::Label* make_label(float size, NVGcolor color) {
    auto* label = new brls::Label();
    label->setFontSize(size);
    label->setTextColor(color);
    label->setIsWrapping(true);
    return label;
}

NVGcolor result_color(diag::Result result) {
    switch (result) {
        case diag::Result::Ok:
            return palette::green();
        case diag::Result::Warning:
            return palette::yellow();
        case diag::Result::Failed:
            return palette::red();
        case diag::Result::Info:
            return palette::accent_light();
        case diag::Result::Pending:
        case diag::Result::Skipped:
            break;
    }
    return palette::text_faint();
}

std::string reason_text(const diag::Step& step) {
    if (diag::reason_has_code(step.reason)) return brls::getStr(diag::reason_key(step.reason), step.code);
    return brls::getStr(diag::reason_key(step.reason));
}

}  // namespace

ConnectionTestView::ConnectionTestView(drpc::Client& client) : client_(client) {
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setPadding(28, 56, 32, 56);

    auto* intro = make_label(18, palette::text_muted());
    intro->setText("app/diag/intro"_i18n);
    intro->setMarginBottom(20);
    content->addView(intro);

    auto* columns = new brls::Box(brls::Axis::ROW);
    content->addView(columns);

    auto* steps = new brls::Box(brls::Axis::COLUMN);
    steps->setGrow(1);
    steps->setShrink(1);
    columns->addView(steps);

    for (size_t i = 0; i < diag::kStepCount; ++i) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::FLEX_START);
        row->setMarginBottom(18);
        steps->addView(row);

        auto* dot = new brls::Box();
        dot->setSize(brls::Size(kDotSide, kDotSide));
        dot->setCornerRadius(kDotSide / 2);
        dot->setShrink(0);
        dot->setMarginTop(7);
        row->addView(dot);

        auto* text = new brls::Box(brls::Axis::COLUMN);
        text->setMarginLeft(16);
        text->setGrow(1);
        text->setShrink(1);
        row->addView(text);

        auto* name = new BoldLabel();
        name->setFontSize(21);
        name->setTextColor(palette::text());
        name->setSingleLine(true);
        name->setText(brls::getStr(diag::step_key(static_cast<diag::StepId>(i))));
        text->addView(name);

        auto* reason = make_label(18, palette::text_muted());
        reason->setMarginTop(4);
        text->addView(reason);

        rows_[i] = {dot, reason};
    }

    again_ = new brls::Button();
    again_->setStyle(&brls::BUTTONSTYLE_BORDERED);
    again_->setText("app/diag/again"_i18n);
    again_->setWidth(260);
    again_->setMarginTop(8);
    again_->registerClickAction([this](brls::View*) {
        start();
        return true;
    });
    steps->addView(again_);

    auto* side = new brls::Box(brls::Axis::COLUMN);
    side->setWidth(kSideWidth);
    side->setShrink(0);
    side->setMarginLeft(40);
    columns->addView(side);

    help_ = new brls::Box(brls::Axis::COLUMN);
    help_->setBackgroundColor(palette::card());
    help_->setCornerRadius(12);
    help_->setPadding(20, 20, 20, 20);
    help_->setMarginBottom(20);
    help_->setAlignItems(brls::AlignItems::CENTER);
    help_->setVisibility(brls::Visibility::GONE);
    side->addView(help_);

    auto* page = make_label(18, palette::text_muted());
    page->setText("app/diag/status_page"_i18n);
    page->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    help_->addView(page);
    auto* url = make_label(18, palette::accent_light());
    // Shown without its scheme, which only takes room; the QR code carries the whole URL.
    std::string_view shown_url = drpc::kStatusPageUrl;
    if (shown_url.starts_with("https://")) shown_url.remove_prefix(8);
    url->setText(std::string(shown_url));
    url->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    url->setMarginTop(4);
    help_->addView(url);
    auto* qr = new QrView(kQrSide);
    qr->setShrink(0);
    qr->setMarginTop(14);
    qr->set_text(drpc::kStatusPageUrl);
    help_->addView(qr);
    auto* hint = make_label(16, palette::text_faint());
    hint->setText("app/diag/status_page_hint"_i18n);
    hint->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    hint->setMarginTop(14);
    help_->addView(hint);

    tip_ = make_label(17, palette::text_muted());
    tip_->setText("app/diag/privacy_tip"_i18n);
    tip_->setVisibility(brls::Visibility::GONE);
    side->addView(tip_);

    setContentView(content);
    start();
}

ConnectionTestView::~ConnectionTestView() { alive_.reset(); }

void ConnectionTestView::open(drpc::Client& client) {
    auto* view = new ConnectionTestView(client);
    view->getAppletFrameItem()->title = "app/diag/title"_i18n;
    brls::Application::pushActivity(new brls::Activity(new brls::AppletFrame(view)));
}

void ConnectionTestView::read_status() {
    // Kept from the last read when the sysmodule cannot be asked (the app then shows its own blocking screen).
    drpc::ipc::Status status{};
    if (client_.status(status)) {
        facts_.status = status;
        facts_.no_status = false;
        status_read_ = true;
    } else if (!status_read_) {
        // Never read: an empty relay host would read as an invalid relay_url.
        facts_.no_status = true;
    }
}

void ConnectionTestView::start() {
    if (running_) return;
    running_ = true;
    const unsigned run = ++run_;
    read_status();
    facts_.network.reset();
    facts_.healthz.reset();
    render();

    // Copied for the worker, which never touches the client or the view.
    const drpc::ipc::RelayHealth health = facts_.status.health;
    const char* raw = facts_.status.relay_host;
    const void* end = std::memchr(raw, '\0', sizeof facts_.status.relay_host);
    std::string host(raw, end ? static_cast<size_t>(static_cast<const char*>(end) - raw) : sizeof facts_.status.relay_host);
    const bool probe = health != drpc::ipc::RelayHealth::NoUrl && !host.empty();
    std::weak_ptr<bool> alive = alive_;
    ConnectionTestView* view = this;  // dereferenced only on the UI thread, once alive is checked
    brls::async([alive, view, run, host = std::move(host), health, probe] {
        const bool connected = network_connected(health);
        brls::sync([alive, view, run, connected] {
            if (!alive.expired()) view->on_network(run, connected);
        });
        if (!connected || !probe) return;
        auto healthz = std::make_shared<diag::Healthz>(probe_healthz(host, health));
        brls::sync([alive, view, run, healthz] {
            if (!alive.expired()) view->on_healthz(run, std::move(*healthz));
        });
    });
}

void ConnectionTestView::on_network(unsigned run, bool connected) {
    if (run != run_) return;
    facts_.network = connected;
    // No Status re-read here: whether a probe follows was decided from the Status read at start, and a
    // newer one (a relay host appearing) would leave the relay step waiting for a probe that never comes.
    render();
}

void ConnectionTestView::on_healthz(unsigned run, diag::Healthz healthz) {
    if (run != run_) return;
    facts_.healthz = std::move(healthz);
    // The latest link state and relay status, now that the relay answered.
    read_status();
    render();
}

void ConnectionTestView::render() {
    const diag::Steps steps = diag::diagnose(facts_);
    for (size_t i = 0; i < steps.size(); ++i) {
        rows_[i].dot->setBackgroundColor(result_color(steps[i].result));
        rows_[i].reason->setText(reason_text(steps[i]));
        rows_[i].reason->setTextColor(steps[i].result == diag::Result::Failed ? palette::text() : palette::text_muted());
    }
    const bool done = diag::finished(steps);
    if (done) running_ = false;
    again_->setAlpha(running_ ? kBusyAlpha : 1.0f);
    const bool help = done && diag::needs_help(steps) && drpc::kStatusPageUrl[0] != '\0';
    help_->setVisibility(help ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    tip_->setVisibility(done ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

}  // namespace app
