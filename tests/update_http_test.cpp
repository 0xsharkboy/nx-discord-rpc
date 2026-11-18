#include <doctest/doctest.h>

#include <string>

#include "fake_https.hpp"
#include "update_http.hpp"

using namespace app::http;

namespace {

Options opts(size_t max = 1 << 20) {
    Options o;
    o.user_agent = "nx-discord-rpc/9.9.9";
    o.max_body = max;
    return o;
}

struct Collect {
    std::string body;
    Sink sink() {
        return [this](std::span<const uint8_t> d) {
            body.append(reinterpret_cast<const char*>(d.data()), d.size());
            return true;
        };
    }
};

}  // namespace

TEST_CASE("parse_url: https only, path and query kept") {
    auto u = parse_url("https://GitHub.com/a/b?x=1&y=%2F");
    REQUIRE(u);
    CHECK(u->host == "github.com");
    CHECK(u->target == "/a/b?x=1&y=%2F");
    CHECK(parse_url("https://api.github.com")->target == "/");
    CHECK(parse_url("https://api.github.com:443/x")->host == "api.github.com");
    CHECK_FALSE(parse_url("http://github.com/x"));
    CHECK_FALSE(parse_url("https://github.com:8443/x"));
    CHECK_FALSE(parse_url("https://user@github.com/x"));
    CHECK_FALSE(parse_url("https://github.com@evil.com/x"));
    CHECK_FALSE(parse_url("https://github.com/x#frag"));
    CHECK_FALSE(parse_url("https://github.com/a b"));
    CHECK_FALSE(parse_url("https:///x"));
    CHECK_FALSE(parse_url("/relative"));
    CHECK_FALSE(parse_url("ftp://github.com/x"));
}

TEST_CASE("allowed_host: dot boundary") {
    CHECK(allowed_host("github.com"));
    CHECK(allowed_host("api.github.com"));
    CHECK(allowed_host("objects.githubusercontent.com"));
    CHECK(allowed_host("release-assets.githubusercontent.com"));
    CHECK_FALSE(allowed_host("githubusercontent.com.evil.com"));
    CHECK_FALSE(allowed_host("evilgithub.com"));
    CHECK_FALSE(allowed_host("evilgithubusercontent.com"));
    CHECK_FALSE(allowed_host("github.com.evil.com"));
    CHECK_FALSE(allowed_host(".github.com"));
    CHECK_FALSE(allowed_host(""));
}

TEST_CASE("get_follows_github_redirect") {
    test::FakeHttps t;
    t.responses["github.com/o/r/releases/download/v1.0.0/exefs.nsp"] =
        test::redirect("https://objects.githubusercontent.com/blob/1?sig=abc&exp=1");
    t.responses["objects.githubusercontent.com/blob/1?sig=abc&exp=1"] = test::ok("payload");
    Collect c;
    const Result r = get(t, "https://github.com/o/r/releases/download/v1.0.0/exefs.nsp", opts(), c.sink());
    CHECK(r.error == Error::None);
    CHECK(r.status == 200);
    CHECK(c.body == "payload");
    CHECK(r.bytes == 7);
    REQUIRE(t.heads.size() == 2);
    CHECK(t.heads[1].find("Host: objects.githubusercontent.com\r\n") != std::string::npos);
    CHECK(t.heads[0].find("User-Agent: nx-discord-rpc/9.9.9\r\n") != std::string::npos);
    CHECK(t.heads[0].find("Authorization") == std::string::npos);
}

TEST_CASE("http_redirect_refused: a redirect to plain http stops the download") {
    test::FakeHttps t;
    t.responses["github.com/x"] = test::redirect("http://objects.githubusercontent.com/blob");
    Collect c;
    const Result r = get(t, "https://github.com/x", opts(), c.sink());
    CHECK(r.error == Error::BadRedirect);
    CHECK(t.requests.size() == 1);
    CHECK(c.body.empty());
}

TEST_CASE("redirect_to_other_host_refused") {
    test::FakeHttps t;
    t.responses["github.com/x"] = test::redirect("https://example.com/blob");
    t.responses["example.com/blob"] = test::ok("evil");
    Collect c;
    CHECK(get(t, "https://github.com/x", opts(), c.sink()).error == Error::BadRedirect);
    CHECK(t.requests.size() == 1);
    CHECK(c.body.empty());
}

