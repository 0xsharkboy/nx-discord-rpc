#include "tabs/about_tab.hpp"

#include <string>

#include "drpc/protocol.hpp"
#include "palette.hpp"
#include "views/bold_label.hpp"

using namespace brls::literals;

namespace app {

namespace {

constexpr char kRepoUrl[] = "https://github.com/0xsharkboy/nx-discord-rpc";

brls::DetailCell* make_row(const std::string& title, const std::string& value) {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(value);
    cell->setDetailTextColor(palette::text_muted());
    return cell;
}

}  // namespace

AboutTab::AboutTab(drpc::Client& client) {
    auto* list = new brls::Box(brls::Axis::COLUMN);
    list->setPadding(28, 56, 32, 56);

    auto* hero = new brls::Box(brls::Axis::ROW);
    hero->setAlignItems(brls::AlignItems::CENTER);
    hero->setMarginBottom(16);
    list->addView(hero);

    auto* icon = new brls::Image();
    icon->setSize(brls::Size(96, 96));
    icon->setScalingType(brls::ImageScalingType::FILL);
    icon->setCornerRadius(20);
    icon->setImageFromRes("img/app_icon.jpg");
    hero->addView(icon);

    auto* names = new brls::Box(brls::Axis::COLUMN);
    names->setMarginLeft(24);
    hero->addView(names);
    auto* title = new BoldLabel();
    title->setFontSize(32);
    title->setTextColor(palette::text());
    title->setText("app/title"_i18n);
    names->addView(title);
    auto* tagline = new brls::Label();
    tagline->setFontSize(20);
    tagline->setTextColor(palette::text_muted());
    tagline->setMarginTop(6);
    tagline->setText("app/about/tagline"_i18n);
    names->addView(tagline);

    drpc::ipc::Status status{};
    client.status(status);
    list->addView(make_row("app/about/app_version"_i18n, APP_VERSION_STRING));
    list->addView(make_row("app/about/sysmodule_version"_i18n, status.sysmodule_version));
    list->addView(make_row("app/about/protocol_version"_i18n, std::to_string(drpc::ipc::kProtocolVersion)));
    list->addView(make_row("app/about/source"_i18n, kRepoUrl));

    auto* privacy = new brls::Header();
    privacy->setTitle("app/about/privacy"_i18n);
    privacy->setMarginTop(12);
    list->addView(privacy);
    auto* body = new brls::Label();
    body->setFontSize(20);
    body->setTextColor(palette::text_muted());
    body->setIsWrapping(true);
    body->setLineHeight(1.4f);
    body->setText("app/about/privacy_body"_i18n);
    body->setMargins(16, 4, 0, 4);
    list->addView(body);

    setContentView(list);
}

}  // namespace app
