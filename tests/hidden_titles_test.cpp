#include <doctest/doctest.h>

#include <cstring>
#include <memory>

#include "drpc/hidden_titles.hpp"

using drpc::TitleVisibility;
namespace ipc = drpc::ipc;

namespace {

constexpr uint64_t kA = 0x0100000000010000ULL;
constexpr uint64_t kB = 0x01007EF00011E000ULL;
constexpr uint64_t kC = 0x0100152000022000ULL;

std::unique_ptr<ipc::HiddenList> empty_list() { return std::make_unique<ipc::HiddenList>(); }

std::unique_ptr<ipc::HiddenList> full_list() {
    auto list = empty_list();
    for (uint64_t i = 0; i < ipc::kMaxHiddenTitles; ++i)
        REQUIRE(drpc::set_visibility(*list, 0x0100000000100000ULL + (i << 16), TitleVisibility::Private) ==
                drpc::SetVisibility::Ok);
    REQUIRE(list->count == ipc::kMaxHiddenTitles);
    return list;
}

}  // namespace

TEST_CASE("next_visibility cycles Shown, Playing a game, Hidden") {
    CHECK(drpc::next_visibility(TitleVisibility::Shown) == TitleVisibility::Private);
    CHECK(drpc::next_visibility(TitleVisibility::Private) == TitleVisibility::Hidden);
    CHECK(drpc::next_visibility(TitleVisibility::Hidden) == TitleVisibility::Shown);
}

TEST_CASE("visibility_of: unlisted titles are Shown") {
    auto list = empty_list();
    CHECK(drpc::visibility_of(*list, kA) == TitleVisibility::Shown);
    list->count = 2;
    list->items[0] = {kB, static_cast<uint8_t>(TitleVisibility::Private), {}};
    list->items[1] = {kC, static_cast<uint8_t>(TitleVisibility::Hidden), {}};
    CHECK(drpc::visibility_of(*list, kA) == TitleVisibility::Shown);
    CHECK(drpc::visibility_of(*list, kB) == TitleVisibility::Private);
    CHECK(drpc::visibility_of(*list, kC) == TitleVisibility::Hidden);
}

TEST_CASE("visibility_of ignores items past count and unknown values") {
    auto list = empty_list();
    list->items[0] = {kA, static_cast<uint8_t>(TitleVisibility::Hidden), {}};
    CHECK(drpc::visibility_of(*list, kA) == TitleVisibility::Shown);
    list->count = 1;
    list->items[0].visibility = 7;
    CHECK(drpc::visibility_of(*list, kA) == TitleVisibility::Shown);
}

TEST_CASE("set_visibility appends, updates in place and removes on Shown") {
    auto list = empty_list();
    CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Private) == drpc::SetVisibility::Ok);
    CHECK(drpc::set_visibility(*list, kB, TitleVisibility::Hidden) == drpc::SetVisibility::Ok);
    CHECK(drpc::set_visibility(*list, kC, TitleVisibility::Private) == drpc::SetVisibility::Ok);
    REQUIRE(list->count == 3);
    CHECK(ipc::valid_hidden(*list));

    CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Hidden) == drpc::SetVisibility::Ok);
    CHECK(list->count == 3);
    CHECK(list->items[0].title_id == kA);
    CHECK(drpc::visibility_of(*list, kA) == TitleVisibility::Hidden);

    CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Shown) == drpc::SetVisibility::Ok);
    REQUIRE(list->count == 2);
    CHECK(list->items[0].title_id == kB);  // order kept
    CHECK(list->items[1].title_id == kC);
    CHECK(list->items[2].title_id == 0);  // the freed slot is zero
    CHECK(list->items[2].visibility == 0);
    CHECK(ipc::valid_hidden(*list));
}

TEST_CASE("set_visibility: Shown for an unlisted title changes nothing") {
    auto list = empty_list();
    CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Shown) == drpc::SetVisibility::Ok);
    CHECK(list->count == 0);
}

TEST_CASE("set_visibility on a full list") {
    auto list = full_list();
    const uint64_t listed = list->items[10].title_id;

    SUBCASE("a new title is refused and the list is unchanged") {
        const ipc::HiddenList before = *list;
        CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Hidden) == drpc::SetVisibility::Full);
        CHECK(std::memcmp(&before, list.get(), sizeof before) == 0);
    }
    SUBCASE("a listed title still switches between Playing a game and Hidden") {
        CHECK(drpc::set_visibility(*list, listed, TitleVisibility::Hidden) == drpc::SetVisibility::Ok);
        CHECK(drpc::visibility_of(*list, listed) == TitleVisibility::Hidden);
        CHECK(list->count == ipc::kMaxHiddenTitles);
    }
    SUBCASE("setting one back to Shown frees a slot") {
        CHECK(drpc::set_visibility(*list, listed, TitleVisibility::Shown) == drpc::SetVisibility::Ok);
        CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Hidden) == drpc::SetVisibility::Ok);
        CHECK(list->count == ipc::kMaxHiddenTitles);
        CHECK(ipc::valid_hidden(*list));
    }
    SUBCASE("Shown for an unlisted title is fine") {
        CHECK(drpc::set_visibility(*list, kA, TitleVisibility::Shown) == drpc::SetVisibility::Ok);
    }
}
