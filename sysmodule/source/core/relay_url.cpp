#include "core/relay_url.hpp"

#include <charconv>

namespace drpc {
namespace {

constexpr std::string_view kScheme = "https://";
constexpr size_t kMaxHost = 253;

bool is_alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_host_char(char c) { return is_alnum(c) || c == '.' || c == '-'; }
bool is_path_char(char c) { return is_alnum(c) || c == '.' || c == '_' || c == '~' || c == '/' || c == '-'; }

bool starts_with_icase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const char c = s[i] >= 'A' && s[i] <= 'Z' ? char(s[i] - 'A' + 'a') : s[i];
        if (c != prefix[i]) return false;
    }
    return true;
}

}  // namespace

std::optional<RelayUrl> parse_relay_url(std::string_view url) {
    if (!starts_with_icase(url, kScheme)) return std::nullopt;
    url.remove_prefix(kScheme.size());

    const size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    std::string_view path = slash == std::string_view::npos ? std::string_view{} : url.substr(slash);

    RelayUrl out;
    const size_t colon = authority.find(':');
    const std::string_view host = authority.substr(0, colon);
    if (host.empty() || host.size() > kMaxHost) return std::nullopt;
    for (char c : host)
        if (!is_host_char(c)) return std::nullopt;
    out.host = std::string(host);

    if (colon != std::string_view::npos) {
        const std::string_view port = authority.substr(colon + 1);
        unsigned value = 0;
        if (port.empty() || port.size() > 5) return std::nullopt;
        const auto [end, ec] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (ec != std::errc{} || end != port.data() + port.size() || value < 1 || value > 65535)
            return std::nullopt;
        out.port = static_cast<uint16_t>(value);
    }

    for (char c : path)
        if (!is_path_char(c)) return std::nullopt;
    while (!path.empty() && path.back() == '/') path.remove_suffix(1);
    out.base_path = std::string(path);
    return out;
}

}  // namespace drpc
