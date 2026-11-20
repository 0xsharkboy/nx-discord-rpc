#include "core/nacp_titles.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace drpc {
namespace {

constexpr size_t kCompressedSizeField = 2;

// Inflates a TitlesDataFormat 1 block into `out`; false on a bad size or a stream that does not end cleanly.
bool inflate_titles(std::span<const uint8_t> block, std::span<uint8_t> out) {
    const size_t size = block[0] | (static_cast<size_t>(block[1]) << 8);
    if (size == 0 || size > block.size() - kCompressedSizeField) return false;

    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return false;  // raw DEFLATE, no zlib header
    stream.next_in = const_cast<Bytef*>(block.data() + kCompressedSizeField);
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int rc = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return rc == Z_STREAM_END;
}

}  // namespace

std::vector<uint8_t> unpack_nacp_titles(std::span<uint8_t> nacp) {
    std::vector<uint8_t> titles(kNacpTitleCount * kNacpTitleEntrySize, 0);
    if (nacp.size() <= kNacpTitlesDataFormatOffset) return titles;

    const auto block = nacp.first(kNacpTitleBlockSize);
    bool ok = false;
    switch (nacp[kNacpTitlesDataFormatOffset]) {
        case 0:
            std::copy(block.begin(), block.end(), titles.begin());
            ok = true;
            break;
        case 1:
            ok = inflate_titles(block, titles);
            break;
        default:
            break;
    }
    if (!ok) std::fill(titles.begin(), titles.end(), uint8_t{0});
    std::copy_n(titles.begin(), kNacpTitleBlockSize, block.begin());
    return titles;
}

std::string_view nacp_title_name(std::span<const uint8_t> entries, size_t index) {
    const size_t offset = index * kNacpTitleEntrySize;
    if (offset + kNacpTitleNameSize > entries.size()) return {};
    const char* name = reinterpret_cast<const char*>(entries.data() + offset);
    const void* end = std::memchr(name, 0, kNacpTitleNameSize);
    return {name, end ? static_cast<size_t>(static_cast<const char*>(end) - name) : kNacpTitleNameSize};
}

}  // namespace drpc
