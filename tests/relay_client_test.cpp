#include <doctest/doctest.h>

#include "core/relay_client.hpp"

#include <optional>
#include <string>

using namespace drpc;
using Kind = Request::Kind;

namespace {

Report game(const char* name = "Zelda", const char* title_id = "0100A0D02127E000") {
    Report r;
    r.name = name;
    r.details = "Playing";
    r.title_id = std::string{title_id};
    r.show_elapsed = true;
    return r;
}

Report home() {
    Report r;
    r.name = "Nintendo Switch";
    r.details = "Home Menu";
    return r;
}

RelayClient keyed(bool admin = false) {
    RelayClient c;
    c.set_key(true, admin);
    return c;
}

Outcome status(int s, std::optional<int> retry_after_s = std::nullopt) {
    return Outcome{s, retry_after_s};
}

// Sends whatever is due at `now` and answers it with `out`; returns the request.
Request answer(RelayClient& c, int64_t now, const Outcome& out) {
    auto req = c.next(now);
    REQUIRE(req);
    c.on_outcome(*req, out, now);
    return *req;
}

// The first instant in [from, to] at which next() returns something.
std::optional<int64_t> first_due(RelayClient& c, int64_t from, int64_t to, int64_t step = 1000) {
    for (int64_t t = from; t <= to; t += step) {
        if (c.next(t)) return t;
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("first report: PUT at once, heartbeat at 60 s, changes sent at once") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->kind == Kind::PutPresence);
    CHECK(req->body == report_json(home(), std::nullopt));
    c.on_outcome(*req, status(204), 0);

    CHECK_FALSE(c.next(1));
    CHECK_FALSE(c.next(59999));
    auto hb = c.next(60000);
    REQUIRE(hb);
    CHECK(hb->kind == Kind::PutPresence);
    c.on_outcome(*hb, status(204), 60000);
    CHECK_FALSE(c.next(60001));

    Report changed = home();
    changed.details = "Settings";
    c.set_report(changed, std::nullopt);
    auto put = c.next(60002);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
    CHECK(put->body == report_json(changed, std::nullopt));
    c.on_outcome(*put, status(204), 60002);

    c.set_report(changed, 42);
    CHECK_FALSE(c.next(60003));
}

TEST_CASE("the PUT body carries the elapsed value of the latest set_report") {
    auto c = keyed();
    c.set_report(home(), 10);
    c.set_report(home(), 11);
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->body == report_json(home(), 11));
    c.on_outcome(*req, status(204), 0);
    c.set_report(home(), 71);
    auto hb = c.next(60000);
    REQUIRE(hb);
    CHECK(hb->body == report_json(home(), 71));
}

TEST_CASE("report to nullopt: one DELETE, then nothing") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);
    auto del = c.next(1000);
    REQUIRE(del);
    CHECK(del->kind == Kind::DeletePresence);
    c.on_outcome(*del, status(204), 1000);
    CHECK_FALSE(first_due(c, 1001, 400000));
}

TEST_CASE("after a DELETE, a desired report is sent at once") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);
    answer(c, 1000, status(204));
    c.set_report(home(), std::nullopt);
    auto put = c.next(2000);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("no DELETE when nothing was ever shown") {
    auto c = keyed();
    c.set_report(std::nullopt, std::nullopt);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("a 4xx on DELETE ends it") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);
    answer(c, 1000, status(400));
    CHECK_FALSE(first_due(c, 1001, 400000));
}

TEST_CASE("DELETE network errors: retries with backoff until 150 s after the last PUT") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);

    int64_t t = 1000;
    CHECK(answer(c, t, status(0)).kind == Kind::DeletePresence);
    int64_t expected[] = {6000, 16000, 36000, 76000};
    for (int64_t at : expected) {
        CHECK_FALSE(c.next(at - 1));
        auto del = c.next(at);
        REQUIRE(del);
        CHECK(del->kind == Kind::DeletePresence);
        c.on_outcome(*del, status(0), at);
        t = at;
    }
    // The next window would end at 156000, past 150 s after the PUT at 0.
    CHECK_FALSE(first_due(c, t + 1, 1000000));
}

TEST_CASE("DELETE stops once 150 s have passed since the last 204 PUT") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);
    answer(c, 140000, status(503));
    CHECK(answer(c, 145000, status(503)).kind == Kind::DeletePresence);
    // The 10 s window ends at 155000, past the cutoff.
    CHECK_FALSE(first_due(c, 145001, 1000000));
}

TEST_CASE("backoff: 503 three times gives windows of 5, 10 and 20 s") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(503));
    CHECK_FALSE(c.next(4999));
    answer(c, 5000, status(503));
    CHECK_FALSE(c.next(14999));
    answer(c, 15000, status(503));
    CHECK_FALSE(c.next(34999));
    CHECK(c.next(35000));
}

TEST_CASE("backoff caps at 300 s") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    int64_t t = 0;
    for (int i = 0; i < 10; ++i) {
        auto at = first_due(c, t, t + 400000, 1);
        REQUIRE(at);
        t = *at;
        answer(c, t, status(0));
    }
    CHECK_FALSE(c.next(t + 299999));
    CHECK(c.next(t + 300000));
}