TEST_CASE("redirect_to_lookalike_host_refused") {
    test::FakeHttps t;
    t.responses["github.com/x"] = test::redirect("https://evilgithub.com/blob");
    t.responses["evilgithub.com/blob"] = test::ok("evil");
    Collect c;
    CHECK(get(t, "https://github.com/x", opts(), c.sink()).error == Error::BadRedirect);
    CHECK(c.body.empty());

    t.responses["github.com/y"] = test::redirect("https://objects.githubusercontent.com.evil.com/blob");
    CHECK(get(t, "https://github.com/y", opts(), c.sink()).error == Error::BadRedirect);
    t.responses["github.com/z"] = test::redirect("/relative/path");
    CHECK(get(t, "https://github.com/z", opts(), c.sink()).error == Error::BadRedirect);
    t.responses["github.com/w"] = "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n";
    CHECK(get(t, "https://github.com/w", opts(), c.sink()).error == Error::BadRedirect);
}

TEST_CASE("redirect_limit") {
    test::FakeHttps t;
    t.responses["github.com/0"] = test::redirect("https://github.com/1", 301);
    t.responses["github.com/1"] = test::redirect("https://github.com/2", 307);
    t.responses["github.com/2"] = test::redirect("https://github.com/3", 308);
    t.responses["github.com/3"] = test::redirect("https://github.com/4");
    t.responses["github.com/4"] = test::ok("too far");
    Collect c;
    CHECK(get(t, "https://github.com/0", opts(), c.sink()).error == Error::TooManyRedirects);
    CHECK(c.body.empty());
    Collect c2;
    CHECK(get(t, "https://github.com/1", opts(), c2.sink()).error == Error::None);
    CHECK(c2.body == "too far");
}

TEST_CASE("first_url_must_be_allowed") {
    test::FakeHttps t;
    Collect c;
    CHECK(get(t, "https://example.com/x", opts(), c.sink()).error == Error::Refused);
    CHECK(get(t, "http://github.com/x", opts(), c.sink()).error == Error::Refused);
    CHECK(t.requests.empty());
}

TEST_CASE("get_status_and_size_caps") {
    test::FakeHttps t;
    t.responses["api.github.com/missing"] = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found";
    t.responses["api.github.com/big"] = test::ok(std::string(100, 'x'));
    t.responses["api.github.com/chunked-big"] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n40\r\n" + std::string(64, 'y') + "\r\n40\r\n" +
        std::string(64, 'y') + "\r\n0\r\n\r\n";
    t.responses["api.github.com/chunked"] =
        "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\n3;ext=1\r\nabc\r\n2\r\nde\r\n0\r\nX-T: 1\r\n\r\n";
    t.responses["api.github.com/short"] = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc";
    t.responses["api.github.com/garbage"] = "SSH-2.0-OpenSSH\r\n\r\n";
    Collect c;
    const Result missing = get(t, "https://api.github.com/missing", opts(), c.sink());
    CHECK(missing.error == Error::Status);
    CHECK(missing.status == 404);
    CHECK(c.body.empty());
    CHECK(get(t, "https://api.github.com/big", opts(99), c.sink()).error == Error::TooLarge);
    CHECK(get(t, "https://api.github.com/chunked-big", opts(100), c.sink()).error == Error::TooLarge);
    Collect ch;
    CHECK(get(t, "https://api.github.com/chunked", opts(5), ch.sink()).error == Error::None);
    CHECK(ch.body == "abcde");
    CHECK(get(t, "https://api.github.com/short", opts(), c.sink()).error == Error::Network);
    CHECK(get(t, "https://api.github.com/garbage", opts(), c.sink()).error == Error::BadResponse);
    t.fail_connect = true;
    CHECK(get(t, "https://api.github.com/big", opts(), c.sink()).error == Error::Network);
}

TEST_CASE("sink_abort_stops") {
    test::FakeHttps t;
    t.responses["github.com/x"] = test::ok(std::string(1000, 'z'));
    size_t seen = 0;
    const Result r = get(t, "https://github.com/x", opts(), [&](std::span<const uint8_t> d) {
        seen += d.size();
        return seen < 20;
    });
    CHECK(r.error == Error::Aborted);
    CHECK(seen < 1000);
}
