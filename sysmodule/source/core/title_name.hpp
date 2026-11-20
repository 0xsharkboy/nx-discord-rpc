#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace drpc {

inline constexpr std::string_view kUnknownGameName = "Unknown game";

// preferred: the NACP entry for the console language; entries: every NACP language entry.
std::string choose_title_name(std::string_view preferred, std::span<const std::string_view> entries);

// entries in NACP order: AmericanEnglish (0), else BritishEnglish (1), else "".
std::string english_title_name(std::span<const std::string_view> entries);

// A game can start extra programs whose id is its application id plus a program index (0 to 0xFF), for
// example Sonic Generations inside Sonic X Shadow Generations. They have no control data of their own:
// returns the parent application id, nullopt when program_id already is one.
std::optional<uint64_t> parent_application_id(uint64_t program_id);

}  // namespace drpc
