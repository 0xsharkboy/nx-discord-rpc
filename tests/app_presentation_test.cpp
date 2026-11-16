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