TEST_CASE("backoff: 429 with Retry-After 360 gives a 360 s window") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(429, 360));
    CHECK_FALSE(c.next(359999));
    CHECK(c.next(360000));
}

TEST_CASE("backoff: a success resets it to 5 s") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(503));
    answer(c, 5000, status(503));
    answer(c, 15000, status(204));
    c.set_report(game(), std::nullopt);
    answer(c, 15001, status(503));
    CHECK_FALSE(c.next(20000));
    CHECK(c.next(20001));
}

TEST_CASE("401 on PUT: Unauthorized until a new key") {
    auto c = keyed();
    CHECK(c.link_state() == LinkState::Ok);
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(401));
    CHECK(c.link_state() == LinkState::Unauthorized);
    CHECK_FALSE(first_due(c, 0, 200000));
    c.set_report(game(), std::nullopt);
    CHECK_FALSE(first_due(c, 200000, 400000));

    c.set_key(true, false);
    CHECK(c.link_state() == LinkState::Ok);
    auto put = c.next(400001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
    CHECK(put->body == report_json(game(), std::nullopt));
}

TEST_CASE("a new key resends the same report at once and clears the backoff") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    answer(c, 60000, status(503));
    answer(c, 65000, status(401));
    c.set_key(true, false);
    auto put = c.next(65001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("401 on DELETE: Unauthorized") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(std::nullopt, std::nullopt);
    answer(c, 1000, status(401));
    CHECK(c.link_state() == LinkState::Unauthorized);
    CHECK_FALSE(first_due(c, 1000, 400000));
}

TEST_CASE("409 on PUT: LinkBroken until a new key") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(409));
    CHECK(c.link_state() == LinkState::LinkBroken);
    CHECK_FALSE(first_due(c, 0, 200000));
    c.set_report(game(), std::nullopt);
    CHECK_FALSE(first_due(c, 200000, 400000));

    c.set_key(true, false);
    CHECK(c.link_state() == LinkState::Ok);
    auto put = c.next(400001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("set_key without an API key: NoKey") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    c.set_key(false, true);
    CHECK(c.link_state() == LinkState::NoKey);
    CHECK_FALSE(first_due(c, 0, 400000));
}

TEST_CASE("400 on PUT: the same report is not resent, the heartbeat goes on") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(400));
    CHECK_FALSE(c.next(1000));
    CHECK_FALSE(c.next(59999));
    CHECK(c.next(60000));

    c.set_report(game(), std::nullopt);
    auto put = c.next(60001);
    REQUIRE(put);
    CHECK(put->body == report_json(game(), std::nullopt));
}

TEST_CASE("icons: one PutIcon after a 204 PUT for a title, never again after 202") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    auto icon = c.next(1);
    REQUIRE(icon);
    CHECK(icon->kind == Kind::PutIcon);
    CHECK(icon->title_id == "0100A0D02127E000");
    CHECK_FALSE(icon->use_admin_key);
    c.on_outcome(*icon, status(202), 1);
    CHECK_FALSE(c.next(2));

    answer(c, 60000, status(204));
    CHECK_FALSE(c.next(60001));
    c.set_report(home(), std::nullopt);
    answer(c, 60002, status(204));
    c.set_report(game(), std::nullopt);
    answer(c, 60003, status(204));
    CHECK_FALSE(c.next(60004));
}

TEST_CASE("icons: none for a report without a title id") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    CHECK_FALSE(c.next(1));
}

TEST_CASE("icons: none on a 4xx PUT") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(400));
    CHECK_FALSE(c.next(1));
}

TEST_CASE("icons: done statuses") {
    for (int s : {200, 202, 400, 404, 409, 413, -1}) {
        CAPTURE(s);
        auto c = keyed();
        c.set_report(game(), std::nullopt);
        answer(c, 0, status(204));
        CHECK(answer(c, 1, status(s)).kind == Kind::PutIcon);
        CHECK(c.link_state() == LinkState::Ok);
        CHECK_FALSE(first_due(c, 2, 59999));
    }
}

TEST_CASE("icons: a presence PUT goes first when both are due") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(game("Mario", "0100000000010000"), std::nullopt);
    auto req = c.next(1);
    REQUIRE(req);
    CHECK(req->kind == Kind::PutPresence);
    c.on_outcome(*req, status(204), 1);
    auto first = c.next(2);
    REQUIRE(first);
    CHECK(first->kind == Kind::PutIcon);
    CHECK(first->title_id == "0100A0D02127E000");
    c.on_outcome(*first, status(202), 2);
    auto second = c.next(3);
    REQUIRE(second);
    CHECK(second->title_id == "0100000000010000");
}

TEST_CASE("icons: 503 three times, no fourth attempt") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    int attempts = 0;
    for (int64_t t = 1; t < 59000; t += 500) {
        auto req = c.next(t);
        if (!req) continue;
        REQUIRE(req->kind == Kind::PutIcon);
        ++attempts;
        c.on_outcome(*req, status(503), t);
    }
    CHECK(attempts == 3);
    answer(c, 60000, status(204));
    CHECK_FALSE(first_due(c, 60001, 119999));
}

