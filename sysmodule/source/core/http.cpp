#include "core/http.hpp"

#include <charconv>
#include <optional>

#include "core/version.hpp"


namespace drpc {
namespace {

constexpr size_t kMaxHead = 8192;
constexpr size_t kMaxLine = 1024;
constexpr int kMaxRetryAfterS = 86400;

char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

template <typename T>
bool parse_number(std::string_view s, T& out, int base = 10) {
    if (s.empty()) return false;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
    return ec == std::errc{} && end == s.data() + s.size();
}

}  // namespace

std::string build_request(std::string_view method, const RelayUrl& url, std::string_view path,
                          std::string_view bearer, std::string_view content_type, size_t body_size) {
    std::string r;
    r.append(method).append(" ").append(url.base_path).append(path).append(" HTTP/1.1\r\n");
    r.append("Host: ").append(url.host);
    if (url.port != 443) r.append(":").append(std::to_string(url.port));
    r.append("\r\nUser-Agent: nx-discord-rpc/").append(kVersion).append("\r\n");
    if (!bearer.empty()) r.append("Authorization: Bearer ").append(bearer).append("\r\n");
    if (!content_type.empty()) r.append("Content-Type: ").append(content_type).append("\r\n");
    r.append("Content-Length: ").append(std::to_string(body_size)).append("\r\n");
    r.append("Connection: close\r\n\r\n");
    return r;
}

bool is_transient_http_status(int status) {
    return status == 429 || (status >= 500 && status <= 599);
}

void HttpResponseParser::feed(std::string_view chunk) {
    for (char c : chunk) {
        if (state_ == State::Done || state_ == State::Failed) return;
        step(c);
    }
}

HttpResponse HttpResponseParser::take() {
    HttpResponse r = std::move(response_);
    response_ = {};
    return r;
}

// Collects one CRLF-terminated line into line_; true once it is complete.
bool HttpResponseParser::line_byte(char c) {
    if (c == '\n') {
        if (!line_.empty() && line_.back() == '\r') line_.pop_back();
        return true;
    }
    if (line_.size() >= kMaxLine) {
        state_ = State::Failed;
        return false;
    }
    line_.push_back(c);
    return false;
}

void HttpResponseParser::step(char c) {
    switch (state_) {
        case State::Head:
            if (head_.size() >= kMaxHead) {
                state_ = State::Failed;
                return;
            }
            head_.push_back(c);
            if (head_.ends_with("\r\n\r\n")) parse_head();
            return;
        case State::Body:
            append_body(c);
            if (state_ == State::Body && --remaining_ == 0) state_ = State::Done;
            return;
        case State::ChunkSize:
            if (line_byte(c)) parse_chunk_size();
            return;
        case State::ChunkData:
            append_body(c);
            if (state_ == State::ChunkData && --remaining_ == 0) state_ = State::ChunkDataEnd;
            return;
        case State::ChunkDataEnd:
            if (c == '\n') {
                state_ = State::ChunkSize;
                line_.clear();
            } else if (c != '\r' || !line_.empty()) {
                state_ = State::Failed;
            } else {
                line_.push_back(c);
            }
            return;
        case State::Trailer:
            if (line_byte(c)) {
                if (line_.empty()) state_ = State::Done;
                line_.clear();
            }
            return;
        case State::Done:
        case State::Failed:
            return;
    }
}

void HttpResponseParser::parse_head() {
    std::string_view head(head_);
    size_t eol = head.find("\r\n");
    const std::string_view status_line = head.substr(0, eol);
    if (!status_line.starts_with("HTTP/1.") || status_line.size() < 12 || status_line[8] != ' ' ||
        !parse_number(status_line.substr(9, 3), response_.status) ||
        (status_line.size() > 12 && status_line[12] != ' ')) {
        state_ = State::Failed;
        return;
    }

    bool chunked = false;
    std::optional<size_t> length;
    for (size_t pos = eol + 2; pos < head.size();) {
        eol = head.find("\r\n", pos);
        const std::string_view line = head.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.empty()) break;
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view name = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (iequals(name, "transfer-encoding")) {
            chunked = iequals(value, "chunked");
        } else if (iequals(name, "retry-after")) {
            int n = 0;
            if (parse_number(value, n) && n >= 0 && n <= kMaxRetryAfterS) response_.retry_after_s = n;
        } else if (iequals(name, "content-length")) {
            size_t n = 0;
            if (!parse_number(value, n)) {
                state_ = State::Failed;
                return;
            }
            length = n;
        }
    }
    head_.clear();
    head_.shrink_to_fit();

    if (chunked) {
        state_ = State::ChunkSize;
    } else if (length) {
        if (*length > kHttpMaxBody) {
            state_ = State::Failed;
        } else {
            remaining_ = *length;
            state_ = remaining_ == 0 ? State::Done : State::Body;
        }
    } else {
        // Only bodiless responses may omit a length; reading until close is not supported.
        const int s = response_.status;
        state_ = s == 204 || s == 304 || (s >= 100 && s < 200) ? State::Done : State::Failed;
    }
}

void HttpResponseParser::parse_chunk_size() {
    std::string_view line(line_);
    line = trim(line.substr(0, line.find(';')));
    size_t n = 0;
    if (!parse_number(line, n, 16)) {
        state_ = State::Failed;
    } else if (n == 0) {
        state_ = State::Trailer;
    } else if (n > kHttpMaxBody - response_.body.size()) {
        state_ = State::Failed;
    } else {
        remaining_ = n;
        state_ = State::ChunkData;
    }
    line_.clear();
}

void HttpResponseParser::append_body(char c) {
    if (response_.body.size() >= kHttpMaxBody) {
        state_ = State::Failed;
        return;
    }
    response_.body.push_back(c);
}

}  // namespace drpc
