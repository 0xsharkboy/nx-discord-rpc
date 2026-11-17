#include <doctest/doctest.h>

#include <vector>

#include "game_list.hpp"

using app::InstalledGame;

namespace {

std::vector<uint64_t> ids(const std::vector<InstalledGame>& games) {
    std::vector<uint64_t> out;
    for (const InstalledGame& g : games) out.push_back(g.title_id);
    return out;
}

}  // namespace

TEST_CASE("sort_games: by name ignoring case, then title id, nameless last") {
    std::vector<InstalledGame> games = {
        {5, "", {}}, {4, "beta", {}}, {3, "Alpha", {}}, {2, "alpha", {}}, {1, "", {}}, {6, "Alphabet", {}},
        {7, "Beta", {}},
    };
    app::sort_games(games);
    CHECK(ids(games) == std::vector<uint64_t>{2, 3, 6, 4, 7, 1, 5});
}

TEST_CASE("sort_games keeps non-ASCII names after ASCII letters") {
    std::vector<InstalledGame> games = {{1, "\xC3\x89toile", {}}, {2, "Zelda", {}}, {3, "apple", {}}};
    app::sort_games(games);
    CHECK(ids(games) == std::vector<uint64_t>{3, 2, 1});
}

TEST_CASE("downscale_rgba averages each covered block") {
    // 4x2 source: left half black, right half white, scaled to 2x1.
    std::vector<uint8_t> src;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t v = x < 2 ? 0 : 255;
            src.insert(src.end(), {v, v, v, 255});
        }
    }
    const app::RgbaIcon out = app::downscale_rgba(src.data(), 4, 2, src.size(), 2, 1);
    REQUIRE(out.width == 2);
    REQUIRE(out.height == 1);
    REQUIRE(out.pixels.size() == 8);
    CHECK(out.pixels[0] == 0);
    CHECK(out.pixels[3] == 255);
    CHECK(out.pixels[4] == 255);
}

TEST_CASE("downscale_rgba mixes a two-color block to the middle") {
    const std::vector<uint8_t> src = {0, 0, 0, 255, 200, 100, 50, 255};  // 2x1
    const app::RgbaIcon out = app::downscale_rgba(src.data(), 2, 1, src.size(), 1, 1);
    REQUIRE(out.pixels.size() == 4);
    CHECK(out.pixels[0] == 100);
    CHECK(out.pixels[1] == 50);
    CHECK(out.pixels[2] == 25);
    CHECK(out.pixels[3] == 255);
}

TEST_CASE("downscale_rgba also scales up and rejects bad input") {
    const std::vector<uint8_t> src = {10, 20, 30, 40};  // 1x1
    const app::RgbaIcon up = app::downscale_rgba(src.data(), 1, 1, src.size(), 2, 2);
    REQUIRE(up.pixels.size() == 16);
    CHECK(up.pixels[12] == 10);
    CHECK(app::downscale_rgba(src.data(), 2, 2, src.size(), 1, 1).pixels.empty());  // too short
    CHECK(app::downscale_rgba(nullptr, 1, 1, 4, 1, 1).pixels.empty());
    CHECK(app::downscale_rgba(src.data(), 1, 1, src.size(), 0, 1).pixels.empty());
}

TEST_CASE("title_id_hex writes 16 upper case digits") {
    CHECK(app::title_id_hex(0x01007ef00011e000ULL) == "01007EF00011E000");
    CHECK(app::title_id_hex(0x10) == "0000000000000010");
}
