#include <doctest/doctest.h>

#include <cstring>

#include "drpc/client.hpp"

using namespace drpc;

namespace {
int64_t g_now = 0;
int64_t fake_now() { return g_now; }
}  // namespace

TEST_CASE("missing scenario reports NotRunning") {
    auto c = make_fake_client("missing");
    CHECK(c->connect() == ConnectError::NotRunning);
}

TEST_CASE("mismatch scenario reports the remote version") {
    auto c = make_fake_client("mismatch");
    CHECK(c->connect() == ConnectError::VersionMismatch);
    CHECK(c->remote_version() == 2);
}

TEST_CASE("unknown scenario yields no client") {
    CHECK(make_fake_client("bogus") == nullptr);
}

TEST_CASE("linked-game shows a game and a linked account") {
    auto c = make_fake_client("linked-game");
    REQUIRE(c->connect() == ConnectError::None);
    CHECK(c->remote_version() == ipc::kProtocolVersion);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(std::strcmp(st.name, "Monument Valley") == 0);
    CHECK(st.is_game == 1);
    CHECK(st.link == ipc::LinkState::Linked);
    ipc::Link l;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Linked);
    CHECK(std::strcmp(l.username, "test_rpc") == 0);
}

TEST_CASE("linked-home is not a game") {
    auto c = make_fake_client("linked-home");
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.is_game == 0);
    CHECK(st.link == ipc::LinkState::Linked);
}

TEST_CASE("backoff scenario reports the relay backoff") {
    auto c = make_fake_client("backoff");
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.health == ipc::RelayHealth::Backoff);
    CHECK(st.backoff_s > 0);
}

TEST_CASE("nokey settings round-trip") {
    auto c = make_fake_client("nokey");
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.link == ipc::LinkState::NoKey);
    ipc::Settings s;
    REQUIRE(c->settings(s));
    s.private_mode = 1;
    s.show_elapsed = 0;
    REQUIRE(c->set_settings(s));
    ipc::Settings back;
    REQUIRE(c->settings(back));
    CHECK(back.private_mode == 1);
    CHECK(back.show_elapsed == 0);
}

TEST_CASE("link-flow counts down then links") {
    set_fake_clock(fake_now);
    g_now = 1000;
    auto c = make_fake_client("link-flow");
    ipc::Link l;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Idle);
    REQUIRE(c->start_link());
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Pending);
    CHECK(std::strcmp(l.user_code, "ABCD-EFGH") == 0);
    CHECK(l.seconds_left == 300);
    g_now += 3000;
    REQUIRE(c->link(l));
    CHECK(l.seconds_left == 297);
    g_now += 5000;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Linked);
    CHECK(std::strcmp(l.username, "test_rpc") == 0);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.link == ipc::LinkState::Linked);
    set_fake_clock(nullptr);
}

TEST_CASE("cancel_link returns to idle and forget_key clears the key") {
    set_fake_clock(fake_now);
    g_now = 0;
    auto c = make_fake_client("link-flow");
    c->start_link();
    c->cancel_link();
    ipc::Link l;
    c->link(l);
    CHECK(l.phase == ipc::LinkPhase::Idle);

    auto g = make_fake_client("linked-game");
    g->forget_key();
    ipc::Status st;
    g->status(st);
    CHECK(st.link == ipc::LinkState::NoKey);
    set_fake_clock(nullptr);
}
