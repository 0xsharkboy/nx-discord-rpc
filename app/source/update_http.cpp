#include "update_http.hpp"

#include <array>
#include <charconv>

namespace app::http {

namespace {

// GitHub heads are large (security and rate limit headers) and signed Locations run to a few KB.
constexpr size_t kMaxHead = 32 * 1024;
constexpr size_t kMaxLine = 1024;
constexpr size_t kMaxLocation = 8 * 1024;

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

bool ends_with_label(std::string_view host, std::string_view suffix) {
    // ".github.com" needs at least one label before it.
    return host.size() > suffix.size() && host.ends_with(suffix);
}

bool is_redirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }

std::string request_head(const Url& url, const Options& o) {
    std::string r;
    r.append("GET ").append(url.target).append(" HTTP/1.1\r\n");
    r.append("Host: ").append(url.host).append("\r\n");
    r.append("User-Agent: ").append(o.user_agent).append("\r\n");
    r.append("Accept: ").append(o.accept).append("\r\n");
    r.append("Connection: close\r\n\r\n");
    return r;
}

}  // namespace

std::optional<Url> parse_url(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() < kScheme.size() || !iequals(url.substr(0, kScheme.size()), kScheme)) return std::nullopt;
    url.remove_prefix(kScheme.size());
    for (char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f || c == '#' || c == '\\') return std::nullopt;
    const size_t slash = url.find_first_of("/?");
    std::string_view authority = url.substr(0, slash);
    std::string target = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    if (target.front() == '?') target.insert(target.begin(), '/');
    if (authority.find('@') != std::string_view::npos) return std::nullopt;
    if (const size_t colon = authority.find(':'); colon != std::string_view::npos) {
        if (authority.substr(colon + 1) != "443") return std::nullopt;
        authority = authority.substr(0, colon);
    }
    if (authority.empty() || authority.size() > 253) return std::nullopt;
    Url out;
    for (char c : authority) {
        const char l = lower(c);
        if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '-' || l == '.')) return std::nullopt;
        out.host.push_back(l);
    }
    out.target = std::move(target);
    return out;
}

bool allowed_host(std::string_view host) {
    return host == "github.com" || host == "api.github.com" || ends_with_label(host, ".github.com") ||
           ends_with_label(host, ".githubusercontent.com");
}

void StreamParser::fail(Error e) {
    state_ = State::Failed;
    if (error_ == Error::None) error_ = e;
}

void StreamParser::feed(std::span<const uint8_t> data) {
    const uint8_t* p = data.data();
    size_t n = data.size();
    while (n > 0 && state_ != State::Done && state_ != State::Failed) {
        switch (state_) {
            case State::Head: {
                const char c = char(*p++);
                --n;
                if (head_.size() >= kMaxHead) return fail(Error::BadResponse);
                head_.push_back(c);
                if (head_.ends_with("\r\n\r\n")) parse_head();
                break;
            }
            case State::Body:
            case State::ChunkData: {
                const size_t take = std::min(n, remaining_);
                if (!body_bytes(p, take)) return;
                p += take;
                n -= take;
                remaining_ -= take;
                if (remaining_ == 0) state_ = state_ == State::Body ? State::Done : State::ChunkDataEnd;
                break;
            }
            case State::UntilClose: {
                if (!body_bytes(p, n)) return;
                n = 0;
                break;
            }
            case State::ChunkSize: {
                const char c = char(*p++);
                --n;
                if (line_byte(c)) parse_chunk_size();
                break;
            }
            case State::ChunkDataEnd: {
                const char c = char(*p++);
                --n;
                if (c == '\n') {
                    state_ = State::ChunkSize;
                    line_.clear();
                } else if (c != '\r' || !line_.empty()) {
                    fail(Error::BadResponse);
                } else {
                    line_.push_back(c);
                }
                break;
            }
            case State::Trailer: {
                const char c = char(*p++);
                --n;
                if (line_byte(c)) {
                    if (line_.empty()) state_ = State::Done;
                    line_.clear();
                }
                break;
            }
            case State::Done:
            case State::Failed:
                break;
        }
    }
}

void StreamParser::closed() {
    if (state_ == State::UntilClose) state_ = State::Done;
}

bool StreamParser::line_byte(char c) {
    if (c == '\n') {
        if (!line_.empty() && line_.back() == '\r') line_.pop_back();
        return true;
    }
    if (line_.size() >= kMaxLine) {
        fail(Error::BadResponse);
        return false;
    }
    line_.push_back(c);
    return false;
}

