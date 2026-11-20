#include "core/title_name.hpp"

namespace drpc {

std::string choose_title_name(std::string_view preferred, std::span<const std::string_view> entries) {
    if (!preferred.empty()) return std::string{preferred};
    for (std::string_view e : entries) {
        if (!e.empty()) return std::string{e};
    }
    return std::string{kUnknownGameName};
}

std::string english_title_name(std::span<const std::string_view> entries) {
    for (size_t i = 0; i < entries.size() && i < 2; ++i) {
        if (!entries[i].empty()) return std::string{entries[i]};
    }
    return {};
}

std::optional<uint64_t> parent_application_id(uint64_t program_id) {
    const uint64_t parent = program_id & ~uint64_t{0xFF};
    if (parent == program_id) return std::nullopt;
    return parent;
}

}  // namespace drpc
