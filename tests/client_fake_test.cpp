#include <doctest/doctest.h>

#include <cstring>
#include <string>

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
    CHECK(c->remote_version() == ipc::kProtocolVersion + 1);
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
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.is_game == 0);
    CHECK(st.link == ipc::LinkState::Linked);
    CHECK(std::strcmp(st.discord_id, "80351110224678912") == 0);
    CHECK(st.avatar[0] == '\0');
}

TEST_CASE("backoff scenario reports the relay backoff") {
    auto c = make_fake_client("backoff");
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.health == ipc::RelayHealth::Backoff);
    CHECK(st.backoff_s > 0);
}

TEST_CASE("nokey settings round-trip") {
    auto c = make_fake_client("nokey");
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.link == ipc::LinkState::NoKey);
    ipc::Settings s;
    REQUIRE(c->settings(s));
    s.private_mode = 1;
    REQUIRE(c->set_settings(s));
    ipc::Settings back;
    REQUIRE(c->settings(back));
    CHECK(back.private_mode == 1);
}

TEST_CASE("link-flow counts down then links") {
    set_fake_clock(fake_now);
    g_now = 1000;
    auto c = make_fake_client("link-flow");
    REQUIRE(c->connect() == ConnectError::None);
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
    REQUIRE(c->connect() == ConnectError::None);
    c->start_link();
    c->cancel_link();
    ipc::Link l;
    c->link(l);
    CHECK(l.phase == ipc::LinkPhase::Idle);

    auto g = make_fake_client("linked-game");
    REQUIRE(g->connect() == ConnectError::None);
    g->forget_key();
    ipc::Status st;
    g->status(st);
    CHECK(st.link == ipc::LinkState::NoKey);
    set_fake_clock(nullptr);
}

TEST_CASE("calls fail before connect and after a failed connect") {
    for (const char* name : {"linked-game", "missing", "mismatch"}) {
        auto c = make_fake_client(name);
        ipc::Status st;
        ipc::Settings s{};
        ipc::Link l;
        CHECK_FALSE(c->status(st));
        CHECK_FALSE(c->settings(s));
        CHECK_FALSE(c->set_settings(s));
        CHECK_FALSE(c->link(l));
        CHECK_FALSE(c->start_link());
        CHECK_FALSE(c->cancel_link());
        CHECK_FALSE(c->unlink());
        CHECK_FALSE(c->forget_key());
        if (std::strcmp(name, "linked-game") != 0) {
            CHECK(c->connect() != ConnectError::None);
            CHECK_FALSE(c->status(st));
            CHECK_FALSE(c->start_link());
        }
    }
}

TEST_CASE("set_settings rejects non-boolean values") {
    auto c = make_fake_client("nokey");
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Settings s{};
    s.debug_log = 2;
    CHECK_FALSE(c->set_settings(s));
    ipc::Settings back;
    REQUIRE(c->settings(back));
    CHECK(back.debug_log == 0);
}

TEST_CASE("linked-home has no elapsed time") {
    auto c = make_fake_client("linked-home");
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.elapsed_s == -1);
}

TEST_CASE("link-ratelimited fails at start with a retry delay") {
    set_fake_clock(fake_now);
    g_now = 0;
    auto c = make_fake_client("link-ratelimited");
    REQUIRE(c->connect() == ConnectError::None);
    REQUIRE(c->start_link());
    ipc::Link l;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Starting);
    g_now += 2000;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Failed);
    CHECK(l.failure == ipc::LinkFailure::RateLimited);
    CHECK(l.retry_after_s == 1500);
    set_fake_clock(nullptr);
}

TEST_CASE("link-cancelled goes from Pending back to Idle") {
    set_fake_clock(fake_now);
    g_now = 0;
    auto c = make_fake_client("link-cancelled");
    REQUIRE(c->connect() == ConnectError::None);
    REQUIRE(c->start_link());
    ipc::Link l;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Pending);
    g_now += 9000;
    REQUIRE(c->link(l));
    CHECK(l.phase == ipc::LinkPhase::Idle);
    set_fake_clock(nullptr);
}

TEST_CASE("unlink-failed reports InProgress then Failed and keeps the key") {
    set_fake_clock(fake_now);
    g_now = 0;
    auto c = make_fake_client("unlink-failed");
    REQUIRE(c->connect() == ConnectError::None);
    REQUIRE(c->unlink());
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.unlink == ipc::UnlinkResult::InProgress);
    g_now += 2500;
    REQUIRE(c->status(st));
    CHECK(st.unlink == ipc::UnlinkResult::Failed);
    CHECK(st.link == ipc::LinkState::Linked);
    REQUIRE(c->forget_key());
    REQUIRE(c->status(st));
    CHECK(st.link == ipc::LinkState::NoKey);
    set_fake_clock(nullptr);
}

TEST_CASE("problem scenarios report their link state and health") {
    struct Case {
        const char* name;
        ipc::LinkState link;
        ipc::RelayHealth health;
    };
    const Case cases[] = {
        {"unauthorized", ipc::LinkState::Unauthorized, ipc::RelayHealth::Ok},
        {"broken", ipc::LinkState::LinkBroken, ipc::RelayHealth::Ok},
        {"offline", ipc::LinkState::Linked, ipc::RelayHealth::Offline},
        {"nourl", ipc::LinkState::NoKey, ipc::RelayHealth::NoUrl},
    };
    for (const auto& k : cases) {
        CAPTURE(k.name);
        auto c = make_fake_client(k.name);
        REQUIRE(c);
        REQUIRE(c->connect() == ConnectError::None);
        ipc::Status st;
        REQUIRE(c->status(st));
        CHECK(st.link == k.link);
        CHECK(st.health == k.health);
    }
}

