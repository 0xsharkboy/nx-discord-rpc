#include "core/json_extract.hpp"

#include <cstring>

namespace drpc::json {

namespace {

constexpr size_t kMaxDepth = 64;
constexpr size_t kMaxKeyLen = 256;
constexpr uint32_t kReplacement = 0xFFFD;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool is_number_char(char c) {
    return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
}

const char* keyword_for(char first) {
    switch (first) {
        case 't': return "true";
        case 'f': return "false";
        case 'n': return "null";
        default: return nullptr;
    }
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool pattern_matches(std::string_view pattern, std::string_view path) {
    size_t p = 0, q = 0;
    while (p < pattern.size() && q < path.size()) {
        if (pattern.compare(p, 2, "[]") == 0 && path[q] == '[') {
            size_t close = path.find(']', q);
            if (close == std::string_view::npos) return false;
            p += 2;
            q = close + 1;
        } else if (pattern[p] == path[q]) {
            ++p;
            ++q;
        } else {
            return false;
        }
    }
    return p == pattern.size() && q == path.size();
}

}  // namespace

Extractor::Extractor(std::vector<std::string> patterns, size_t max_value_len)
    : patterns_(std::move(patterns)), max_value_len_(max_value_len) {}

void Extractor::reset() {
    state_ = State::ExpectValue;
    stack_.clear();
    captures_.clear();
    path_.clear();
    value_.clear();
    key_.clear();
    string_is_key_ = false;
    capturing_ = false;
    overflow_ = false;
    hex_ = 0;
    hex_count_ = 0;
    pending_high_ = 0;
    literal_first_ = 0;
    literal_len_ = 0;
}

const Capture* Extractor::find(std::string_view path) const {
    for (const Capture& c : captures_)
        if (c.path == path) return &c;
    return nullptr;
}

void Extractor::feed(std::string_view chunk) {
    for (char c : chunk) {
        if (state_ == State::Error) return;
        step(c);
    }
}

bool Extractor::finish() {
    if (state_ == State::InLiteral && stack_.empty() && end_literal()) state_ = State::Done;
    return state_ == State::Done;
}

void Extractor::step(char c) {
    switch (state_) {
        case State::InString:
        case State::InEscape:
        case State::InHex:
            string_byte(c);
            return;
        case State::InLiteral:
            if (is_number_char(c) || (c >= 'a' && c <= 'z')) {
                literal_byte(c);
                return;
            }
            if (!end_literal()) return;
            end_value();
            step(c);
            return;
        case State::Done:
            if (!is_space(c)) fail();
            return;
        case State::Error:
            return;
        default:
            break;
    }

    if (is_space(c)) return;

    switch (state_) {
        case State::ExpectValueOrEnd:
            if (c == ']') {
                close_container();
                return;
            }
            [[fallthrough]];
        case State::ExpectValue:
            begin_value(c);
            return;
        case State::ExpectKeyOrEnd:
            if (c == '}') {
                close_container();
                return;
            }
            [[fallthrough]];
        case State::ExpectKey:
            if (c == '"') begin_string(true);
            else fail();
            return;
        case State::ExpectColon:
            if (c == ':') state_ = State::ExpectValue;
            else fail();
            return;
        case State::AfterValue: {
            Frame& top = stack_.back();
            if (c == ',') {
                if (top.is_array) {
                    ++top.index;
                    state_ = State::ExpectValue;
                } else {
                    state_ = State::ExpectKey;
                }
            } else if ((c == ']' && top.is_array) || (c == '}' && !top.is_array)) {
                close_container();
            } else {
                fail();
            }
            return;
        }
        default:
            fail();
    }
}

void Extractor::begin_value(char c) {
    if (c == '{' || c == '[') {
        if (stack_.size() >= kMaxDepth) return fail();
        stack_.push_back({c == '[', 0, {}});
        state_ = c == '[' ? State::ExpectValueOrEnd : State::ExpectKeyOrEnd;
    } else if (c == '"') {
        begin_string(false);
    } else if (c == '-' || (c >= '0' && c <= '9') || keyword_for(c)) {
        build_path();
        capturing_ = path_matches();
        overflow_ = false;
        value_.clear();
        literal_first_ = c;
        literal_len_ = 0;
        state_ = State::InLiteral;
        literal_byte(c);
    } else {
        fail();
    }
}

void Extractor::begin_string(bool is_key) {
    string_is_key_ = is_key;
    overflow_ = false;
    pending_high_ = 0;
    if (is_key) {
        key_.clear();
        capturing_ = false;
    } else {
        build_path();
        capturing_ = path_matches();
        value_.clear();
    }
    state_ = State::InString;
}

void Extractor::sink(char c) {
    if (string_is_key_) {
        if (key_.size() < kMaxKeyLen) key_.push_back(c);
        else overflow_ = true;
    } else if (capturing_) {
        if (value_.size() < max_value_len_) value_.push_back(c);
        else overflow_ = true;
    }
}

void Extractor::emit_codepoint(uint32_t cp) {
    if (cp < 0x80) {
        sink(static_cast<char>(cp));
    } else if (cp < 0x800) {
        sink(static_cast<char>(0xC0 | (cp >> 6)));
        sink(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sink(static_cast<char>(0xE0 | (cp >> 12)));
        sink(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        sink(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        sink(static_cast<char>(0xF0 | (cp >> 18)));
        sink(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        sink(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        sink(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void Extractor::flush_pending_high() {
    if (pending_high_) {
        pending_high_ = 0;
        emit_codepoint(kReplacement);
    }
}

void Extractor::string_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    switch (state_) {
        case State::InString:
            if (c == '"') {
                flush_pending_high();
                end_string();
            } else if (c == '\\') {
                state_ = State::InEscape;
            } else if (u < 0x20) {
                fail();
            } else {
                flush_pending_high();
                sink(c);
            }
            return;
        case State::InEscape:
            if (c == 'u') {
                hex_ = 0;
                hex_count_ = 0;
                state_ = State::InHex;
                return;
            }
            flush_pending_high();
            state_ = State::InString;
            switch (c) {
                case '"': sink('"'); break;
                case '\\': sink('\\'); break;
                case '/': sink('/'); break;
                case 'b': sink('\b'); break;
                case 'f': sink('\f'); break;
                case 'n': sink('\n'); break;
                case 'r': sink('\r'); break;
                case 't': sink('\t'); break;
                default: fail();
            }
            return;
        default: {
            int d = hex_digit(c);
            if (d < 0) return fail();
            hex_ = (hex_ << 4) | static_cast<uint32_t>(d);
            if (++hex_count_ < 4) return;
            state_ = State::InString;
            const bool high = hex_ >= 0xD800 && hex_ <= 0xDBFF;
            const bool low = hex_ >= 0xDC00 && hex_ <= 0xDFFF;
            if (pending_high_ && low) {
                emit_codepoint(0x10000 + ((pending_high_ - 0xD800) << 10) + (hex_ - 0xDC00));
                pending_high_ = 0;
                return;
            }
            flush_pending_high();
            if (high) pending_high_ = hex_;
            else emit_codepoint(low ? kReplacement : hex_);
        }
    }
}

void Extractor::end_string() {
    if (string_is_key_) {
        stack_.back().key = overflow_ ? std::string("\x01") : key_;
        state_ = State::ExpectColon;
        return;
    }
    capture(true);
    end_value();
}

void Extractor::capture(bool is_string) {
    if (capturing_ && !overflow_ && captures_.size() < kMaxCaptures) captures_.push_back({path_, value_, is_string});
}

void Extractor::literal_byte(char c) {
    const char* kw = keyword_for(literal_first_);
    bool ok;
    if (kw) ok = literal_len_ < std::strlen(kw) && kw[literal_len_] == c;
    else ok = is_number_char(c);
    if (!ok) return fail();
    ++literal_len_;
    if (capturing_) {
        if (value_.size() < max_value_len_) value_.push_back(c);
        else overflow_ = true;
    }
}

bool Extractor::end_literal() {
    const char* kw = keyword_for(literal_first_);
    if (kw && literal_len_ != std::strlen(kw)) {
        fail();
        return false;
    }
    capture(false);
    return true;
}

void Extractor::end_value() { state_ = stack_.empty() ? State::Done : State::AfterValue; }

void Extractor::close_container() {
    stack_.pop_back();
    end_value();
}

void Extractor::build_path() {
    path_.clear();
    for (const Frame& f : stack_) {
        if (f.is_array) {
            path_ += '[';
            path_ += std::to_string(f.index);
            path_ += ']';
        } else {
            if (!path_.empty()) path_ += '.';
            path_ += f.key;
        }
    }
}

bool Extractor::path_matches() const {
    for (const std::string& p : patterns_)
        if (pattern_matches(p, path_)) return true;
    return false;
}

}  // namespace drpc::json
