#include <doctest/doctest.h>

#include "core/relay_client.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace drpc;
using Kind = Request::Kind;

namespace {

constexpr const char* kHash = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";

Report game(const char* name = "Zelda", const char* title_id = "0100A0D02127E000") {
    Report r;
    r.name = name;
    r.details = "Playing";
    r.title_id = std::string{title_id};
    r.icon = std::string{kHash};
    return r;
}

Report home() {
    Report r;
    r.name = "Nintendo Switch";
    r.details = "Home Menu";
    return r;
}

RelayClient keyed() {
    RelayClient c;
    c.set_key(true);
    return c;
}

Outcome status(int s, std::optional<int> retry_after_s = std::nullopt) {
    Outcome out;
    out.status = s;
    out.retry_after_s = retry_after_s;
    return out;
}

// A 200 presence answer from a relay that reads the icon hash.
Outcome need(bool need_icon) {
    Outcome out;
    out.status = 200;
    out.need_icon = need_icon;
    return out;
}

// A heartbeat PUT at `t` answered with `out`, then whatever comes next at t + 1.
std::optional<Request> heartbeat(RelayClient& c, int64_t t, const Outcome& out) {
    auto req = c.next(t);
    REQUIRE(req);
    REQUIRE(req->kind == Kind::PutPresence);
    c.on_outcome(*req, out, t);
    return c.next(t + 1);
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

TEST_CASE("backoff: Retry-After 0 still doubles from 5 s") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(503, 0));
    CHECK_FALSE(c.next(4999));
    answer(c, 5000, status(503, 0));
    CHECK_FALSE(c.next(14999));
    answer(c, 15000, status(503, 0));
    CHECK_FALSE(c.next(34999));
    CHECK(c.next(35000));
}

TEST_CASE("backoff: a Retry-After below the delay does not shorten the window") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    answer(c, 0, status(503));
    answer(c, 5000, status(429, 1));
    CHECK_FALSE(c.next(14999));
    CHECK(c.next(15000));
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

    c.set_key(true);
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
    c.set_key(true);
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

    c.set_key(true);
    CHECK(c.link_state() == LinkState::Ok);
    auto put = c.next(400001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("set_key without an API key: NoKey") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    c.set_key(false);
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

// Runs the client from `from` to `to` in `step` ms steps, answering presence PUTs with `presence` and icon
// uploads with `icon`; returns the instants of the icon uploads.
std::vector<int64_t> icon_uploads(RelayClient& c, int64_t from, int64_t to, const Outcome& presence, const Outcome& icon,
                                  int64_t step = 1000) {
    std::vector<int64_t> at;
    for (int64_t t = from; t <= to; t += step) {
        for (int i = 0; i < 4; ++i) {
            auto req = c.next(t);
            if (!req) break;
            if (req->kind == Kind::PutIcon) at.push_back(t);
            c.on_outcome(*req, req->kind == Kind::PutIcon ? icon : presence, t);
        }
    }
    return at;
}

TEST_CASE("need_icon_true_queues_upload") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    auto icon = heartbeat(c, 0, need(true));
    REQUIRE(icon);
    CHECK(icon->kind == Kind::PutIcon);
    CHECK(icon->title_id == "0100A0D02127E000");
}

TEST_CASE("need_icon_false_queues_nothing") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 700000, need(false), status(200)).empty());
}

TEST_CASE("icons: a 200 presence answer without need_icon queues nothing") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 700000, status(200), status(200)).empty());
}

TEST_CASE("success_then_need_icon_within_10_min_is_ignored") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 599999, need(true), status(200)) == std::vector<int64_t>{0});
}

TEST_CASE("success_then_need_icon_after_10_min_reuploads") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 600000, need(true), status(200)) == std::vector<int64_t>{0, 600000});
}

