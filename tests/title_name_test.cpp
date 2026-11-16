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
