#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace drpc::json {

class Writer {
public:
    Writer& begin_object();
    Writer& end_object();
    Writer& begin_array();
    Writer& end_array();
    Writer& key(std::string_view k);
    Writer& value(std::string_view s);
    Writer& value(const char* s) { return value(std::string_view{s}); }
    Writer& value(int64_t n);
    Writer& value(bool b);
    Writer& null();
    const std::string& str() const { return out_; }

private:
    void before_value();

    std::string out_;
    std::vector<bool> needs_comma_;
    bool after_key_ = false;
};

void append_escaped(std::string& out, std::string_view s);

}  // namespace drpc::json
