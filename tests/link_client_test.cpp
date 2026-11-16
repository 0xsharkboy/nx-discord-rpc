#include <doctest/doctest.h>

#include "core/link_client.hpp"

#include <optional>
#include <string>

using namespace drpc;
using Kind = LinkRequest::Kind;
using ipc::LinkFailure;
using ipc::LinkPhase;
using ipc::UnlinkResult;

namespace {

const std::string kKey(43, 'k');
constexpr const char* kStartBody =
    R"({"link_id":"L","user_code":"ABCD-EFGH","verification_uri_complete":"https://discord.com/activate?user_code=ABCD-EFGH","expires_in":300,"interval":5})";

LinkResponse resp(int status, std::string body = {}, std::optional<int> retry_after_s = std::nullopt) {
    return LinkResponse{status, std::move(body), retry_after_s};
}

LinkEffect answer(LinkClient& c, int64_t now, const LinkResponse& r, Kind expected) {
    auto req = c.next(now);
    REQUIRE(req);
    CHECK(req->kind == expected);
    return c.on_response(*req, r, now);
}

LinkClient pending_at_zero() {
    LinkClient c;
    c.start(0);
    answer(c, 0, resp(200, kStartBody), Kind::Start);
    REQUIRE(c.view(0).phase == LinkPhase::Pending);
    return c;
}

std::optional<int64_t> first_due(LinkClient& c, int64_t from, int64_t to, int64_t step = 500) {
    for (int64_t t = from; t <= to; t += step) {
        if (c.next(t)) return t;
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("idle: nothing to send, empty view") {
    LinkClient c;
    CHECK_FALSE(c.next(0));
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Idle);
    CHECK(v.failure == LinkFailure::None);
    CHECK(v.seconds_left == 0);
    CHECK(v.user_code.empty());
    CHECK(v.verification_uri.empty());
    CHECK(v.username.empty());
    CHECK(c.unlink_result() == UnlinkResult::None);
}

TEST_CASE("start: a Start request, then Pending with the code and URI") {
    LinkClient c;
    c.start(0);
    CHECK(c.view(0).phase == LinkPhase::Starting);
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->kind == Kind::Start);
    auto fx = c.on_response(*req, resp(200, kStartBody), 0);
    CHECK_FALSE(fx.api_key);
    CHECK_FALSE(fx.username);
    CHECK_FALSE(fx.clear_key);

    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Pending);
    CHECK(v.failure == LinkFailure::None);
    CHECK(v.user_code == "ABCD-EFGH");
    CHECK(v.verification_uri == "https://discord.com/activate?user_code=ABCD-EFGH");
    CHECK(v.seconds_left == 300);
    CHECK(c.view(1500).seconds_left == 299);
}

TEST_CASE("start: defaults for expires_in and interval, interval at least 2 s") {
    LinkClient c;
    c.start(0);
    answer(c, 0, resp(200, R"({"link_id":"L","user_code":"C","verification_uri_complete":"U"})"), Kind::Start);
    CHECK(c.view(0).seconds_left == 300);
    CHECK(first_due(c, 0, 10000) == 5000);

    LinkClient fast;
    fast.start(0);
    answer(fast, 0,
           resp(200, R"({"link_id":"L","user_code":"C","verification_uri_complete":"U","interval":1})"),
           Kind::Start);
    CHECK(first_due(fast, 0, 10000) == 2000);
}

TEST_CASE("review focus 1: a second start while Starting or Pending sends no second Start") {
    LinkClient c;
    c.start(0);
    c.start(0);
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->kind == Kind::Start);
    c.start(10);
    c.on_response(*req, resp(200, kStartBody), 10);
    CHECK(c.view(10).phase == LinkPhase::Pending);

    c.start(20);
    CHECK(c.view(20).phase == LinkPhase::Pending);
    for (int64_t t = 20; t < 300000; t += 500) {
        auto r = c.next(t);
        if (!r) continue;
        CHECK(r->kind == Kind::Poll);
        c.on_response(*r, resp(200, R"({"state":"pending"})"), t);
        c.start(t);
    }
}