void StreamParser::parse_head() {
    std::string_view head(head_);
    size_t eol = head.find("\r\n");
    const std::string_view status_line = head.substr(0, eol);
    if (!status_line.starts_with("HTTP/1.") || status_line.size() < 12 || status_line[8] != ' ' ||
        !parse_number(status_line.substr(9, 3), status_) || (status_line.size() > 12 && status_line[12] != ' ')) {
        return fail(Error::BadResponse);
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
        } else if (iequals(name, "content-length")) {
            size_t v = 0;
            if (!parse_number(value, v)) return fail(Error::BadResponse);
            length = v;
        } else if (iequals(name, "location")) {
            if (value.size() > kMaxLocation) return fail(Error::BadRedirect);
            location_ = std::string(value);
        }
    }
    head_.clear();
    head_.shrink_to_fit();
    head_done_ = true;
    if (status_ != 200) discard_ = true;

    const int s = status_;
    if (s == 204 || s == 304 || (s >= 100 && s < 200)) {
        state_ = State::Done;
    } else if (chunked) {
        state_ = State::ChunkSize;
    } else if (length) {
        if (!discard_ && *length > max_body_) return fail(Error::TooLarge);
        remaining_ = *length;
        state_ = remaining_ == 0 ? State::Done : State::Body;
    } else {
        state_ = State::UntilClose;
    }
}

void StreamParser::parse_chunk_size() {
    std::string_view line(line_);
    line = trim(line.substr(0, line.find(';')));
    size_t v = 0;
    if (!parse_number(line, v, 16)) {
        fail(Error::BadResponse);
    } else if (v == 0) {
        state_ = State::Trailer;
    } else if (!discard_ && v > max_body_ - bytes_) {
        fail(Error::TooLarge);
    } else {
        remaining_ = v;
        state_ = State::ChunkData;
    }
    line_.clear();
}

bool StreamParser::body_bytes(const uint8_t* p, size_t n) {
    if (discard_) return true;
    if (n > max_body_ - bytes_) {
        fail(Error::TooLarge);
        return false;
    }
    bytes_ += n;
    if (n > 0 && sink_ && !sink_({p, n})) {
        fail(Error::Aborted);
        return false;
    }
    return true;
}

Result get(drpc::Transport& t, std::string_view first_url, const Options& options, const Sink& sink) {
    Result result;
    std::optional<Url> url = parse_url(first_url);
    if (!url || !allowed_host(url->host)) {
        result.error = Error::Refused;
        return result;
    }
    for (int hop = 0;; ++hop) {
        StreamParser parser(options.max_body, sink);
        bool connected = t.connect(url->host, 443);
        if (connected) {
            const std::string head = request_head(*url, options);
            connected = t.write_all({reinterpret_cast<const uint8_t*>(head.data()), head.size()});
        }
        if (!connected) {
            t.close();
            result.error = Error::Network;
            return result;
        }
        std::array<uint8_t, 4096> buf;
        bool eof = false;
        while (!parser.done() && parser.error() == Error::None) {
            const int n = t.read(buf, options.read_timeout_ms);
            if (n < 0) {
                eof = true;
                break;
            }
            if (n == 0) break;  // timed out
            parser.feed({buf.data(), size_t(n)});
            // A redirect is decided by its head alone: its body is never read.
            if (parser.head_done() && is_redirect(parser.status())) break;
        }
        if (eof) parser.closed();
        t.close();

        result.status = parser.status();
        result.bytes = parser.bytes();
        if (parser.head_done() && is_redirect(parser.status())) {
            if (hop >= kMaxRedirects) {
                result.error = Error::TooManyRedirects;
                return result;
            }
            url = parse_url(parser.location());
            if (!url || !allowed_host(url->host)) {
                result.error = Error::BadRedirect;
                return result;
            }
            continue;
        }
        if (parser.error() != Error::None) {
            result.error = parser.error();
        } else if (!parser.done()) {
            result.error = Error::Network;  // closed early or timed out
        } else if (parser.status() != 200) {
            result.error = Error::Status;
        }
        return result;
    }
}

const char* error_name(Error e) {
    switch (e) {
        case Error::None:
            return "ok";
        case Error::Network:
            return "network error";
        case Error::BadResponse:
            return "invalid response";
        case Error::Status:
            return "HTTP error";
        case Error::TooLarge:
            return "too large";
        case Error::BadRedirect:
            return "redirect refused";
        case Error::TooManyRedirects:
            return "too many redirects";
        case Error::Refused:
            return "URL refused";
        case Error::Aborted:
            return "stopped";
    }
    return "?";
}

}  // namespace app::http
