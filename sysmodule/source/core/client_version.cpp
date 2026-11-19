#include "core/client_version.hpp"

#include "core/json_extract.hpp"

namespace drpc {

namespace {

// X.Y.Z: three non-empty runs of digits.
bool valid_version(std::string_view v) {
    if (v.empty() || v.size() > kMinVersionMaxLen) return false;
    int dots = 0;
    char prev = '.';
    for (char c : v) {
        if (c == '.') {
            if (prev == '.') return false;
            ++dots;
        } else if (c < '0' || c > '9') {
            return false;
        }
        prev = c;
    }
    return dots == 2 && prev != '.';
}

}  // namespace

std::string parse_min_version(std::string_view body) {
    // Longer than kMinVersionMaxLen on purpose: a cut value must still be rejected as too long.
    json::Extractor e({"min_version"}, 32);
    e.feed(body);
    if (!e.finish()) return {};
    const json::Capture* c = e.find("min_version");
    if (!c || !c->is_string || !valid_version(c->value)) return {};
    return c->value;
}

}  // namespace drpc
