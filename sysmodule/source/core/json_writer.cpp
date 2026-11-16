#include "core/json_writer.hpp"

namespace drpc::json {

void append_escaped(std::string& out, std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out += kHex[(c >> 4) & 0xF];
                    out += kHex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void Writer::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (needs_comma_.empty()) return;
    if (needs_comma_.back()) out_ += ',';
    needs_comma_.back() = true;
}

Writer& Writer::begin_object() {
    before_value();
    out_ += '{';
    needs_comma_.push_back(false);
    return *this;
}

Writer& Writer::end_object() {
    out_ += '}';
    needs_comma_.pop_back();
    return *this;
}

Writer& Writer::begin_array() {
    before_value();
    out_ += '[';
    needs_comma_.push_back(false);
    return *this;
}

Writer& Writer::end_array() {
    out_ += ']';
    needs_comma_.pop_back();
    return *this;
}

Writer& Writer::key(std::string_view k) {
    before_value();
    append_escaped(out_, k);
    out_ += ':';
    after_key_ = true;
    return *this;
}

Writer& Writer::value(std::string_view s) {
    before_value();
    append_escaped(out_, s);
    return *this;
}

Writer& Writer::value(int64_t n) {
    before_value();
    out_ += std::to_string(n);
    return *this;
}

Writer& Writer::value(bool b) {
    before_value();
    out_ += b ? "true" : "false";
    return *this;
}

Writer& Writer::null() {
    before_value();
    out_ += "null";
    return *this;
}

}  // namespace drpc::json
