#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace drpc::json {

struct Capture {
    std::string path;
    std::string value;
    bool is_string;
};

// Single-pass, byte-at-a-time JSON reader that keeps only scalars whose path
// matches a pattern ("." between keys, "[]" for any array index). Memory is
// bounded by nesting depth, key length and kMaxCaptures values, never by document size.
class Extractor {
public:
    // Matches past this many are dropped.
    static constexpr size_t kMaxCaptures = 64;

    Extractor(std::vector<std::string> patterns, size_t max_value_len = 512);

    void reset();
    void feed(std::string_view chunk);
    bool finish();

    const std::vector<Capture>& captures() const { return captures_; }
    const Capture* find(std::string_view path) const;

private:
    enum class State : uint8_t {
        ExpectValue,
        ExpectValueOrEnd,
        ExpectKeyOrEnd,
        ExpectKey,
        ExpectColon,
        AfterValue,
        InString,
        InEscape,
        InHex,
        InLiteral,
        Done,
        Error,
    };

    struct Frame {
        bool is_array;
        size_t index;
        std::string key;
    };

    void step(char c);
    void begin_value(char c);
    void begin_string(bool is_key);
    void string_byte(char c);
    void emit_codepoint(uint32_t cp);
    void flush_pending_high();
    void sink(char c);
    void end_string();
    void literal_byte(char c);
    bool end_literal();
    void end_value();
    void capture(bool is_string);
    void close_container();
    void build_path();
    bool path_matches() const;
    void fail() { state_ = State::Error; }

    std::vector<std::string> patterns_;
    size_t max_value_len_;

    State state_ = State::ExpectValue;
    std::vector<Frame> stack_;
    std::vector<Capture> captures_;

    std::string path_;
    std::string value_;
    std::string key_;
    bool string_is_key_ = false;
    bool capturing_ = false;
    bool overflow_ = false;

    uint32_t hex_ = 0;
    int hex_count_ = 0;
    uint32_t pending_high_ = 0;

    char literal_first_ = 0;
    size_t literal_len_ = 0;
};

}  // namespace drpc::json
