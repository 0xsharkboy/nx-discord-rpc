#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "core/nacp_titles.hpp"

using namespace std::string_view_literals;

namespace {

constexpr size_t kNacpSize = 0x4000;

// 32 title entries, raw DEFLATE (Python: zlib.compressobj(9, zlib.DEFLATED, -15)). Names at index
// 0 (AmericanEnglish), 2 (Japanese), 16 (Polish) and 17 (Thai); every publisher is "Nintendo".
constexpr uint8_t kCompressedTitles[] = {
    0xed, 0xda, 0x31, 0x4a, 0x04, 0x31, 0x14, 0x06, 0x60, 0x8f, 0x92, 0x73, 0x58, 0x5a, 0x8b, 0x58,
    0x2c, 0x08, 0x76, 0x0b, 0x13, 0xdd, 0xc1, 0x61, 0x07, 0x34, 0x16, 0x76, 0x3a, 0x63, 0xe3, 0x05,
    0xb4, 0xb2, 0x59, 0xac, 0xd6, 0x45, 0x3b, 0xd9, 0x85, 0x6d, 0xbd, 0xc7, 0x40, 0x2c, 0x3c, 0x86,
    0xd1, 0x4b, 0x28, 0xce, 0xf7, 0x35, 0x21, 0x2f, 0xa9, 0xfe, 0xf7, 0x48, 0x95, 0xc9, 0x2c, 0x86,
    0xfd, 0x78, 0x1a, 0xe7, 0x55, 0x68, 0x4f, 0xc2, 0x71, 0x6c, 0xaa, 0xe9, 0x6e, 0xd8, 0x3b, 0x8f,
    0xd3, 0x34, 0xfb, 0x2e, 0xa4, 0x72, 0x7c, 0x54, 0x37, 0xd5, 0x0e, 0xff, 0xd3, 0x41, 0x3d, 0x4f,
    0xa5, 0xf9, 0xad, 0x24, 0x60, 0x7c, 0x72, 0xb7, 0xcd, 0xfd, 0x73, 0xee, 0xaf, 0xf3, 0xcd, 0xcb,
    0xc7, 0xf6, 0xf1, 0x73, 0xb9, 0x0a, 0xb9, 0xbf, 0xcf, 0xfd, 0x2a, 0x77, 0x9b, 0x90, 0xbb, 0x65,
    0xd9, 0x94, 0xe5, 0xad, 0x14, 0x5f, 0x73, 0xf7, 0xf4, 0x73, 0xf5, 0x4e, 0x6a, 0xde, 0x7f, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xf7, 0x1c, 0xb6, 0xcd, 0xc5, 0x59, 0x1d,
    0xd2, 0x55, 0xba, 0x7c, 0xef, 0xc4, 0x31, 0x3a, 0x63, 0xff, 0xff, 0x39, 0x6c, 0x6e, 0x87, 0xf5,
    0xc3, 0xb0, 0x5e, 0x18, 0x05, 0xf3, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7f, 0xdc, 0x17,
};

constexpr auto kZelda = "The Legend of Zelda: Breath of the Wild"sv;

std::vector<uint8_t> compressed_nacp(uint16_t declared_size) {
    std::vector<uint8_t> nacp(kNacpSize, 0);
    nacp[0] = static_cast<uint8_t>(declared_size & 0xFF);
    nacp[1] = static_cast<uint8_t>(declared_size >> 8);
    std::memcpy(nacp.data() + 2, kCompressedTitles, sizeof kCompressedTitles);
    nacp[drpc::kNacpTitlesDataFormatOffset] = 1;
    return nacp;
}

bool title_block_is_zero(const std::vector<uint8_t>& nacp) {
    for (size_t i = 0; i < drpc::kNacpTitleBlockSize; ++i)
        if (nacp[i] != 0) return false;
    return true;
}

void check_no_names(const std::vector<uint8_t>& titles) {
    REQUIRE(titles.size() == drpc::kNacpTitleCount * drpc::kNacpTitleEntrySize);
    for (size_t i = 0; i < drpc::kNacpTitleCount; ++i) CHECK(drpc::nacp_title_name(titles, i).empty());
}

}  // namespace

TEST_CASE("plain NACP titles are read as they are") {
    std::vector<uint8_t> nacp(kNacpSize, 0);
    std::memcpy(nacp.data() + 3 * drpc::kNacpTitleEntrySize, "Mario Kart 8 Deluxe", 19);
    const auto titles = drpc::unpack_nacp_titles(nacp);
    REQUIRE(titles.size() == drpc::kNacpTitleCount * drpc::kNacpTitleEntrySize);
    CHECK(drpc::nacp_title_name(titles, 3) == "Mario Kart 8 Deluxe");
    CHECK(drpc::nacp_title_name(titles, 0).empty());
    CHECK(drpc::nacp_title_name(titles, 16).empty());
    CHECK(drpc::nacp_title_name(nacp, 3) == "Mario Kart 8 Deluxe");
}

TEST_CASE("compressed NACP titles are inflated, including the languages past 16") {
    auto nacp = compressed_nacp(sizeof kCompressedTitles);
    const auto titles = drpc::unpack_nacp_titles(nacp);
    CHECK(drpc::nacp_title_name(titles, 0) == kZelda);
    CHECK(drpc::nacp_title_name(titles, 2) == "ゼルダの伝説 ブレス オブ ザ ワイルド");
    CHECK(drpc::nacp_title_name(titles, 16) == "Polski tytuł");
    CHECK(drpc::nacp_title_name(titles, 17) == "ไทย");
    CHECK(drpc::nacp_title_name(titles, 1).empty());
}

TEST_CASE("compressed NACP title block is rewritten with the first 16 plain entries") {
    auto nacp = compressed_nacp(sizeof kCompressedTitles);
    drpc::unpack_nacp_titles(nacp);
    CHECK(drpc::nacp_title_name(nacp, 0) == kZelda);
    CHECK(drpc::nacp_title_name(nacp, 1).empty());
    const std::string_view publisher{reinterpret_cast<const char*>(nacp.data()) + 0x200, 8};
    CHECK(publisher == "Nintendo");
}

TEST_CASE("truncated compressed titles give no names and never the raw bytes") {
    auto nacp = compressed_nacp(100);
    check_no_names(drpc::unpack_nacp_titles(nacp));
    CHECK(title_block_is_zero(nacp));
}

TEST_CASE("compressed size larger than the title block is refused") {
    auto nacp = compressed_nacp(0x2FFF);
    check_no_names(drpc::unpack_nacp_titles(nacp));
    CHECK(title_block_is_zero(nacp));
}

TEST_CASE("unknown titles data format is refused") {
    auto nacp = compressed_nacp(sizeof kCompressedTitles);
    nacp[drpc::kNacpTitlesDataFormatOffset] = 2;
    check_no_names(drpc::unpack_nacp_titles(nacp));
    CHECK(title_block_is_zero(nacp));
}

TEST_CASE("NACP too short to hold the format byte gives no names") {
    std::vector<uint8_t> nacp(drpc::kNacpTitlesDataFormatOffset, 0);
    std::memcpy(nacp.data(), "Zelda", 5);
    check_no_names(drpc::unpack_nacp_titles(nacp));
}