TEST_CASE("polling: due every interval after the previous answer") {
    auto c = pending_at_zero();
    CHECK_FALSE(c.next(4999));
    auto poll = c.next(5000);
    REQUIRE(poll);
    CHECK(poll->kind == Kind::Poll);
    CHECK(poll->link_id == "L");
    c.on_response(*poll, resp(200, R"({"state":"pending"})"), 5000);
    CHECK(c.view(5000).phase == LinkPhase::Pending);
    CHECK_FALSE(c.next(9999));
    auto again = c.next(10000);
    REQUIRE(again);
    CHECK(again->kind == Kind::Poll);
}

TEST_CASE("polling: linked carries the key and the username") {
    auto c = pending_at_zero();
    auto fx = answer(c, 5000, resp(200, R"({"state":"linked","api_key":")" + kKey + R"(","username":"test_rpc"})"),
                     Kind::Poll);
    REQUIRE(fx.api_key);
    CHECK(*fx.api_key == kKey);
    REQUIRE(fx.username);
    CHECK(*fx.username == "test_rpc");
    CHECK_FALSE(fx.clear_key);
    auto v = c.view(5000);
    CHECK(v.phase == LinkPhase::Linked);
    CHECK(v.username == "test_rpc");
    CHECK(v.seconds_left == 0);
    CHECK_FALSE(first_due(c, 5000, 400000));
}

TEST_CASE("polling: denied and expired are final") {
    auto denied = pending_at_zero();
    answer(denied, 5000, resp(200, R"({"state":"denied"})"), Kind::Poll);
    CHECK(denied.view(5000).phase == LinkPhase::Denied);
    CHECK_FALSE(first_due(denied, 5000, 400000));

    auto expired = pending_at_zero();
    answer(expired, 5000, resp(200, R"({"state":"expired"})"), Kind::Poll);
    CHECK(expired.view(5000).phase == LinkPhase::Expired);
    CHECK_FALSE(first_due(expired, 5000, 400000));
}

TEST_CASE("polling: a 42-character key fails with Other") {
    auto c = pending_at_zero();
    auto fx = answer(c, 5000,
                     resp(200, R"({"state":"linked","api_key":")" + std::string(42, 'k') + R"(","username":"u"})"),
                     Kind::Poll);
    CHECK_FALSE(fx.api_key);
    CHECK_FALSE(fx.username);
    auto v = c.view(5000);
    CHECK(v.phase == LinkPhase::Failed);
    CHECK(v.failure == LinkFailure::Other);
    CHECK_FALSE(first_due(c, 5000, 400000));
}

