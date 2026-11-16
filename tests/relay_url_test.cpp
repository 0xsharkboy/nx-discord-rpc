#include <doctest/doctest.h>

#include "core/relay_url.hpp"
#include "core/settings.hpp"

TEST_CASE("default relay url parses") {
    const auto u = drpc::parse_relay_url(drpc::kDefaultRelayUrl);
    REQUIRE(u);
    CHECK(u->host == "nx-discord-rpc.0xsharkboy.dev");
    CHECK(u->port == 443);
    CHECK(u->base_path.empty());
}

TEST_CASE("port and base path") {
    const auto u = drpc::parse_relay_url("HTTPS://h.example:8443/relay/");
    REQUIRE(u);
    CHECK(u->host == "h.example");
    CHECK(u->port == 8443);
    CHECK(u->base_path == "/relay");
}

TEST_CASE("only slash path is empty") {
    const auto u = drpc::parse_relay_url("https://h/");
    REQUIRE(u);
    CHECK(u->base_path.empty());
}

TEST_CASE("invalid relay urls are rejected") {
    for (const char* s : {"", "http://h", "https://", "https://h/a b", "https://u@h", "https://h?x",
                          "https://h#f", "https://h:0", "https://h:70000", "https://h:", "https://h:8a",
                          "https://:443", "https://h/a\tb", "ftp://h", "https://h\r\n"}) {
        CAPTURE(s);
        CHECK_FALSE(drpc::parse_relay_url(s));
    }
    CHECK_FALSE(drpc::parse_relay_url(std::string_view("https://h/a\r\nX: y")));
    CHECK_FALSE(drpc::parse_relay_url("https://" + std::string(254, 'a')));
}
