#include <doctest/doctest.h>

#include <array>
#include <string_view>

#include "core/title_name.hpp"

using namespace std::string_view_literals;

TEST_CASE("console language entry wins") {
    std::array entries{"Zelda"sv, "ゼルダ"sv};
    CHECK(drpc::choose_title_name("ゼルダの伝説"sv, entries) == "ゼルダの伝説");
}

TEST_CASE("empty console language entry falls back to the first non-empty entry") {
    std::array entries{""sv, ""sv, "Pokémon Violet"sv, "Pokémon Violett"sv};
    CHECK(drpc::choose_title_name(""sv, entries) == "Pokémon Violet");
}

TEST_CASE("all entries empty gives a non-empty placeholder") {
    std::array entries{""sv, ""sv};
    CHECK(drpc::choose_title_name(""sv, entries) == "Unknown game");
    CHECK(drpc::choose_title_name(""sv, {}) == "Unknown game");
    CHECK_FALSE(drpc::kUnknownGameName.empty());
}

TEST_CASE("english title name prefers American then British English") {
    std::array a{"Zelda US"sv, "Zelda UK"sv, "ゼルダ"sv};
    CHECK(drpc::english_title_name(a) == "Zelda US");
    std::array b{""sv, "Zelda UK"sv, "ゼルダ"sv};
    CHECK(drpc::english_title_name(b) == "Zelda UK");
    std::array c{""sv, ""sv, "ゼルダ"sv};
    CHECK(drpc::english_title_name(c).empty());
    CHECK(drpc::english_title_name({}).empty());
}

TEST_CASE("a sub-program maps to its parent application, an application to nothing") {
    // Sonic X Shadow Generations (01005EA01C0FC000) starts Sonic Generations as program index 1.
    CHECK(drpc::parent_application_id(0x01005EA01C0FC001ULL) == std::optional<uint64_t>{0x01005EA01C0FC000ULL});
    CHECK(drpc::parent_application_id(0x01005EA01C0FC0FFULL) == std::optional<uint64_t>{0x01005EA01C0FC000ULL});
    CHECK_FALSE(drpc::parent_application_id(0x01005EA01C0FC000ULL).has_value());
}