TEST_CASE("review focus 2: sleep while Pending interrupts the link for good") {
    auto c = pending_at_zero();
    c.on_sleep();
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Failed);
    CHECK(v.failure == LinkFailure::Interrupted);
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("review focus 2: sleep while Starting interrupts the link") {
    LinkClient c;
    c.start(0);
    c.on_sleep();
    CHECK(c.view(0).phase == LinkPhase::Failed);
    CHECK(c.view(0).failure == LinkFailure::Interrupted);
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("review focus 2: Pending without answers expires at expires_in, with no poll from then on") {
    auto c = pending_at_zero();
    for (int64_t t = 0; t < 300000; t += 500) {
        auto r = c.next(t);
        if (!r) continue;
        CHECK(r->kind == Kind::Poll);
        c.on_response(*r, resp(0), t);
    }
    CHECK(c.view(299500).phase == LinkPhase::Pending);
    CHECK(c.view(300000).phase == LinkPhase::Expired);
    CHECK(c.view(300000).seconds_left == 0);
    CHECK_FALSE(first_due(c, 300000, 1000000));
    CHECK(c.view(300000).phase == LinkPhase::Expired);
}

TEST_CASE("errors: Start 429 is RateLimited with Retry-After") {
    LinkClient c;
    c.start(0);
    answer(c, 0, resp(429, R"({"error":"rate_limited"})", 360), Kind::Start);
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Failed);
    CHECK(v.failure == LinkFailure::RateLimited);
    CHECK(v.retry_after_s == 360);
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("errors: Start network error or 5xx is Unreachable, other is Other") {
    for (int status : {0, 502, 503}) {
        LinkClient c;
        c.start(0);
        answer(c, 0, resp(status), Kind::Start);
        CHECK(c.view(0).phase == LinkPhase::Failed);
        CHECK(c.view(0).failure == LinkFailure::Unreachable);
    }
    for (auto [status, body] : {std::pair{400, std::string{}}, std::pair{200, std::string{"{not json"}},
                                std::pair{200, std::string{R"({"user_code":"C"})"}}}) {
        LinkClient c;
        c.start(0);
        answer(c, 0, resp(status, body), Kind::Start);
        CHECK(c.view(0).phase == LinkPhase::Failed);
        CHECK(c.view(0).failure == LinkFailure::Other);
    }
}

TEST_CASE("errors: a Poll 503, 429 or network error keeps polling") {
    auto c = pending_at_zero();
    answer(c, 5000, resp(503), Kind::Poll);
    CHECK(c.view(5000).phase == LinkPhase::Pending);
    CHECK(first_due(c, 5500, 20000) == 10000);
    answer(c, 10000, resp(0), Kind::Poll);
    CHECK(c.view(10000).phase == LinkPhase::Pending);
    CHECK(first_due(c, 10500, 20000) == 15000);
    answer(c, 15000, resp(429), Kind::Poll);
    CHECK(c.view(15000).phase == LinkPhase::Pending);
    CHECK(first_due(c, 15500, 30000) == 20000);
}

TEST_CASE("errors: a Poll 404 fails with Other") {
    auto c = pending_at_zero();
    answer(c, 5000, resp(404, R"({"error":"unknown_link"})"), Kind::Poll);
    CHECK(c.view(5000).phase == LinkPhase::Failed);
    CHECK(c.view(5000).failure == LinkFailure::Other);
    CHECK_FALSE(first_due(c, 5000, 400000));
}

TEST_CASE("cancel: Pending goes Idle with no more requests") {
    auto c = pending_at_zero();
    c.cancel();
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Idle);
    CHECK(v.user_code.empty());
    CHECK(v.verification_uri.empty());
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("cancel: a late Start answer is ignored") {
    LinkClient c;
    c.start(0);
    auto req = c.next(0);
    REQUIRE(req);
    c.cancel();
    c.on_response(*req, resp(200, kStartBody), 0);
    CHECK(c.view(0).phase == LinkPhase::Idle);
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("unlink: comes before a due Poll; 204 is Done and clears the key") {
    auto c = pending_at_zero();
    c.unlink();
    CHECK(c.unlink_result() == UnlinkResult::InProgress);
    auto req = c.next(5000);
    REQUIRE(req);
    CHECK(req->kind == Kind::Unlink);
    c.unlink();
    auto fx = c.on_response(*req, resp(204), 5000);
    CHECK(fx.clear_key);
    CHECK(c.unlink_result() == UnlinkResult::Done);
    auto poll = c.next(5000);
    REQUIRE(poll);
    CHECK(poll->kind == Kind::Poll);
}

TEST_CASE("unlink: 401 is Done, network error and other statuses are Failed") {
    LinkClient done;
    done.unlink();
    auto fx = answer(done, 0, resp(401), Kind::Unlink);
    CHECK(fx.clear_key);
    CHECK(done.unlink_result() == UnlinkResult::Done);
    CHECK_FALSE(done.next(0));

    for (int status : {0, 429, 503, 400, 200}) {
        LinkClient c;
        c.unlink();
        auto f = answer(c, 0, resp(status), Kind::Unlink);
        CHECK_FALSE(f.clear_key);
        CHECK(c.unlink_result() == UnlinkResult::Failed);
        CHECK_FALSE(c.next(0));
    }
}

TEST_CASE("unlink: a second unlink while one is in flight sends nothing more") {
    LinkClient c;
    c.unlink();
    c.unlink();
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->kind == Kind::Unlink);
    c.unlink();
    CHECK(c.unlink_result() == UnlinkResult::InProgress);
    c.on_response(*req, resp(204), 0);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("fetch me: one request, the username lands in the effect") {
    LinkClient c;
    c.request_username();
    c.request_username();
    auto req = c.next(0);
    REQUIRE(req);
    CHECK(req->kind == Kind::FetchMe);
    c.request_username();
    auto fx = c.on_response(*req, resp(200, R"({"username":"test_rpc","link_broken":false,"presence":null})"), 0);
    REQUIRE(fx.username);
    CHECK(*fx.username == "test_rpc");
    CHECK_FALSE(fx.api_key);
    CHECK_FALSE(fx.clear_key);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("fetch me: a failure yields nothing and is not retried until asked again") {
    LinkClient c;
    c.request_username();
    auto fx = answer(c, 0, resp(503), Kind::FetchMe);
    CHECK_FALSE(fx.username);
    CHECK_FALSE(first_due(c, 0, 1000000));

    c.request_username();
    fx = answer(c, 0, resp(200, R"({"link_broken":false})"), Kind::FetchMe);
    CHECK_FALSE(fx.username);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("priority: Unlink, then Start, then Poll, then FetchMe") {
    LinkClient c;
    c.request_username();
    c.start(0);
    c.unlink();
    answer(c, 0, resp(204), Kind::Unlink);
    answer(c, 0, resp(200, kStartBody), Kind::Start);
    answer(c, 0, resp(200, R"({"username":"u"})"), Kind::FetchMe);
    c.request_username();
    answer(c, 5000, resp(200, R"({"state":"pending"})"), Kind::Poll);
    answer(c, 5000, resp(200, R"({"username":"u"})"), Kind::FetchMe);
}

TEST_CASE("errors: a Start 200 with an empty code or URI fails with Other") {
    for (const char* body : {R"({"link_id":"L","user_code":"","verification_uri_complete":"U"})",
                             R"({"link_id":"L","user_code":"C","verification_uri_complete":""})"}) {
        LinkClient c;
        c.start(0);
        answer(c, 0, resp(200, body), Kind::Start);
        CHECK(c.view(0).phase == LinkPhase::Failed);
        CHECK(c.view(0).failure == LinkFailure::Other);
        CHECK_FALSE(first_due(c, 0, 1000000));
    }
}

TEST_CASE("fetch me: the username also updates the Linked view") {
    auto c = pending_at_zero();
    answer(c, 5000, resp(200, R"({"state":"linked","api_key":")" + kKey + R"(","username":"old"})"), Kind::Poll);
    c.request_username();
    answer(c, 5000, resp(200, R"({"username":"new_name"})"), Kind::FetchMe);
    CHECK(c.view(5000).username == "new_name");
}

TEST_CASE("fetch me: Idle view stays empty") {
    LinkClient c;
    c.request_username();
    answer(c, 0, resp(200, R"({"username":"test_rpc"})"), Kind::FetchMe);
    CHECK(c.view(0).username.empty());
}

TEST_CASE("sleep: a late Start answer after on_sleep is ignored") {
    LinkClient c;
    c.start(0);
    auto req = c.next(0);
    REQUIRE(req);
    c.on_sleep();
    auto fx = c.on_response(*req, resp(200, kStartBody), 0);
    CHECK_FALSE(fx.api_key);
    CHECK_FALSE(fx.username);
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Failed);
    CHECK(v.failure == LinkFailure::Interrupted);
    CHECK(v.user_code.empty());
    CHECK(v.verification_uri.empty());
    CHECK_FALSE(first_due(c, 0, 1000000));
}

TEST_CASE("cancel: a late linked Poll answer is ignored") {
    auto c = pending_at_zero();
    auto req = c.next(5000);
    REQUIRE(req);
    REQUIRE(req->kind == Kind::Poll);
    c.cancel();
    auto fx = c.on_response(*req, resp(200, R"({"state":"linked","api_key":")" + kKey + R"(","username":"u"})"), 5000);
    CHECK_FALSE(fx.api_key);
    CHECK_FALSE(fx.username);
    CHECK_FALSE(fx.clear_key);
    CHECK(c.view(5000).phase == LinkPhase::Idle);
    CHECK(c.view(5000).username.empty());
    CHECK_FALSE(first_due(c, 5000, 1000000));
}

TEST_CASE("unlink: after a failure, unlink() sends a new Unlink") {
    LinkClient c;
    c.unlink();
    answer(c, 0, resp(0), Kind::Unlink);
    CHECK(c.unlink_result() == UnlinkResult::Failed);
    c.unlink();
    CHECK(c.unlink_result() == UnlinkResult::InProgress);
    auto fx = answer(c, 0, resp(204), Kind::Unlink);
    CHECK(fx.clear_key);
    CHECK(c.unlink_result() == UnlinkResult::Done);
}

TEST_CASE("forget: a pending link goes Idle and nothing more is sent") {
    auto c = pending_at_zero();
    c.forget();
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Idle);
    CHECK(v.user_code.empty());
    CHECK(v.verification_uri.empty());
    CHECK_FALSE(first_due(c, 0, 600000));
}

TEST_CASE("forget: a queued unlink is dropped") {
    LinkClient c;
    c.unlink();
    c.forget();
    CHECK(c.unlink_result() == UnlinkResult::None);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("forget: an unlink answered after forget has no effect") {
    LinkClient c;
    c.unlink();
    auto req = c.next(0);
    REQUIRE(req);
    REQUIRE(req->kind == Kind::Unlink);
    c.forget();
    auto fx = c.on_response(*req, resp(204), 0);
    CHECK_FALSE(fx.clear_key);
    CHECK(c.unlink_result() == UnlinkResult::None);
}

TEST_CASE("forget: a username fetch answered after forget has no effect") {
    LinkClient c;
    c.request_username();
    auto req = c.next(0);
    REQUIRE(req);
    REQUIRE(req->kind == Kind::FetchMe);
    c.forget();
    auto fx = c.on_response(*req, resp(200, R"({"username":"u"})"), 0);
    CHECK_FALSE(fx.username);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("forget: a linked view loses its username") {
    auto c = pending_at_zero();
    auto fx = answer(c, 5000, resp(200, R"({"state":"linked","api_key":")" + kKey + R"(","username":"test_rpc"})"),
                     Kind::Poll);
    REQUIRE(fx.api_key);
    REQUIRE(c.view(5000).username == "test_rpc");
    c.forget();
    CHECK(c.view(5000).phase == LinkPhase::Idle);
    CHECK(c.view(5000).username.empty());
}

TEST_CASE("start: past the deadline, a Pending link expires and a new Start goes out") {
    auto c = pending_at_zero();
    c.start(300000);
    auto req = c.next(300000);
    REQUIRE(req);
    CHECK(req->kind == Kind::Start);
    CHECK(c.view(300000).phase == LinkPhase::Starting);
}

TEST_CASE("start: before the deadline, a Pending link stays and no Start goes out") {
    auto c = pending_at_zero();
    c.start(299999);
    CHECK(c.view(299999).phase == LinkPhase::Pending);
    auto req = c.next(299999);
    REQUIRE(req);
    CHECK(req->kind == Kind::Poll);
}

TEST_CASE("offline: an unsent Unlink fails and nothing is sent") {
    LinkClient c;
    c.unlink();
    c.on_offline();
    CHECK(c.unlink_result() == UnlinkResult::Failed);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("offline: an unsent Start fails as Unreachable and nothing is sent") {
    LinkClient c;
    c.start(0);
    c.on_offline();
    auto v = c.view(0);
    CHECK(v.phase == LinkPhase::Failed);
    CHECK(v.failure == LinkFailure::Unreachable);
    CHECK_FALSE(c.next(0));
}

TEST_CASE("offline: a queued username fetch survives and goes out once back online") {
    LinkClient c;
    c.request_username();
    c.on_offline();
    auto fx = answer(c, 0, resp(200, R"({"username":"u"})"), Kind::FetchMe);
    CHECK(fx.username == "u");
}

TEST_CASE("offline: Pending is untouched and still polls, then expires on its own") {
    auto c = pending_at_zero();
    c.on_offline();
    CHECK(c.view(0).phase == LinkPhase::Pending);
    auto req = c.next(5000);
    REQUIRE(req);
    CHECK(req->kind == Kind::Poll);
    c.on_offline();
    CHECK(c.view(300000).phase == LinkPhase::Expired);
}

TEST_CASE("offline: idle, linked and finished states are untouched") {
    LinkClient c;
    c.on_offline();
    CHECK(c.view(0).phase == LinkPhase::Idle);
    CHECK(c.unlink_result() == UnlinkResult::None);

    LinkClient done;
    done.unlink();
    answer(done, 0, resp(204), Kind::Unlink);
    done.on_offline();
    CHECK(done.unlink_result() == UnlinkResult::Done);
}
