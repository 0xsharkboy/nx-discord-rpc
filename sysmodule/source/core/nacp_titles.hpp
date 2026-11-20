#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace drpc {

// control.nacp title entries: name (0x200) then publisher (0x100), UTF-8, not always NUL-terminated.
inline constexpr size_t kNacpTitleEntrySize = 0x300;
inline constexpr size_t kNacpTitleNameSize = 0x200;
// The title block at offset 0 holds 16 plain entries...
inline constexpr size_t kNacpLegacyTitleCount = 16;
inline constexpr size_t kNacpTitleBlockSize = kNacpLegacyTitleCount * kNacpTitleEntrySize;
// ...unless [21.0.0+] TitlesDataFormat is 1: then it is a little-endian u16 compressed size followed by
// raw DEFLATE data that expands to up to 32 entries (Polish and Thai are 16 and 17).
inline constexpr size_t kNacpTitleCount = 32;
inline constexpr size_t kNacpTitlesDataFormatOffset = 0x3215;

// Returns the 32 title entries of a raw control.nacp (missing ones zeroed) and rewrites its title block
// with the first 16 plain entries, so libnx language helpers that index NacpStruct read real names.
// Unknown format or corrupt data: every entry is empty and the title block is zeroed, so the raw
// compressed bytes never show up as a name.
std::vector<uint8_t> unpack_nacp_titles(std::span<uint8_t> nacp);

// Name of entry `index` in a run of title entries (empty when out of range).
std::string_view nacp_title_name(std::span<const uint8_t> entries, size_t index);

}  // namespace drpc
