#include "game_list.hpp"

#include <algorithm>
#include <cstdio>

namespace app {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// <0, 0, >0 like strcmp, ASCII case folded; non-ASCII bytes compare as they are.
int compare_names(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const auto ca = static_cast<unsigned char>(lower(a[i]));
        const auto cb = static_cast<unsigned char>(lower(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

}  // namespace

void sort_games(std::vector<InstalledGame>& games) {
    std::sort(games.begin(), games.end(), [](const InstalledGame& a, const InstalledGame& b) {
        if (a.name.empty() != b.name.empty()) return b.name.empty();
        if (const int c = compare_names(a.name, b.name); c != 0) return c < 0;
        return a.title_id < b.title_id;
    });
}

RgbaIcon downscale_rgba(const uint8_t* src, int src_w, int src_h, size_t src_size, int out_w, int out_h) {
    RgbaIcon out;
    if (!src || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) return out;
    if (src_size < static_cast<size_t>(src_w) * static_cast<size_t>(src_h) * 4) return out;
    out.width = out_w;
    out.height = out_h;
    out.pixels.resize(static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 4);
    for (int y = 0; y < out_h; ++y) {
        const int y0 = static_cast<int>(static_cast<long long>(y) * src_h / out_h);
        int y1 = static_cast<int>(static_cast<long long>(y + 1) * src_h / out_h);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < out_w; ++x) {
            const int x0 = static_cast<int>(static_cast<long long>(x) * src_w / out_w);
            int x1 = static_cast<int>(static_cast<long long>(x + 1) * src_w / out_w);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; ++sy) {
                const uint8_t* row = src + (static_cast<size_t>(sy) * src_w + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4) {
                    for (int c = 0; c < 4; ++c) sum[c] += row[c];
                }
            }
            const unsigned n = static_cast<unsigned>((y1 - y0) * (x1 - x0));
            uint8_t* px = &out.pixels[(static_cast<size_t>(y) * out_w + x) * 4];
            for (int c = 0; c < 4; ++c) px[c] = static_cast<uint8_t>((sum[c] + n / 2) / n);
        }
    }
    return out;
}

std::string title_id_hex(uint64_t title_id) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(title_id));
    return buf;
}

}  // namespace app
