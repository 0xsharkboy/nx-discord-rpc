#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// An icon already scaled down for a list row, as tightly packed RGBA rows.
struct RgbaIcon {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

struct InstalledGame {
    uint64_t title_id = 0;
    std::string name;  // empty when ns has no name for it (the screen shows the title id)
    RgbaIcon icon;     // empty when there is none
};

// By name, ignoring ASCII case, then by title id; games without a name go last.
void sort_games(std::vector<InstalledGame>& games);

// Box filter: each output pixel is the average of the source pixels it covers. Empty when a size is not positive or
// src is too short.
RgbaIcon downscale_rgba(const uint8_t* src, int src_w, int src_h, size_t src_size, int out_w, int out_h);

// Title ids as the sysmodule and the IPC strings write them: 16 upper case hex digits.
std::string title_id_hex(uint64_t title_id);

}  // namespace app
