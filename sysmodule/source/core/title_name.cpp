#include "core/title_name.hpp"

namespace drpc {

std::string choose_title_name(std::string_view preferred, std::span<const std::string_view> entries) {
    if (!preferred.empty()) return std::string{preferred};
    for (std::string_view e : entries) {
        if (!e.empty()) return std::string{e};
    }
    return std::string{kUnknownGameName};
}

}  // namespace drpc