TEST_CASE("need_icon_after_503_waits_for_backoff") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    // Windows of 60 s, 300 s, 900 s, then 900 s each; a need_icon every heartbeat does not shorten them.
    CHECK(icon_uploads(c, 0, 2160000, need(true), status(503)) ==
          std::vector<int64_t>{0, 60000, 360000, 1260000, 2160000});
    // To the millisecond: nothing between the failure and the end of the first window.
    auto d = keyed();
    d.set_report(game(), std::nullopt);
    CHECK(icon_uploads(d, 0, 60000, need(true), status(503), 1) == std::vector<int64_t>{0, 60000});
}

TEST_CASE("icons: network errors back off like a 5xx and keep retrying") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 1260000, need(true), status(0)) == std::vector<int64_t>{0, 60000, 360000, 1260000});
}

TEST_CASE("icons: a success after failures resets the backoff") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 60000, need(true), status(503)) == std::vector<int64_t>{0, 60000});
    CHECK(icon_uploads(c, 61000, 360000, need(true), status(200)) == std::vector<int64_t>{360000});
    CHECK(icon_uploads(c, 361000, 960000, need(true), status(503)) == std::vector<int64_t>{960000});
    CHECK(icon_uploads(c, 961000, 1020000, need(true), status(503)) == std::vector<int64_t>{1020000});
}

TEST_CASE("upload_400_stops_title") {
    for (int s : {400, 413}) {
        CAPTURE(s);
        auto c = keyed();
        c.set_report(game(), std::nullopt);
        CHECK(icon_uploads(c, 0, 1300000, need(true), status(s)) == std::vector<int64_t>{0});
        CHECK(c.link_state() == LinkState::Ok);

        c.set_report(game("Mario", "0100000000010000"), std::nullopt);
        auto other = heartbeat(c, 1300001, need(true));
        REQUIRE(other);
        CHECK(other->title_id == "0100000000010000");
        c.on_outcome(*other, status(200), 1300002);

        c.set_report(game(), std::nullopt);
        CHECK(icon_uploads(c, 1300003, 2000000, need(true), status(200)).empty());
    }
}

TEST_CASE("icons: any other final answer also stops the title") {
    for (int s : {404, 409, 422}) {
        CAPTURE(s);
        auto c = keyed();
        c.set_report(game(), std::nullopt);
        CHECK(icon_uploads(c, 0, 700000, need(true), status(s)) == std::vector<int64_t>{0});
        CHECK(c.link_state() == LinkState::Ok);
    }
}

TEST_CASE("icons: a failing title does not hold back another one") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(answer(c, 0, need(true)).kind == Kind::PutPresence);
    CHECK(answer(c, 1, status(503)).kind == Kind::PutIcon);
    c.set_report(game("Mario", "0100000000010000"), std::nullopt);
    CHECK(answer(c, 2, need(true)).kind == Kind::PutPresence);
    auto other = c.next(3);
    REQUIRE(other);
    CHECK(other->kind == Kind::PutIcon);
    CHECK(other->title_id == "0100000000010000");
}

TEST_CASE("204_with_icon_falls_back_to_once_per_session") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    auto icon = heartbeat(c, 0, status(204));
    REQUIRE(icon);
    CHECK(icon->kind == Kind::PutIcon);
    CHECK(icon->title_id == "0100A0D02127E000");
    c.on_outcome(*icon, status(200), 1);
    CHECK_FALSE(c.next(2));

    // Never again for this title, even past 10 minutes or after leaving it.
    CHECK(icon_uploads(c, 2, 700000, status(204), status(200)).empty());
    c.set_report(home(), std::nullopt);
    answer(c, 700001, status(204));
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 700002, 800000, status(204), status(200)).empty());
}

TEST_CASE("icons: an old relay's failed upload is retried on the backoff") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 400000, status(204), status(503)) == std::vector<int64_t>{0, 60000, 360000});
}

TEST_CASE("icons: a 204 for a report without a hash still uploads once per session") {
    auto c = keyed();
    Report r = game();
    r.icon.reset();
    c.set_report(r, std::nullopt);
    CHECK(icon_uploads(c, 0, 800000, status(204), status(200)) == std::vector<int64_t>{0});
}

