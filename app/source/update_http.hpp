#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "drpc/transport.hpp"

// The updater's HTTPS client: GET only, streamed to a sink, with GitHub's redirects. It is separate from
// core/http, which buffers whole bodies and serves the sysmodule's relay requests.
namespace app::http {

struct Url {
    std::string host;  // lowercase
    std::string target;  // path and query, verbatim; at least "/"
};

// https://host[:443]/path?query only. Rejects other schemes and ports, userinfo, fragments, whitespace and
// control characters.
std::optional<Url> parse_url(std::string_view url);

// GitHub's own hosts: github.com, api.github.com, or a name ending in ".github.com" or
// ".githubusercontent.com" (whole labels, so "evilgithub.com" is refused).
bool allowed_host(std::string_view host);

inline constexpr int kMaxRedirects = 3;

enum class Error {
    None,
    Network,        // connect, write or read failed or timed out
    BadResponse,    // not HTTP, head too big, broken chunking
    Status,         // a final status other than 200
    TooLarge,       // the body is over the cap
    BadRedirect,    // Location missing, not https, or to a host outside the allowlist
    TooManyRedirects,
    Refused,        // the first URL itself is not allowed
    Aborted,        // the sink returned false
};

struct Result {
    Error error = Error::None;
    int status = 0;
    size_t bytes = 0;  // body bytes given to the sink
};

// Gets body bytes in order; false stops the transfer (Error::Aborted).
using Sink = std::function<bool(std::span<const uint8_t>)>;

struct Options {
    std::string user_agent;  // e.g. "nx-discord-rpc/0.6.0"
    std::string accept = "*/*";
    size_t max_body = 0;
    int read_timeout_ms = 15'000;
};

// Incremental response parser: status line and headers, then a Content-Length or chunked body handed to a
// sink. A response with neither ends at connection close only for redirects, which carry no useful body.
class StreamParser {
public:
    StreamParser(size_t max_body, Sink sink) : max_body_(max_body), sink_(std::move(sink)) {}

    // Feeds bytes; stops at the first error.
    void feed(std::span<const uint8_t> data);
    // The connection closed: completes a body that runs to the close.
    void closed();

    bool head_done() const { return head_done_; }
    bool done() const { return state_ == State::Done; }
    Error error() const { return error_; }
    int status() const { return status_; }
    const std::string& location() const { return location_; }
    size_t bytes() const { return bytes_; }

    // Bodies of non-200 answers are dropped, not sent to the sink.
    void set_discard_body(bool discard) { discard_ = discard; }

private:
    enum class State { Head, Body, UntilClose, ChunkSize, ChunkData, ChunkDataEnd, Trailer, Done, Failed };

    void fail(Error e);
    void parse_head();
    bool line_byte(char c);
    void parse_chunk_size();
    bool body_bytes(const uint8_t* p, size_t n);

    State state_ = State::Head;
    Error error_ = Error::None;
    std::string head_;
    std::string line_;
    int status_ = 0;
    std::string location_;
    size_t max_body_;
    size_t remaining_ = 0;
    size_t bytes_ = 0;
    bool head_done_ = false;
    bool discard_ = false;
    Sink sink_;
};

// GET url over t, following up to kMaxRedirects redirects that pass allowed_host. Every hop opens a new
// connection (Connection: close). Only a 200 body reaches the sink.
Result get(drpc::Transport& t, std::string_view url, const Options& options, const Sink& sink);

const char* error_name(Error e);

}  // namespace app::http
