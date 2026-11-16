#include <doctest/doctest.h>

#include "core/settings.hpp"

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
    CHECK(drpc::redact("a SECRET b SECRET", "SECRET") == "a [redacted] b [redacted]");
    CHECK(drpc::redact("abc", "") == "abc");
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
