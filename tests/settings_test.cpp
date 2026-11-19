#include <doctest/doctest.h>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/settings.hpp"

namespace {
const std::string kApiKey = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNO-_";
}

TEST_CASE("defaults on empty or garbage input") {
    CHECK(drpc::parse_settings("") == drpc::Settings{});
    using namespace std::string_view_literals;
    CHECK(drpc::parse_settings("\x00\xff garbage\n=\nenabled=maybe"sv) == drpc::Settings{});
}

TEST_CASE("paused, docked and playtime default to true, false, false") {
    const auto s = drpc::parse_settings("enabled = 1\n");
    CHECK(s.show_paused);
    CHECK_FALSE(s.show_docked);
    CHECK_FALSE(s.show_playtime);
}

TEST_CASE("paused, docked and playtime parse and serialize") {
    const auto s = drpc::parse_settings("show_paused = false\nshow_docked = true\nshow_playtime = 1\n");
    CHECK_FALSE(s.show_paused);
    CHECK(s.show_docked);
    CHECK(s.show_playtime);
    const std::string out = drpc::serialize_settings(s);
    CHECK(out.find("show_paused = false\n") != std::string::npos);
    CHECK(out.find("show_docked = true\n") != std::string::npos);
    CHECK(out.find("show_playtime = true\n") != std::string::npos);
    CHECK(drpc::parse_settings(out) == s);
}

TEST_CASE("round trip") {
    drpc::Settings s;
    s.private_mode = true;
    s.show_firmware = true;
    CHECK(drpc::parse_settings(drpc::serialize_settings(s)) == s);
}

TEST_CASE("parses CRLF, comments and spacing") {
    auto s = drpc::parse_settings("# c\r\nenabled = 0\r\n; c\r\nshow_home_menu=true\r\n");
    CHECK_FALSE(s.enabled);
    CHECK(s.show_home_menu);
}

TEST_CASE("redaction") {
    const std::string_view both[] = {"KEY-A", "KEY-B"};
    CHECK(drpc::redact("a KEY-A b KEY-B c KEY-A", both) == "a [secret] b [secret] c [secret]");
    const std::string_view with_empty[] = {"", "KEY-A"};
    CHECK(drpc::redact("abc KEY-A", with_empty) == "abc [secret]");
    CHECK(drpc::redact("abc", std::span<const std::string_view>{}) == "abc");
}

TEST_CASE("title id formatting") {
    CHECK(drpc::format_title_id(0x0100000000D15C0D) == "0100000000D15C0D");
    CHECK(drpc::format_title_id(0) == "0000000000000000");
}

TEST_CASE("a UTF-8 BOM before the first key is ignored") {
    const auto s = drpc::parse_settings("\xEF\xBB\xBFprivate_mode = true\r\nenabled = false\r\n");
    CHECK(s.private_mode);
    CHECK_FALSE(s.enabled);
}

TEST_CASE("debug log is off by default and round-trips") {
    CHECK_FALSE(drpc::Settings{}.debug_log);
    CHECK(drpc::parse_settings("debug_log = true").debug_log);
    drpc::Settings s;
    s.debug_log = true;
    CHECK(drpc::serialize_settings(s).find("debug_log = true\n") != std::string::npos);
    CHECK(drpc::parse_settings(drpc::serialize_settings(s)) == s);
}

TEST_CASE("relay defaults") {
    const drpc::Settings s = drpc::parse_settings("");
    CHECK(s.relay_url == "https://nx-discord-rpc.0xsharkboy.dev");
    CHECK(s.relay_url == drpc::kDefaultRelayUrl);
    CHECK(s.api_key.empty());
    CHECK(s.admin_key.empty());
}

TEST_CASE("relay settings round-trip") {
    drpc::Settings s;
    s.relay_url = "https://relay.example.org";
    s.api_key = kApiKey;
    s.admin_key = "0123456789abcdef+/=";
    CHECK(drpc::parse_settings(drpc::serialize_settings(s)) == s);
}

TEST_CASE("normalize_api_key") {
    REQUIRE(kApiKey.size() == 43);
    CHECK(drpc::normalize_api_key(kApiKey) == kApiKey);
    CHECK(drpc::normalize_api_key("\"" + kApiKey + "\"") == kApiKey);
    CHECK(drpc::normalize_api_key("'" + kApiKey + "'\n") == kApiKey);
    CHECK(drpc::normalize_api_key("\xEF\xBB\xBF  " + kApiKey + "\r\n") == kApiKey);
    CHECK_FALSE(drpc::normalize_api_key(kApiKey.substr(1)));
    CHECK_FALSE(drpc::normalize_api_key(kApiKey + "a"));
    for (const char bad : {'+', '/', '='}) {
        std::string k = kApiKey;
        k[10] = bad;
        CHECK_FALSE(drpc::normalize_api_key(k));
    }
    CHECK_FALSE(drpc::normalize_api_key(""));
}

TEST_CASE("normalize_admin_key") {
    const std::string real = "AbCdEfGhIjKlMnOpQrStUvWxYz0123456789+/abcde=";
    REQUIRE(real.size() == 44);
    CHECK(drpc::normalize_admin_key(real) == real);
    CHECK(drpc::normalize_admin_key("\"" + real + "\"\n") == real);
    CHECK_FALSE(drpc::normalize_admin_key("abcdefgh ijklmnopqrst"));
    CHECK_FALSE(drpc::normalize_admin_key(std::string(15, 'a')));
    CHECK(drpc::normalize_admin_key(std::string(16, 'a')));
    CHECK(drpc::normalize_admin_key(std::string(128, 'a')));
    CHECK_FALSE(drpc::normalize_admin_key(std::string(129, 'a')));
}

