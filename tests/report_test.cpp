#include <doctest/doctest.h>

#include "core/report.hpp"

#include <string>

using namespace drpc;

namespace {
SceneInput game() {
    SceneInput in;
    in.scene = Scene::Game;
    in.title_id = 0x0100A0D02127E000ULL;
    in.game_name = "Zelda";
    in.firmware = "19.0.1";
    return in;
}
Settings base() {
    Settings s;
    s.enabled = true;
    s.private_mode = false;
    s.show_firmware = false;
    s.show_home_menu = false;
    return s;
}
bool valid_utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}
}  // namespace

TEST_CASE("game scene") {
    auto r = render_report(game(), base());
    REQUIRE(r);
    CHECK(r->name == "Zelda");
    CHECK(r->details == "on Nintendo Switch");
    CHECK(r->title_id == std::optional<std::string>{"0100A0D02127E000"});
}

TEST_CASE("private mode masks the game") {
    Settings s = base();
    s.private_mode = true;
    auto r = render_report(game(), s);
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
    CHECK(r->details == "Playing a game");
    CHECK_FALSE(r->title_id);
}

TEST_CASE("home menu") {
    SceneInput in;
    in.scene = Scene::HomeMenu;
    CHECK_FALSE(render_report(in, base()));
    Settings s = base();
    s.show_home_menu = true;
    auto r = render_report(in, s);
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
    CHECK(r->details == "In the HOME Menu");
    CHECK_FALSE(r->title_id);
}

TEST_CASE("disabled shows nothing") {
    Settings s = base();
    s.enabled = false;
    s.show_home_menu = true;
    CHECK_FALSE(render_report(game(), s));
    SceneInput in;
    CHECK_FALSE(render_report(in, s));
}

TEST_CASE("firmware suffix") {
    Settings s = base();
    s.show_firmware = true;
    auto r = render_report(game(), s);
    REQUIRE(r);
    CHECK(r->details == "on Nintendo Switch \xC2\xB7 19.0.1");
    s.private_mode = true;
    CHECK(render_report(game(), s)->details == "Playing a game \xC2\xB7 19.0.1");
    s.private_mode = false;
    s.show_home_menu = true;
    SceneInput in;
    in.firmware = "19.0.1";
    CHECK(render_report(in, s)->details == "In the HOME Menu \xC2\xB7 19.0.1");
}

TEST_CASE("elapsed is always included when given") {
    auto r = render_report(game(), base());
    REQUIRE(r);
    CHECK(report_json(*r, 10).find("\"elapsed_s\":10") != std::string::npos);
    CHECK(report_json(*r, std::nullopt).find("elapsed_s") == std::string::npos);
}

TEST_CASE("a leftover show_elapsed = false in config.ini still yields elapsed_s") {
    const Settings s = parse_settings("show_elapsed = false\n");
    auto r = render_report(game(), s);
    REQUIRE(r);
    CHECK(report_json(*r, 10).find("\"elapsed_s\":10") != std::string::npos);
    CHECK(serialize_settings(s).find("show_elapsed") == std::string::npos);
}

TEST_CASE("truncation on a UTF-8 boundary") {
    std::string name(127, 'a');
    name += "\xE2\x82\xAC";
    name += std::string(70, 'b');
    REQUIRE(name.size() == 200);
    SceneInput in = game();
    in.game_name = name;
    auto r = render_report(in, base());
    REQUIRE(r);
    CHECK(r->name.size() <= 128);
    CHECK(valid_utf8(r->name));
    CHECK(name.compare(0, r->name.size(), r->name) == 0);
    CHECK(r->name.size() == 127);
    CHECK(truncate_utf8("abc", 128) == "abc");
    CHECK(truncate_utf8("abc", 0).empty());
}

TEST_CASE("firmware is applied before truncation") {
    SceneInput in = game();
    Settings s = base();
    s.show_firmware = true;
    in.firmware = std::string(200, 'x');
    auto r = render_report(in, s);
    REQUIRE(r);
    CHECK(r->details.size() <= 128);
}

