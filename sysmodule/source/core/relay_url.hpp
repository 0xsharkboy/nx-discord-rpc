#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace drpc {

struct RelayUrl {
    std::string host;
    uint16_t port = 443;
    std::string base_path;  // "" or "/x/y", never a trailing '/'
};

// https only; rejects userinfo, query, fragment, whitespace and control characters.
std::optional<RelayUrl> parse_relay_url(std::string_view url);

}  // namespace drpc