TEST_CASE("invalid keys in config.ini read as empty") {
    const auto s = drpc::parse_settings("api_key = nope\nadmin_key = short\n");
    CHECK(s.api_key.empty());
    CHECK(s.admin_key.empty());
    const auto ok = drpc::parse_settings("api_key = \"" + kApiKey + "\"\nadmin_key = 0123456789abcdef\n");
    CHECK(ok.api_key == kApiKey);
    CHECK(ok.admin_key == "0123456789abcdef");
}

TEST_CASE("unknown legacy keys are ignored") {
    CHECK(drpc::parse_settings("token = abc\napplication_id = 123\nicon_url_template = x\n") == drpc::Settings{});
}

TEST_CASE("username round trips and rejects control characters") {
    drpc::Settings s;
    s.username = "test_rpc";
    CHECK(drpc::parse_settings(drpc::serialize_settings(s)).username == "test_rpc");
    CHECK(drpc::parse_settings("username = bad\x01name\n").username.empty());
    CHECK(drpc::parse_settings("username = " + std::string(40, 'a') + "\n").username == std::string(32, 'a'));
}

TEST_CASE("discord id and avatar round trip and drop invalid values") {
    drpc::Settings s;
    s.discord_id = "80351110224678912";
    s.avatar = "a_0123456789abcdef0123456789abcdef";
    const drpc::Settings back = drpc::parse_settings(drpc::serialize_settings(s));
    CHECK(back.discord_id == s.discord_id);
    CHECK(back.avatar == s.avatar);
    const drpc::Settings bad = drpc::parse_settings("discord_id = abc\navatar = ZZ\n");
    CHECK(bad.discord_id.empty());
    CHECK(bad.avatar.empty());
}

TEST_CASE("a username containing a newline normalizes to empty") {
    CHECK(drpc::normalize_username("a\nb").empty());
}

TEST_CASE("username truncation does not split a UTF-8 sequence") {
    CHECK(drpc::normalize_username(std::string(31, 'a') + "\xC3\xA9") == std::string(31, 'a'));
}

namespace {
using drpc::TitleVisibility;
using Hidden = std::vector<std::pair<uint64_t, TitleVisibility>>;
}  // namespace

TEST_CASE("hidden titles default to none and parse private and hide entries") {
    CHECK(drpc::Settings{}.hidden_titles.empty());
    const auto s = drpc::parse_settings("hidden_titles = 0100abcd00010000:private, 0100A0D02127E000:hide\n");
    CHECK(s.hidden_titles == Hidden{{0x0100ABCD00010000ULL, TitleVisibility::Private},
                                    {0x0100A0D02127E000ULL, TitleVisibility::Hidden}});
}

TEST_CASE("hidden titles serialize upper case and round trip") {
    drpc::Settings s;
    s.hidden_titles = {{0x0100abcd00010000ULL, TitleVisibility::Private},
                       {0x0100A0D02127E000ULL, TitleVisibility::Hidden}};
    const std::string out = drpc::serialize_settings(s);
    CHECK(out.find("hidden_titles = 0100ABCD00010000:private,0100A0D02127E000:hide\n") != std::string::npos);
    CHECK(drpc::parse_settings(out) == s);
    CHECK(drpc::serialize_settings(drpc::Settings{}).find("hidden_titles = \n") != std::string::npos);
}

TEST_CASE("hidden titles: malformed, unknown and duplicate entries are dropped") {
    const auto s = drpc::parse_settings(
        "hidden_titles = 0100ABCD0001000:private,0100ABCD000100000:hide,0100ABCD0001000G:hide,"
        "0100ABCD00010000:shown,0100ABCD00010000:hidden,0100ABCD00010000,:private,0100000000000352:hide,"
        "0100ABCD00010000:PRIVATE,,0100A0D02127E000:hide,0100A0D02127E000:private, 0100000000010000 : private \n");
    CHECK(s.hidden_titles == Hidden{{0x0100A0D02127E000ULL, TitleVisibility::Hidden},
                                    {0x0100000000010000ULL, TitleVisibility::Private}});
}

TEST_CASE("hidden titles: 64 entries round trip, more are dropped") {
    drpc::Settings s;
    for (uint64_t i = 0; i < 64; ++i) {
        s.hidden_titles.emplace_back(0x0100000000010000ULL + (i << 16),
                                     i % 2 ? TitleVisibility::Hidden : TitleVisibility::Private);
    }
    const std::string out = drpc::serialize_settings(s);
    CHECK(drpc::parse_settings(out) == s);

    std::string ini = "hidden_titles = ";
    for (uint64_t i = 0; i < 70; ++i) ini += drpc::format_title_id(0x0100000000010000ULL + (i << 16)) + ":hide,";
    const auto many = drpc::parse_settings(ini);
    REQUIRE(many.hidden_titles.size() == 64);
    CHECK(many.hidden_titles.back().first == 0x0100000000010000ULL + (63ULL << 16));
}

TEST_CASE("visibility_of finds a title, Shown otherwise") {
    drpc::Settings s;
    s.hidden_titles = {{0x0100A0D02127E000ULL, TitleVisibility::Hidden}};
    CHECK(drpc::visibility_of(s, 0x0100A0D02127E000ULL) == TitleVisibility::Hidden);
    CHECK(drpc::visibility_of(s, 0x0100000000010000ULL) == TitleVisibility::Shown);
}

TEST_CASE("show_badge defaults to true, parses and serializes") {
    CHECK(drpc::parse_settings("enabled = 1\n").show_badge);
    const auto s = drpc::parse_settings("show_badge = false\n");
    CHECK_FALSE(s.show_badge);
    const std::string out = drpc::serialize_settings(s);
    CHECK(out.find("show_badge = false\n") != std::string::npos);
    CHECK(drpc::parse_settings(out) == s);
}
