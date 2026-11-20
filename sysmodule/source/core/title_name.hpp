#pragma once

#include <span>
#include <string>
#include <string_view>

namespace drpc {

inline constexpr std::string_view kUnknownGameName = "Unknown game";

// preferred: the NACP entry for the console language; entries: every NACP language entry.
std::string choose_title_name(std::string_view preferred, std::span<const std::string_view> entries);

// entries in NACP order: AmericanEnglish (0), else BritishEnglish (1), else "".
std::string english_title_name(std::span<const std::string_view> entries);

}  // namespace drpc
