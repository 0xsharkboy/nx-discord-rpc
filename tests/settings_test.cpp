#include <doctest/doctest.h>

#include <span>
#include <string>
#include <string_view>

#include "core/settings.hpp"

namespace {
const std::string kApiKey = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNO-_";
}

TEST_CASE("defaults on empty or garbage input") {
    CHECK(drpc::parse_settings("") == drpc::Settings{});
    using namespace std::string_view_literals;
    CHECK(drpc::parse_settings("\x00\xff garbage\n=\nenabled=maybe"sv) == drpc::Settings{});
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

TEST_CASE("a username containing a newline normalizes to empty") {
    CHECK(drpc::normalize_username("a\nb").empty());
}
