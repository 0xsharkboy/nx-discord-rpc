#include <doctest/doctest.h>

#include <string>

#include "core/http.hpp"
#include "core/relay_url.hpp"

namespace {

drpc::HttpResponseParser parse(std::string_view raw) {
    drpc::HttpResponseParser p;
    p.feed(raw);
    return p;
}

}  // namespace

TEST_CASE("chunked response split at every byte offset") {
    const std::string raw =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5;ext=1\r\nhello\r\n"
        "B\r\n, chunked w\r\n"
        "4\r\norld\r\n"
        "0\r\nX-Trailer: t\r\n\r\n";
    for (size_t cut = 0; cut <= raw.size(); ++cut) {
        CAPTURE(cut);
        drpc::HttpResponseParser p;
        p.feed(std::string_view(raw).substr(0, cut));
        p.feed(std::string_view(raw).substr(cut));
        REQUIRE(p.done());
        CHECK_FALSE(p.failed());
        const drpc::HttpResponse r = p.take();
        CHECK(r.status == 200);
        CHECK(r.body == "hello, chunked world");
    }
}

TEST_CASE("chunked response fed byte by byte") {
    const std::string raw = "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n";
    drpc::HttpResponseParser p;
    for (char c : raw) {
        CHECK_FALSE(p.done());
        p.feed(std::string_view(&c, 1));
    }
    REQUIRE(p.done());
    CHECK(p.take().body == "abc");
}

TEST_CASE("Content-Length response ignores trailing bytes") {
    auto p = parse("HTTP/1.1 201 Created\r\ncontent-length: 4\r\n\r\nbodyGARBAGE");
    REQUIRE(p.done());
    CHECK_FALSE(p.failed());
    const auto r = p.take();
    CHECK(r.status == 201);
    CHECK(r.body == "body");
}

TEST_CASE("incomplete response is neither done nor failed") {
    auto p = parse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc");
    CHECK_FALSE(p.done());
    CHECK_FALSE(p.failed());
}

TEST_CASE("body over 4 KB fails") {
    const std::string big(4097, 'x');
    CHECK(parse("HTTP/1.1 200 OK\r\nContent-Length: 4097\r\n\r\n" + big).failed());

    drpc::HttpResponseParser chunked;
    chunked.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
    for (int i = 0; i < 5; ++i) chunked.feed("400\r\n" + std::string(1024, 'y') + "\r\n");
    CHECK(chunked.failed());
    CHECK_FALSE(chunked.done());

    auto exact = parse("HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n" + std::string(4096, 'z'));
    CHECK(exact.done());
    CHECK_FALSE(exact.failed());
}

TEST_CASE("malformed responses fail") {
    CHECK(parse("garbage\r\n\r\n").failed());
    CHECK(parse("HTTP/1.1 abc OK\r\n\r\n").failed());
    CHECK(parse("HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n").failed());
    CHECK(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n").failed());
    CHECK(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naXX").failed());
    CHECK(parse("HTTP/1.1 200 OK\r\n" + std::string(9000, 'h')).failed());
}

TEST_CASE("response without a body length") {
    CHECK(parse("HTTP/1.1 204 No Content\r\n\r\n").done());
    CHECK(parse("HTTP/1.1 200 OK\r\n\r\nuntil close").failed());
}

TEST_CASE("rate limits and server errors are worth retrying soon, other errors are not") {
    for (int status : {429, 500, 502, 503, 504})
        CHECK(drpc::is_transient_http_status(status));
    for (int status : {400, 401, 403, 404, 413})
        CHECK_FALSE(drpc::is_transient_http_status(status));
}


TEST_CASE("build_request with base path and port") {
    const auto url = drpc::parse_relay_url("https://h.example:8443/relay");
    REQUIRE(url);
    CHECK(drpc::build_request("PUT", *url, "/v1/presence", "key", "application/json", 98) ==
          "PUT /relay/v1/presence HTTP/1.1\r\n"
          "Host: h.example:8443\r\n"
          "User-Agent: nx-discord-rpc/0.1.0\r\n"
          "Authorization: Bearer key\r\n"
          "Content-Type: application/json\r\n"
          "Content-Length: 98\r\n"
          "Connection: close\r\n\r\n");
}

TEST_CASE("build_request default port without base path") {
    const auto url = drpc::parse_relay_url("https://h.example");
    REQUIRE(url);
    CHECK(drpc::build_request("PUT", *url, "/v1/presence", "key", "application/json", 98) ==
          "PUT /v1/presence HTTP/1.1\r\n"
          "Host: h.example\r\n"
          "User-Agent: nx-discord-rpc/0.1.0\r\n"
          "Authorization: Bearer key\r\n"
          "Content-Type: application/json\r\n"
          "Content-Length: 98\r\n"
          "Connection: close\r\n\r\n");
}

TEST_CASE("build_request DELETE has no body headers") {
    const auto url = drpc::parse_relay_url("https://h.example");
    REQUIRE(url);
    CHECK(drpc::build_request("DELETE", *url, "/v1/presence", "key", "", 0) ==
          "DELETE /v1/presence HTTP/1.1\r\n"
          "Host: h.example\r\n"
          "User-Agent: nx-discord-rpc/0.1.0\r\n"
          "Authorization: Bearer key\r\n"
          "Content-Length: 0\r\n"
          "Connection: close\r\n\r\n");
}

TEST_CASE("build_request omits Authorization without bearer") {
    const auto url = drpc::parse_relay_url("https://h.example");
    REQUIRE(url);
    CHECK(drpc::build_request("GET", *url, "/x", "", "", 0).find("Authorization") == std::string::npos);
}

TEST_CASE("bodiless 204 is done after the head") {
    auto p = parse("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    CHECK(p.done());
    CHECK(p.take().status == 204);
}

TEST_CASE("Retry-After seconds") {
    auto p = parse("HTTP/1.1 429 Too Many Requests\r\nretry-after: 360\r\nContent-Length: 0\r\n\r\n");
    REQUIRE(p.done());
    const auto r = p.take();
    CHECK(r.status == 429);
    REQUIRE(r.retry_after_s);
    CHECK(*r.retry_after_s == 360);
}

TEST_CASE("Retry-After other forms are ignored") {
    for (const char* v : {"Wed, 21 Oct 2015 07:28:00 GMT", "-1", "86401", "", "1.5", "99999999999999"}) {
        CAPTURE(v);
        auto p = parse(std::string("HTTP/1.1 503 Service Unavailable\r\nRetry-After: ") + v +
                       "\r\nContent-Length: 0\r\n\r\n");
        REQUIRE(p.done());
        CHECK_FALSE(p.take().retry_after_s);
    }
}

TEST_CASE("409 JSON body is captured") {
    auto p = parse("HTTP/1.1 409 Conflict\r\nContent-Length: 17\r\n\r\n{\"error\":\"taken\"}");
    REQUIRE(p.done());
    const auto r = p.take();
    CHECK(r.status == 409);
    CHECK(r.body == "{\"error\":\"taken\"}");
}
