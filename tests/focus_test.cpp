#include <doctest/doctest.h>

#include <vector>

#include "core/focus.hpp"

using namespace drpc;

namespace {
constexpr uint64_t kGame = 0x0100000000010000ULL;
constexpr uint64_t kOtherGame = 0x0100000000020000ULL;

FocusEvent app(uint64_t program, bool in_focus) {
    return {kAppletApplication, program, in_focus};
}
FocusEvent applet(uint32_t id, bool in_focus) {
    return {id, 0, in_focus};
}
}  // namespace

TEST_CASE("app_in_focus_is_front") {
    const std::vector<FocusEvent> events{applet(kAppletHomeMenu, true), applet(kAppletHomeMenu, false),
                                         app(kGame, false), app(kGame, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK(focus->app_in_front);
}

TEST_CASE("home_menu_after_app_is_paused_home") {
    const std::vector<FocusEvent> events{app(kGame, true), app(kGame, false), applet(kAppletHomeMenu, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK_FALSE(focus->app_in_front);
    CHECK(focus->screen == Screen::HomeMenu);
}

TEST_CASE("shop_is_paused_shop") {
    const std::vector<FocusEvent> events{app(kGame, true), app(kGame, false), applet(kAppletHomeMenu, true),
                                         applet(kAppletHomeMenu, false), applet(kAppletShop, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK_FALSE(focus->app_in_front);
    CHECK(focus->screen == Screen::Shop);
}

TEST_CASE("album_and_user_page_screens") {
    const std::vector<FocusEvent> album{app(kGame, true), app(kGame, false), applet(kAppletPhotoViewer, true)};
    const auto a = replay_focus(album, kGame);
    REQUIRE(a);
    CHECK(a->screen == Screen::Album);
    const std::vector<FocusEvent> page{app(kGame, true), app(kGame, false), applet(kAppletMyPage, true)};
    const auto p = replay_focus(page, kGame);
    REQUIRE(p);
    CHECK(p->screen == Screen::UserPage);
}

TEST_CASE("other_applet_is_paused_unknown") {
    const std::vector<FocusEvent> events{app(kGame, true), app(kGame, false), applet(0x11, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK_FALSE(focus->app_in_front);
    CHECK(focus->screen == Screen::Unknown);
}

TEST_CASE("overlay_applet_is_ignored") {
    const std::vector<FocusEvent> front{app(kGame, true), applet(kAppletOverlay, true)};
    const auto focus = replay_focus(front, kGame);
    REQUIRE(focus);
    CHECK(focus->app_in_front);
    const std::vector<FocusEvent> only_overlay{applet(kAppletOverlay, true), applet(kAppletOverlay, false)};
    CHECK_FALSE(replay_focus(only_overlay, kGame));
}

TEST_CASE("focus_returns_to_app") {
    const std::vector<FocusEvent> events{app(kGame, true),  app(kGame, false), applet(kAppletHomeMenu, true),
                                         app(kGame, true),  applet(kAppletHomeMenu, false)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK(focus->app_in_front);
}

TEST_CASE("no_focus_events_is_nullopt") {
    CHECK_FALSE(replay_focus({}, kGame));
}

TEST_CASE("app_never_seen_is_nullopt") {
    const std::vector<FocusEvent> events{applet(kAppletHomeMenu, true), applet(kAppletHomeMenu, false),
                                         app(kOtherGame, true)};
    CHECK_FALSE(replay_focus(events, kGame));
}

TEST_CASE("other_program_ignored") {
    const std::vector<FocusEvent> events{app(kGame, true), app(kGame, false), app(kOtherGame, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK_FALSE(focus->app_in_front);
    CHECK(focus->screen == Screen::Unknown);
}

TEST_CASE("stale_foreign_app_under_home_menu") {
    // A foreign application left on the stack (an old session) does not hide the HOME Menu above it.
    const std::vector<FocusEvent> events{app(kOtherGame, true), app(kGame, true), app(kGame, false),
                                         applet(kAppletHomeMenu, true)};
    const auto focus = replay_focus(events, kGame);
    REQUIRE(focus);
    CHECK_FALSE(focus->app_in_front);
    CHECK(focus->screen == Screen::HomeMenu);
}
