#include <doctest/doctest.h>

#include <cstring>

#include "presentation.hpp"

using namespace drpc;

namespace {

ipc::Status status(ipc::LinkState link, ipc::RelayHealth health, const char* username = "test_rpc") {
    ipc::Status st;
    std::memset(&st, 0, sizeof st);
    st.link = link;
    st.health = health;
    std::strncpy(st.username, username, sizeof st.username - 1);
    return st;
}

}  // namespace

TEST_CASE("footer_line follows the precedence") {
    using app::Footer;
    using LS = ipc::LinkState;
    using RH = ipc::RelayHealth;
    CHECK(app::footer_line(status(LS::NoKey, RH::NoUrl)).kind == Footer::NoUrl);
    CHECK(app::footer_line(status(LS::LinkBroken, RH::NoUrl)).kind == Footer::NoUrl);
    CHECK(app::footer_line(status(LS::NoKey, RH::Offline)).kind == Footer::Offline);
    CHECK(app::footer_line(status(LS::NoKey, RH::Backoff)).kind == Footer::NoKey);
    CHECK(app::footer_line(status(LS::LinkBroken, RH::Backoff)).kind == Footer::Broken);
    CHECK(app::footer_line(status(LS::Unauthorized, RH::Ok)).kind == Footer::Unauthorized);
    CHECK(app::footer_line(status(LS::Linked, RH::Backoff)).kind == Footer::Backoff);
    CHECK(app::footer_line(status(LS::Linked, RH::Ok)).kind == Footer::Ok);
    CHECK(app::footer_line(status(LS::Linked, RH::Ok, "")).kind == Footer::OkUnnamed);
    CHECK(app::footer_line(status(LS::Linked, RH::Ok)).tone == app::Tone::Good);
    CHECK(app::footer_line(status(LS::NoKey, RH::Ok)).tone == app::Tone::Neutral);
    CHECK(app::footer_line(status(LS::Unauthorized, RH::Ok)).tone == app::Tone::Bad);
    CHECK(app::footer_line(status(LS::Linked, RH::Backoff)).tone == app::Tone::Warning);
}

TEST_CASE("footer_line says Update required when the relay refuses this version") {
    using app::Footer;
    using LS = ipc::LinkState;
    using RH = ipc::RelayHealth;
    ipc::Status st = status(LS::Outdated, RH::Ok);
    std::strncpy(st.min_version, "0.7.0", sizeof st.min_version - 1);
    CHECK(app::footer_line(st).kind == Footer::Outdated);
    CHECK(app::footer_line(st).tone == app::Tone::Bad);
    CHECK(app::update_required(st));
    // The relay named no version: the line says Update required without one.
    st.min_version[0] = '\0';
    CHECK(app::footer_line(st).kind == Footer::OutdatedUnnamed);
    CHECK(app::footer_line(st).tone == app::Tone::Bad);
    // A refused version is no backoff, but an invalid relay_url or no network still comes first.
    CHECK(app::footer_line(status(LS::Outdated, RH::Backoff)).kind == Footer::OutdatedUnnamed);
    CHECK(app::footer_line(status(LS::Outdated, RH::NoUrl)).kind == Footer::NoUrl);
    CHECK(app::footer_line(status(LS::Outdated, RH::Offline)).kind == Footer::Offline);
}

TEST_CASE("update_required keys on the link state, never on min_version") {
    // A link attempt refused for the version leaves min_version set while the state is NoKey or Linked.
    for (auto link : {ipc::LinkState::NoKey, ipc::LinkState::Linked, ipc::LinkState::Unauthorized,
                      ipc::LinkState::LinkBroken}) {
        ipc::Status st = status(link, ipc::RelayHealth::Ok);
        std::strncpy(st.min_version, "0.7.0", sizeof st.min_version - 1);
        CHECK_FALSE(app::update_required(st));
        CHECK(app::footer_line(st).kind != app::Footer::Outdated);
    }
    CHECK(app::update_required(status(ipc::LinkState::Outdated, ipc::RelayHealth::Ok)));
}

TEST_CASE("format_elapsed switches to hours at one hour") {
    CHECK(app::format_elapsed(0) == "00:00");
    CHECK(app::format_elapsed(754) == "12:34");
    CHECK(app::format_elapsed(3599) == "59:59");
    CHECK(app::format_elapsed(3600) == "1:00:00");
    CHECK(app::format_elapsed(36000 + 62) == "10:01:02");
    CHECK(app::format_elapsed(-5) == "00:00");
}

TEST_CASE("link_countdown is m:ss") {
    CHECK(app::link_countdown(0) == "0:00");
    CHECK(app::link_countdown(9) == "0:09");
    CHECK(app::link_countdown(298) == "4:58");
    CHECK(app::link_countdown(900) == "15:00");
}

TEST_CASE("retry_minutes rounds up with a minimum of one") {
    CHECK(app::retry_minutes(0) == 1);
    CHECK(app::retry_minutes(1) == 1);
    CHECK(app::retry_minutes(60) == 1);
    CHECK(app::retry_minutes(61) == 2);
    CHECK(app::retry_minutes(1500) == 25);
}

namespace {

ipc::Status playing(const char* title_id = "0100000000010000") {
    ipc::Status st = status(ipc::LinkState::Linked, ipc::RelayHealth::Ok);
    std::strncpy(st.name, "Monument Valley", sizeof st.name - 1);
    std::strncpy(st.details, "on Nintendo Switch", sizeof st.details - 1);
    std::strncpy(st.title_id, title_id, sizeof st.title_id - 1);
    st.is_game = title_id[0] != '\0';
    st.shown = 1;
    st.elapsed_s = 754;
    st.running_title_id = 0x0100000000010000ULL;
    return st;
}

}  // namespace

TEST_CASE("preview_of shows the game with its icon and timer") {
    const app::Preview p = app::preview_of(playing());
    CHECK(p.shown);
    CHECK(p.icon_title == "0100000000010000");
    CHECK(p.timer);
}

TEST_CASE("preview_of hides the timer when the sysmodule sends none, as while paused") {
    ipc::Status st = playing();
    st.elapsed_s = -1;
    const app::Preview p = app::preview_of(st);
    CHECK(p.shown);
    CHECK_FALSE(p.timer);
}

TEST_CASE("preview_of never uses the running title of a masked game") {
    // A Private title: Status carries no title id, the running id is still known locally.
    ipc::Status st = playing("");
    std::strncpy(st.name, "Nintendo Switch", sizeof st.name - 1);
    const app::Preview p = app::preview_of(st);
    CHECK(p.shown);
    CHECK(p.icon_title.empty());
}

TEST_CASE("preview_of shows nothing when nothing reaches Discord") {
    ipc::Status hidden = playing("");
    hidden.shown = 0;
    hidden.name[0] = '\0';
    hidden.elapsed_s = -1;
    CHECK_FALSE(app::preview_of(hidden).shown);
    CHECK_FALSE(app::preview_of(hidden).timer);

    ipc::Status nameless = playing();
    nameless.name[0] = '\0';
    CHECK_FALSE(app::preview_of(nameless).shown);

    for (auto link : {ipc::LinkState::NoKey, ipc::LinkState::Unauthorized, ipc::LinkState::LinkBroken,
                      ipc::LinkState::Outdated}) {
        ipc::Status st = playing();
        st.link = link;
        const app::Preview p = app::preview_of(st);
        CHECK_FALSE(p.shown);
        CHECK(p.icon_title.empty());
        CHECK_FALSE(p.timer);
    }
}