TEST_CASE("icons: a failure opens an icon backoff of 5 s, then 10 s") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    answer(c, 1, status(0));
    CHECK_FALSE(c.next(5000));
    CHECK(answer(c, 5001, status(0)).kind == Kind::PutIcon);
    CHECK_FALSE(c.next(15000));
    CHECK(c.next(15001));
}

TEST_CASE("icons: a 429 with Retry-After 120 does not delay the presence heartbeat") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    CHECK(answer(c, 1, status(429, 120)).kind == Kind::PutIcon);
    auto hb = c.next(60000);
    REQUIRE(hb);
    CHECK(hb->kind == Kind::PutPresence);
    c.on_outcome(*hb, status(204), 60000);
    CHECK_FALSE(first_due(c, 60001, 119999, 1));
    CHECK(answer(c, 120001, status(204)).kind == Kind::PutPresence);
    auto icon = c.next(120002);
    REQUIRE(icon);
    CHECK(icon->kind == Kind::PutIcon);
}

TEST_CASE("icons: none inside a presence backoff window") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(game("Zelda", "0100000000010000"), std::nullopt);
    CHECK(answer(c, 1, status(503)).kind == Kind::PutPresence);
    CHECK_FALSE(first_due(c, 2, 5000, 1));
    auto put = c.next(5001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("icons: a 401 with the admin key drops it and retries the title with the API key") {
    auto c = keyed(true);
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    auto icon = answer(c, 1, status(401));
    CHECK(icon.kind == Kind::PutIcon);
    CHECK(icon.use_admin_key);
    CHECK(c.link_state() == LinkState::Ok);
    auto retry = c.next(2);
    REQUIRE(retry);
    CHECK(retry->kind == Kind::PutIcon);
    CHECK(retry->title_id == "0100A0D02127E000");
    CHECK_FALSE(retry->use_admin_key);
    c.on_outcome(*retry, status(202), 2);

    c.set_key(true, true);
    c.set_report(game("Mario", "0100000000010000"), std::nullopt);
    answer(c, 3, status(204));
    auto next_icon = c.next(4);
    REQUIRE(next_icon);
    CHECK(next_icon->kind == Kind::PutIcon);
    CHECK_FALSE(next_icon->use_admin_key);
}

TEST_CASE("icons: the admin key is used while it works") {
    auto c = keyed(true);
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    auto icon = c.next(1);
    REQUIRE(icon);
    CHECK(icon->use_admin_key);
}

TEST_CASE("icons: a 401 with the API key is Unauthorized") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    CHECK(answer(c, 1, status(401)).kind == Kind::PutIcon);
    CHECK(c.link_state() == LinkState::Unauthorized);
    CHECK_FALSE(first_due(c, 2, 400000));
}

TEST_CASE("icons: a 401 on presence stops them") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(204));
    c.set_report(home(), std::nullopt);
    answer(c, 1, status(401));
    CHECK_FALSE(first_due(c, 2, 400000));
}

TEST_CASE("wake: a PUT at once, even 10 s after the last one") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    CHECK_FALSE(c.next(10000));
    c.on_wake();
    auto put = c.next(10000);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
    c.on_outcome(*put, status(204), 10000);
    CHECK_FALSE(c.next(10001));
    CHECK(c.next(70000));
}

TEST_CASE("wake: a failed PUT is retried after the backoff") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(204));
    c.on_wake();
    answer(c, 10000, status(0));
    CHECK_FALSE(c.next(14999));
    CHECK(c.next(15000));
}

TEST_CASE("wake: clears an open backoff window") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(503));
    CHECK_FALSE(c.next(1000));
    c.on_wake();
    auto put = c.next(1000);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("backoff: Retry-After is capped at 3600 s") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(429, 86400));
    CHECK_FALSE(c.next(3599999));
    CHECK(c.next(3600000));
}

TEST_CASE("stale outcome: a report changed while its PUT was in flight is sent at once") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    auto a = c.next(0);
    REQUIRE(a);
    c.set_report(game(), std::nullopt);
    c.on_outcome(*a, status(204), 0);
    auto b = c.next(1);
    REQUIRE(b);
    CHECK(b->kind == Kind::PutPresence);
    CHECK(b->body == report_json(game(), std::nullopt));
}

TEST_CASE("stale outcome: a report cleared while its PUT was in flight gives a DELETE") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    auto a = c.next(0);
    REQUIRE(a);
    c.set_report(std::nullopt, std::nullopt);
    c.on_outcome(*a, status(204), 0);
    auto del = c.next(1);
    REQUIRE(del);
    CHECK(del->kind == Kind::DeletePresence);
}

TEST_CASE("NoKey: nothing is ever sent") {
    RelayClient c;
    CHECK(c.link_state() == LinkState::NoKey);
    c.set_report(game(), std::nullopt);
    c.on_wake();
    CHECK_FALSE(first_due(c, 0, 400000));
}
