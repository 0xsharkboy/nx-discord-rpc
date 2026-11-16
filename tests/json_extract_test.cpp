#include <doctest/doctest.h>

#include <string>
#include <string_view>

#include "core/json_extract.hpp"

using drpc::json::Extractor;

TEST_CASE("extracts scalars by path") {
    Extractor e({"op", "s", "t", "d.heartbeat_interval"});
    e.feed(R"({"t":null,"s":42,"op":10,"d":{"heartbeat_interval":41250,"_trace":["x"]}})");
    REQUIRE(e.finish());
    CHECK(e.find("op")->value == "10");
    CHECK(e.find("s")->value == "42");
    CHECK(e.find("t")->value == "null");
    CHECK(e.find("d.heartbeat_interval")->value == "41250");
}

TEST_CASE("array wildcard yields indexed paths") {
    Extractor e({"d[].session_id", "d[].status"});
    e.feed(R"({"d":[{"session_id":"all","status":"dnd"},{"session_id":"abc","status":"online"}]})");
    REQUIRE(e.finish());
    CHECK(e.find("d[0].status")->value == "dnd");
    CHECK(e.find("d[1].session_id")->value == "abc");
}

TEST_CASE("chunk boundaries anywhere give identical results") {
    std::string doc = R"({"d":{"user_settings":{"status":"invïsible \"x\" é😀"},"n":-1.5e3}})";
    for (size_t split = 0; split <= doc.size(); ++split) {
        Extractor e({"d.user_settings.status", "d.n"});
        e.feed(std::string_view(doc).substr(0, split));
        e.feed(std::string_view(doc).substr(split));
        REQUIRE(e.finish());
        CHECK(e.find("d.user_settings.status")->value == "invïsible \"x\" é\xF0\x9F\x98\x80");
        CHECK(e.find("d.n")->value == "-1.5e3");
    }
}

TEST_CASE("multi-megabyte document is consumed without capturing unrelated data") {
    Extractor e({"d.session_id"});
    e.feed(R"({"d":{"guilds":[)");
    std::string guild = R"({"id":"1","name":")" + std::string(4096, 'g') + R"(","channels":[1,2,3]},)";
    for (int i = 0; i < 1500; ++i) e.feed(guild);
    e.feed(R"({}],"session_id":"sess"}})");
    REQUIRE(e.finish());
    CHECK(e.captures().size() == 1);
    CHECK(e.find("d.session_id")->value == "sess");
}

TEST_CASE("oversized values are dropped, truncated input fails") {
    Extractor e({"a"}, 8);
    e.feed(R"({"a":"0123456789")");
    CHECK_FALSE(e.finish());
    CHECK(e.find("a") == nullptr);
}

TEST_CASE("oversized value is dropped but a well-formed document still finishes") {
    Extractor e({"a", "b"}, 4);
    e.feed(R"({"a":"toolong","b":"ok"})");
    REQUIRE(e.finish());
    CHECK(e.find("a") == nullptr);
    CHECK(e.find("b")->value == "ok");
}

TEST_CASE("top-level array is addressed as [] and [n]") {
    Extractor e({"[]", "[1].x"});
    e.feed(R"([true, {"x": false}, "s"])");
    REQUIRE(e.finish());
    CHECK(e.find("[0]")->value == "true");
    CHECK_FALSE(e.find("[0]")->is_string);
    CHECK(e.find("[1].x")->value == "false");
    CHECK(e.find("[2]")->is_string);
}

TEST_CASE("lone surrogates become U+FFFD") {
    Extractor e({"a", "b", "c"});
    e.feed(R"({"a":"\ud800x","b":"\udc00","c":"\ud800A"})");
    REQUIRE(e.finish());
    CHECK(e.find("a")->value == "\xEF\xBF\xBDx");
    CHECK(e.find("b")->value == "\xEF\xBF\xBD");
    CHECK(e.find("c")->value == "\xEF\xBF\xBD" "A");
}

TEST_CASE("malformed documents fail") {
    for (const char* bad : {R"({"a":})", R"({"a" 1})", R"({"a":1,})", R"([1,])", R"({"a":1}})",
                            R"({"a":tru})", R"({"a":"x\q"})", R"({"a":1} x)", R"({"a":"\u12G4"})", "", R"({"a":1)"}) {
        Extractor e({"a"});
        e.feed(bad);
        CHECK_FALSE(e.finish());
    }
}

TEST_CASE("reset makes the extractor reusable") {
    Extractor e({"a"});
    e.feed(R"({"a":1})");
    REQUIRE(e.finish());
    e.reset();
    CHECK(e.captures().empty());
    e.feed(R"({"a":2})");
    REQUIRE(e.finish());
    CHECK(e.captures().size() == 1);
    CHECK(e.find("a")->value == "2");
}

TEST_CASE("top-level scalar and trailing whitespace") {
    Extractor e({""});
    e.feed("  12 \n");
    REQUIRE(e.finish());
    CHECK(e.find("")->value == "12");
}

TEST_CASE("captures beyond the cap are dropped and the document stays valid") {
    Extractor e({"op", "d[].status"});
    std::string doc = R"({"op":0,"d":[)";
    for (int i = 0; i < 200; ++i) doc += std::string(i ? "," : "") + R"({"status":"online"})";
    doc += "]}";
    e.feed(doc);
    REQUIRE(e.finish());
    CHECK(e.captures().size() == Extractor::kMaxCaptures);
    CHECK(e.find("op")->value == "0");
    CHECK(e.find("d[0].status")->value == "online");
    CHECK(e.find("d[62].status"));
    CHECK_FALSE(e.find("d[63].status"));
}