TEST_CASE("icons: an icon the console cannot read yet is retried, not stopped") {
    auto c = keyed();
    Report r = game();
    r.icon.reset();
    c.set_report(r, std::nullopt);
    CHECK(icon_uploads(c, 0, 400000, status(204), status(-1)) == std::vector<int64_t>{0, 60000, 360000});
}

TEST_CASE("icons: a hash read after a failed one is still honoured") {
    // The hash failed at launch: the 204 fallback queues an upload, which cannot read the icon either.
    auto c = keyed();
    Report r = game();
    r.icon.reset();
    c.set_report(r, std::nullopt);
    CHECK(answer(c, 0, status(204)).kind == Kind::PutPresence);
    CHECK(answer(c, 1, status(-1)).kind == Kind::PutIcon);
    // The next read works: the relay asks for the icon and gets it once the retry window ends.
    c.set_report(game(), std::nullopt);
    CHECK(answer(c, 2, need(true)).kind == Kind::PutPresence);
    CHECK(icon_uploads(c, 3, 70000, need(true), status(200)) == std::vector<int64_t>{60003});
}

TEST_CASE("icons: none for a report without a title id") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    CHECK(icon_uploads(c, 0, 100000, need(true), status(200)).empty());
}

TEST_CASE("icons: none on a 4xx PUT") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, status(400));
    CHECK_FALSE(c.next(1));
}

TEST_CASE("icons: a presence PUT goes first, then only the shown title uploads") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
    c.set_report(game("Mario", "0100000000010000"), std::nullopt);
    auto req = c.next(1);
    REQUIRE(req);
    CHECK(req->kind == Kind::PutPresence);
    c.on_outcome(*req, need(true), 1);
    auto first = c.next(2);
    REQUIRE(first);
    CHECK(first->kind == Kind::PutIcon);
    CHECK(first->title_id == "0100000000010000");
    c.on_outcome(*first, status(200), 2);
    CHECK_FALSE(first_due(c, 3, 59999));

    // Zelda's upload waited in the queue and goes once Zelda is shown again.
    c.set_report(game(), std::nullopt);
    CHECK(answer(c, 60000, need(true)).kind == Kind::PutPresence);
    auto back = c.next(60001);
    REQUIRE(back);
    CHECK(back->kind == Kind::PutIcon);
    CHECK(back->title_id == "0100A0D02127E000");
}

TEST_CASE("icons: a queued retrying title is not uploaded after set_report(nullopt)") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 1000, need(true), status(503)) == std::vector<int64_t>{0});
    c.set_report(std::nullopt, std::nullopt);
    CHECK(answer(c, 2000, status(204)).kind == Kind::DeletePresence);
    CHECK_FALSE(first_due(c, 2001, 3000000));
}

TEST_CASE("icons: a queued retrying title is not uploaded in private mode or on the HOME Menu") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 1000, need(true), status(503)) == std::vector<int64_t>{0});
    Report masked = home();
    masked.details = "Playing a game";
    c.set_report(masked, std::nullopt);
    CHECK(icon_uploads(c, 2000, 3000000, status(204), status(200)).empty());
}

TEST_CASE("icons: a queued retrying title is not uploaded after set_report(other title)") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 0, 1000, need(true), status(503)) == std::vector<int64_t>{0});
    c.set_report(game("Mario", "0100000000010000"), std::nullopt);
    // Mario needs nothing; Zelda's retry is never sent while Mario is shown.
    CHECK(icon_uploads(c, 2000, 3000000, need(false), status(200)).empty());

    // Back on Zelda, past its 60 s window: the retry goes with the same backoff state (next window 300 s).
    c.set_report(game(), std::nullopt);
    CHECK(icon_uploads(c, 3001000, 3400000, need(true), status(503)) == std::vector<int64_t>{3001000, 3301000});
}

