#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <span>
#include <string_view>

#include "core/relay_url.hpp"
#include "drpc/transport.hpp"

namespace drpc {

inline constexpr size_t kHttpMaxBody = 4096;

struct HttpResponse {
    int status = 0;
    std::string body;
    std::optional<int> retry_after_s;
    const char* note = nullptr;  // why a round trip failed or came back short; a string literal
};

// Head only; the caller writes the body (exactly body_size bytes) separately.
std::string build_request(std::string_view method, const RelayUrl& url, std::string_view path,
                          std::string_view bearer, std::string_view content_type, size_t body_size);

// 429 and 5xx: the same request may succeed a few minutes later.
bool is_transient_http_status(int status);

// Incremental HTTP/1.1 response parser for Content-Length and chunked bodies.
// Bytes after the end of the response are ignored; bodies over max_body fail.
class HttpResponseParser {
public:
    explicit HttpResponseParser(size_t max_body = kHttpMaxBody) : max_body_(max_body) {}

    void feed(std::string_view chunk);
    bool done() const { return state_ == State::Done; }
    bool failed() const { return state_ == State::Failed; }
    // The status line and headers parsed, even if the body then failed.
    bool head_done() const { return head_done_; }
    HttpResponse take();

private:
    enum class State { Head, Body, ChunkSize, ChunkData, ChunkDataEnd, Trailer, Done, Failed };

    void step(char c);
    bool line_byte(char c);
    void parse_head();
    void parse_chunk_size();
    void append_body(char c);

    State state_ = State::Head;
    std::string line_;
    std::string head_;
    HttpResponse response_;
    size_t max_body_;
    size_t remaining_ = 0;
    bool head_done_ = false;
};

// One request over t: connects, writes head then body, reads one response. Status 0 on a network
// error; the body is empty unless the response completed. note says what went wrong, if anything.
HttpResponse round_trip(Transport& t, const RelayUrl& url, std::string_view head, std::span<const uint8_t> body,
                        size_t max_body, int read_timeout_ms);

}  // namespace drpc