TEST_CASE("long-names fills the string fields") {
    auto c = make_fake_client("long-names");
    REQUIRE(c->connect() == ConnectError::None);
    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(std::strlen(st.name) == 128);
    CHECK(std::strlen(st.details) == 128);
    REQUIRE(c->start_link());
    ipc::Link l;
    REQUIRE(c->link(l));
    CHECK(std::strlen(l.verification_uri) == 127);
}

TEST_CASE("mismatch: an older sysmodule is the side to update, a newer one means updating the UI") {
    CHECK(sysmodule_outdated(0));
    CHECK(sysmodule_outdated(ipc::kProtocolVersion - 1));
    CHECK_FALSE(sysmodule_outdated(ipc::kProtocolVersion + 1));
    CHECK_FALSE(sysmodule_outdated(ipc::kProtocolVersion));
}

TEST_CASE("mismatch-old scenario reports an older remote version") {
    auto c = make_fake_client("mismatch-old");
    REQUIRE(c);
    CHECK(c->connect() == ConnectError::VersionMismatch);
    CHECK(c->remote_version() == 0);
}

TEST_CASE("fake hidden list: sample entries, set_hidden validates and the status follows it") {
    auto c = make_fake_client("linked-game");
    ipc::HiddenList l;
    CHECK_FALSE(c->hidden(l));
    REQUIRE(c->connect() == ConnectError::None);
    REQUIRE(c->hidden(l));
    CHECK(l.count == 2);
    CHECK(ipc::valid_hidden(l));

    ipc::Status st;
    REQUIRE(c->status(st));
    CHECK(st.running_title_id == 0x0100000000010000ULL);
    CHECK(st.last_relay_status == 204);
    CHECK(st.shown == 1);

    l.count = 3;
    l.items[2].title_id = st.running_title_id;
    l.items[2].visibility = static_cast<uint8_t>(ipc::TitleVisibility::Hidden);
    REQUIRE(c->set_hidden(l));
    REQUIRE(c->status(st));
    CHECK(st.shown == 0);
    CHECK(st.title_id[0] == '\0');
    CHECK(st.running_title_id == 0x0100000000010000ULL);

    l.items[2].visibility = static_cast<uint8_t>(ipc::TitleVisibility::Private);
    REQUIRE(c->set_hidden(l));
    REQUIRE(c->status(st));
    CHECK(st.shown == 1);
    CHECK(std::strcmp(st.name, "Nintendo Switch") == 0);
    CHECK(st.title_id[0] == '\0');

    ipc::HiddenList bad = l;
    bad.items[2].visibility = 0;
    CHECK_FALSE(c->set_hidden(bad));
    ipc::HiddenList now;
    REQUIRE(c->hidden(now));
    CHECK(now.count == 3);
}

TEST_CASE("fake last relay status follows the scenario") {
    struct Case {
        const char* name;
        int32_t status;
    };
    for (const Case k : {Case{"nokey", 0}, Case{"unauthorized", 401}, Case{"broken", 409}, Case{"offline", -1},
                         Case{"backoff", 503}, Case{"linked-home", 204}}) {
        CAPTURE(k.name);
        auto c = make_fake_client(k.name);
        REQUIRE(c->connect() == ConnectError::None);
        ipc::Status st;
        REQUIRE(c->status(st));
        CHECK(st.last_relay_status == k.status);
    }
}

TEST_CASE("fake hidden-full: the list is full and valid") {
    auto c = make_fake_client("hidden-full");
    REQUIRE(c);
    REQUIRE(c->connect() == ConnectError::None);
    ipc::HiddenList l;
    REQUIRE(c->hidden(l));
    CHECK(l.count == ipc::kMaxHiddenTitles);
    CHECK(ipc::valid_hidden(l));
}

TEST_CASE("fake preview scenarios: paused, private and idle") {
    ipc::Status s;
    auto paused = make_fake_client("linked-paused");
    REQUIRE(paused);
    REQUIRE(paused->connect() == ConnectError::None);
    REQUIRE(paused->status(s));
    CHECK(s.shown == 1);
    CHECK(std::string(s.details) == "Paused \xC2\xB7 HOME Menu");
    CHECK(s.elapsed_s == -1);

    auto priv = make_fake_client("linked-private");
    REQUIRE(priv);
    REQUIRE(priv->connect() == ConnectError::None);
    REQUIRE(priv->status(s));
    CHECK(std::string(s.name) == "Nintendo Switch");
    CHECK(s.title_id[0] == '\0');
    CHECK(s.is_game == 0);
    CHECK(s.running_title_id == 0x0100000000010000ULL);
    ipc::HiddenList l;
    REQUIRE(priv->hidden(l));
    CHECK(ipc::valid_hidden(l));

    auto idle = make_fake_client("linked-idle");
    REQUIRE(idle);
    REQUIRE(idle->connect() == ConnectError::None);
    REQUIRE(idle->status(s));
    CHECK(s.shown == 0);
    CHECK(s.link == ipc::LinkState::Linked);
    CHECK(s.running_title_id == 0);
}
