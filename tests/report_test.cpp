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
          R"({"name":"Zelda","details":"on Nintendo Switch","elapsed_s":754,"title_id":"0100A0D02127E000"})");
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