TEST_CASE("icons: an upload outcome after the title left the screen still counts") {
    for (int s : {200, 400}) {
        CAPTURE(s);
        auto c = keyed();
        c.set_report(game(), std::nullopt);
        answer(c, 0, need(true));
        auto icon = c.next(1);
        REQUIRE(icon);
        REQUIRE(icon->kind == Kind::PutIcon);
        c.set_report(home(), std::nullopt);
        c.on_outcome(*icon, status(s), 1);
        answer(c, 2, status(204));

        // Back on the title within 10 minutes: no upload after a success, none ever after a 400.
        c.set_report(game(), std::nullopt);
        CHECK(icon_uploads(c, 3000, 590000, need(true), status(200)).empty());
        if (s == 400) CHECK(icon_uploads(c, 591000, 1300000, need(true), status(200)).empty());
    }
}

TEST_CASE("icons: a 429 with Retry-After 120 does not delay the presence heartbeat") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
    CHECK(answer(c, 1, status(429, 120)).kind == Kind::PutIcon);
    auto hb = c.next(60000);
    REQUIRE(hb);
    CHECK(hb->kind == Kind::PutPresence);
    c.on_outcome(*hb, need(true), 60000);
    CHECK_FALSE(first_due(c, 60001, 119999, 1));
    CHECK(answer(c, 120001, need(true)).kind == Kind::PutPresence);
    auto icon = c.next(120002);
    REQUIRE(icon);
    CHECK(icon->kind == Kind::PutIcon);
}

TEST_CASE("icons: a 429 without Retry-After backs off 5 s, then 10 s") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
    answer(c, 1, status(429));
    CHECK_FALSE(c.next(5000));
    CHECK(answer(c, 5001, status(429)).kind == Kind::PutIcon);
    CHECK_FALSE(c.next(15000));
    CHECK(c.next(15001));
}

TEST_CASE("icons: none inside a presence backoff window") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
    c.set_report(game("Zelda", "0100000000010000"), std::nullopt);
    CHECK(answer(c, 1, status(503)).kind == Kind::PutPresence);
    CHECK_FALSE(first_due(c, 2, 5000, 1));
    auto put = c.next(5001);
    REQUIRE(put);
    CHECK(put->kind == Kind::PutPresence);
}

TEST_CASE("icons: a 401 on an upload is Unauthorized") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
    CHECK(answer(c, 1, status(401)).kind == Kind::PutIcon);
    CHECK(c.link_state() == LinkState::Unauthorized);
    CHECK_FALSE(first_due(c, 2, 400000));
}

TEST_CASE("icons: a 401 on presence stops them") {
    auto c = keyed();
    c.set_report(game(), std::nullopt);
    answer(c, 0, need(true));
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

TEST_CASE("backoff_left_ms: the presence backoff window, nullopt once it closes") {
    auto c = keyed();
    c.set_report(home(), std::nullopt);
    CHECK_FALSE(c.backoff_left_ms(0).has_value());
    answer(c, 0, status(503));
    CHECK(c.backoff_left_ms(0) == 5000);
    CHECK(c.backoff_left_ms(4000) == 1000);
    CHECK_FALSE(c.backoff_left_ms(5000).has_value());
    answer(c, 5000, status(204));
    CHECK_FALSE(c.backoff_left_ms(5000).has_value());
}

TEST_CASE("parse_need_icon reads a boolean need_icon only") {
    CHECK(parse_need_icon(R"({"need_icon":true})") == true);
    CHECK(parse_need_icon(R"({"need_icon": false, "other": 1})") == false);
    CHECK_FALSE(parse_need_icon(R"({"need_icon":"true"})").has_value());
    CHECK_FALSE(parse_need_icon(R"({"need_icon":1})").has_value());
    CHECK_FALSE(parse_need_icon(R"({"need_icon":null})").has_value());
    CHECK_FALSE(parse_need_icon(R"({"other":true})").has_value());
    CHECK_FALSE(parse_need_icon(R"({"need_icon":true)").has_value());
    CHECK_FALSE(parse_need_icon("").has_value());
    CHECK_FALSE(parse_need_icon(R"({"a":{"need_icon":true}})").has_value());
}