TEST_CASE("title id below the range") {
    SceneInput in = game();
    in.title_id = 0x0100000000000352ULL;
    auto r = render_report(in, base());
    REQUIRE(r);
    CHECK_FALSE(r->title_id);
    CHECK(r->name == "Zelda");
    in.title_id = 0x0100000000010000ULL;
    CHECK(render_report(in, base())->title_id == std::optional<std::string>{"0100000000010000"});
}

TEST_CASE("empty game name falls back") {
    SceneInput in = game();
    in.game_name.clear();
    auto r = render_report(in, base());
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
}

TEST_CASE("report json") {
    auto r = render_report(game(), base());
    CHECK(report_json(*r, 754) ==
          R"({"name":"Zelda","details":"on Nintendo Switch","elapsed_s":754,"title_id":"0100A0D02127E000",)"
          R"("badge":true})");
    CHECK(report_json(*r, -5).find("\"elapsed_s\":0") != std::string::npos);
    CHECK(report_json(*r, 99999999999LL).find("\"elapsed_s\":31536000") != std::string::npos);
    SceneInput in = game();
    in.game_name = "Say \"hi\" \\ there";
    auto q = render_report(in, base());
    CHECK(report_json(*q, std::nullopt).find(R"("name":"Say \"hi\" \\ there")") != std::string::npos);
}

TEST_CASE("equality ignores elapsed") {
    CHECK(*render_report(game(), base()) == *render_report(game(), base()));
}

namespace {
SceneInput paused(Screen screen = Screen::Unknown) {
    SceneInput in = game();
    in.scene = Scene::Paused;
    in.paused_screen = screen;
    return in;
}
}  // namespace

TEST_CASE("paused details name the screen") {
    const Settings s = base();
    CHECK(render_report(paused(Screen::HomeMenu), s)->details == "Paused \xC2\xB7 HOME Menu");
    CHECK(render_report(paused(Screen::Shop), s)->details == "Paused \xC2\xB7 Nintendo eShop");
    CHECK(render_report(paused(Screen::Album), s)->details == "Paused \xC2\xB7 Album");
    CHECK(render_report(paused(Screen::UserPage), s)->details == "Paused \xC2\xB7 User page");
    CHECK(render_report(paused(Screen::Unknown), s)->details == "Paused");
    auto r = render_report(paused(Screen::Album), s);
    CHECK(r->name == "Zelda");
    CHECK(r->title_id == std::optional<std::string>{"0100A0D02127E000"});
}

TEST_CASE("paused_private_mode_shows_paused_without_screen") {
    Settings s = base();
    s.private_mode = true;
    SceneInput in = paused(Screen::Shop);
    in.playtime_s = 7200;
    s.show_playtime = true;
    auto r = render_report(in, s);
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
    CHECK(r->details == "Paused");
    CHECK_FALSE(r->title_id);
}

TEST_CASE("show_paused_off_renders_as_game") {
    Settings s = base();
    s.show_paused = false;
    auto p = render_report(paused(Screen::Shop), s);
    auto g = render_report(game(), s);
    REQUIRE(p);
    CHECK(*p == *g);
    CHECK(p->details == "on Nintendo Switch");
    s.private_mode = true;
    CHECK(render_report(paused(), s)->details == "Playing a game");
}

TEST_CASE("docked and handheld") {
    Settings s = base();
    s.show_docked = true;
    SceneInput in = game();
    in.docked = true;
    CHECK(render_report(in, s)->details == "on Nintendo Switch \xC2\xB7 Docked");
    in.docked = false;
    CHECK(render_report(in, s)->details == "on Nintendo Switch \xC2\xB7 Handheld");
    in.docked = std::nullopt;
    CHECK(render_report(in, s)->details == "on Nintendo Switch");
    in.docked = true;
    s.show_docked = false;
    CHECK(render_report(in, s)->details == "on Nintendo Switch");
}

TEST_CASE("details_order_docked_playtime_firmware") {
    Settings s = base();
    s.show_docked = true;
    s.show_playtime = true;
    s.show_firmware = true;
    SceneInput in = paused(Screen::HomeMenu);
    in.docked = false;
    in.playtime_s = 547200;
    CHECK(render_report(in, s)->details ==
          "Paused \xC2\xB7 HOME Menu \xC2\xB7 Handheld \xC2\xB7 152 h total \xC2\xB7 19.0.1");
}

TEST_CASE("playtime is skipped when off, unknown, or too short") {
    Settings s = base();
    SceneInput in = game();
    in.playtime_s = 3600;
    CHECK(render_report(in, s)->details == "on Nintendo Switch");
    s.show_playtime = true;
    CHECK(render_report(in, s)->details == "on Nintendo Switch \xC2\xB7 1 h total");
    in.playtime_s = 59;
    CHECK(render_report(in, s)->details == "on Nintendo Switch");
    in.playtime_s = std::nullopt;
    CHECK(render_report(in, s)->details == "on Nintendo Switch");
}

TEST_CASE("private mode drops playtime on a game") {
    Settings s = base();
    s.private_mode = true;
    s.show_playtime = true;
    SceneInput in = game();
    in.playtime_s = 7200;
    CHECK(render_report(in, s)->details == "Playing a game");
}

TEST_CASE("home menu ignores docked and playtime") {
    Settings s = base();
    s.show_home_menu = true;
    s.show_docked = true;
    s.show_playtime = true;
    SceneInput in;
    in.scene = Scene::HomeMenu;
    in.docked = true;
    in.playtime_s = 7200;
    CHECK(render_report(in, s)->details == "In the HOME Menu");
}

TEST_CASE("format_playtime_boundaries") {
    CHECK(format_playtime(0) == "");
    CHECK(format_playtime(59) == "");
    CHECK(format_playtime(60) == "1 min total");
    CHECK(format_playtime(3599) == "59 min total");
    CHECK(format_playtime(3600) == "1 h total");
    CHECK(format_playtime(547200) == "152 h total");
}

TEST_CASE("report_json_includes_icon_when_set") {
    auto r = render_report(game(), base());
    REQUIRE(r);
    CHECK(report_json(*r, 754).find("\"icon\"") == std::string::npos);
    r->icon = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    CHECK(report_json(*r, 754) ==
          R"({"name":"Zelda","details":"on Nintendo Switch","elapsed_s":754,"title_id":"0100A0D02127E000",)"
          R"("icon":"9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08","badge":true})");
}

TEST_CASE("render_report never sets the icon") {
    auto r = render_report(game(), base());
    REQUIRE(r);
    CHECK_FALSE(r->icon);
}

namespace {
Settings with_visibility(TitleVisibility v) {
    Settings s = base();
    s.hidden_titles = {{0x0100000000010000ULL, TitleVisibility::Hidden}, {0x0100A0D02127E000ULL, v}};
    return s;
}
}  // namespace

TEST_CASE("hidden_title_sends_nothing") {
    Settings s = with_visibility(TitleVisibility::Hidden);
    s.show_home_menu = true;
    CHECK_FALSE(render_report(game(), s));
    SceneInput paused = game();
    paused.scene = Scene::Paused;
    paused.paused_screen = Screen::HomeMenu;
    CHECK_FALSE(render_report(paused, s));
    s.private_mode = true;
    CHECK_FALSE(render_report(game(), s));
    // Another title and the HOME Menu are not affected.
    SceneInput other = game();
    other.title_id = 0x0100152000022000ULL;
    CHECK(render_report(other, s));
    SceneInput home;
    home.scene = Scene::HomeMenu;
    CHECK(render_report(home, s));
}

TEST_CASE("private_title_sends_no_id_or_icon") {
    Settings s = with_visibility(TitleVisibility::Private);
    s.show_playtime = true;
    s.show_docked = true;
    SceneInput in = game();
    in.playtime_s = 7200;
    in.docked = true;
    auto r = render_report(in, s);
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
    CHECK(r->details == "Playing a game \xC2\xB7 Docked");
    CHECK_FALSE(r->title_id);
    CHECK_FALSE(r->icon);

    in.scene = Scene::Paused;
    in.paused_screen = Screen::Album;
    r = render_report(in, s);
    REQUIRE(r);
    CHECK(r->name == "Nintendo Switch");
    CHECK(r->details == "Paused \xC2\xB7 Docked");
    CHECK_FALSE(r->title_id);

    // The same output as private mode for this title.
    Settings global = base();
    global.private_mode = true;
    global.show_playtime = true;
    global.show_docked = true;
    CHECK(render_report(in, global) == r);
}

TEST_CASE("a Shown entry or another title's entry renders the game as usual") {
    Settings s = with_visibility(TitleVisibility::Shown);
    auto r = render_report(game(), s);
    REQUIRE(r);
    CHECK(r->name == "Zelda");
    CHECK(r->title_id == std::optional<std::string>{"0100A0D02127E000"});
}

TEST_CASE("the badge flag follows show_badge and is sent only when on") {
    auto s = base();
    s.show_badge = true;
    auto on = render_report(game(), s);
    REQUIRE(on);
    CHECK(on->badge);
    CHECK(report_json(*on, 10).find("\"badge\":true") != std::string::npos);
    s.show_badge = false;
    auto off = render_report(game(), s);
    REQUIRE(off);
    CHECK_FALSE(off->badge);
    CHECK(report_json(*off, 10).find("badge") == std::string::npos);
    s.show_badge = true;
    s.private_mode = true;
    auto priv = render_report(game(), s);
    REQUIRE(priv);
    CHECK(priv->badge);
}

TEST_CASE("a game name that is not valid UTF-8 is repaired before it is sent") {
    SceneInput in = game();
    // Latin-1, a cut sequence, a UTF-16 surrogate and an overlong slash, as a NACP may hold them.
    in.game_name = std::string("Pok") + "\xE9" + "mon " + "\xC3" + "( " + "\xED\xA0\x80" + " " + "\xC0\xAF";
    auto r = render_report(in, base());
    REQUIRE(r);
    CHECK(r->name == "Pok?mon ?( ??? ??");
    CHECK(valid_utf8(r->name));
    CHECK(valid_utf8(report_json(*r, std::nullopt)));

    // Valid text is left alone, and a name cut by the NACP field ends on a whole character.
    CHECK(repair_utf8("Pok\xC3\xA9mon \xE2\x82\xAC \xF0\x9F\x8E\xAE") == "Pok\xC3\xA9mon \xE2\x82\xAC \xF0\x9F\x8E\xAE");
    CHECK(repair_utf8("ab\xE2\x82") == "ab??");
    CHECK(repair_utf8("\xF4\x90\x80\x80") == "????");
}

TEST_CASE("english_game_names picks the English name and falls back to the console name") {
    SceneInput in = game();
    in.game_name = "Zelda : BOTW (fr)";
    in.english_game_name = "Zelda: BOTW";
    auto s = base();
    auto off = render_report(in, s);
    REQUIRE(off);
    CHECK(off->name == "Zelda : BOTW (fr)");

    s.english_game_names = true;
    auto on = render_report(in, s);
    REQUIRE(on);
    CHECK(on->name == "Zelda: BOTW");

    SceneInput no_english = in;
    no_english.english_game_name.clear();
    auto fallback = render_report(no_english, s);
    REQUIRE(fallback);
    CHECK(fallback->name == "Zelda : BOTW (fr)");

    s.private_mode = true;
    auto priv = render_report(in, s);
    REQUIRE(priv);
    CHECK(priv->name == "Nintendo Switch");
    CHECK(report_json(*priv, 10).find("BOTW") == std::string::npos);
}
