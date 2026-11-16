#include <doctest/doctest.h>

#include "core/json_writer.hpp"

TEST_CASE("writer builds nested objects with commas") {
    drpc::json::Writer w;
    w.begin_object().key("op").value(int64_t{3}).key("d").begin_object()
     .key("afk").value(false).key("since").null().key("a").begin_array()
     .value("x").value("y").end_array().end_object().end_object();
    CHECK(w.str() == R"({"op":3,"d":{"afk":false,"since":null,"a":["x","y"]}})");
}
TEST_CASE("writer escapes quotes, backslashes and control characters") {
    drpc::json::Writer w;
    w.value("a\"b\\c\n\x01");
    CHECK(w.str() == R"("a\"b\\c\n\u0001")");
}
TEST_CASE("writer passes UTF-8 through unchanged") {
    drpc::json::Writer w;
    w.value("Pokémon · ゼルダ");
    CHECK(w.str() == "\"Pokémon · ゼルダ\"");
}
TEST_CASE("const char* selects the string overload") {
    drpc::json::Writer w; w.value("true");
    CHECK(w.str() == "\"true\"");
}
