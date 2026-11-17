#include "installed_games.hpp"

#include <algorithm>

#include "drpc/protocol.hpp"

#ifdef __SWITCH__
#include <borealis/extern/nanovg/stb_image.h>

#include "title_icon.hpp"
#endif

namespace app {

#ifdef __SWITCH__

namespace {

RgbaIcon decode_icon(const std::vector<uint8_t>& jpeg) {
    if (jpeg.empty()) return {};
    int w = 0, h = 0, n = 0;
    stbi_uc* rgba = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &n, 4);
    if (!rgba) return {};
    RgbaIcon icon = downscale_rgba(rgba, w, h, static_cast<size_t>(w) * static_cast<size_t>(h) * 4, kGameIconPixels,
                                   kGameIconPixels);
    stbi_image_free(rgba);
    return icon;
}

}  // namespace

std::vector<InstalledGame> list_installed_games(const std::atomic<bool>& cancel) {
    std::vector<uint64_t> ids;
    title_icon::list_applications(ids);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    std::vector<InstalledGame> games;
    games.reserve(ids.size());
    for (const uint64_t id : ids) {
        if (cancel.load()) break;
        if (id < drpc::ipc::kMinReportableTitleId) continue;
        InstalledGame game;
        game.title_id = id;
        title_icon::Control control;
        if (title_icon::read_control(id, control)) {
            game.name = std::move(control.name);
            game.icon = decode_icon(control.jpeg);
        }
        games.push_back(std::move(game));
    }
    sort_games(games);
    return games;
}

#else

namespace {

// A diagonal two-color gradient, enough to see the icon slot in the desktop screenshots.
RgbaIcon sample_icon(uint32_t from, uint32_t to) {
    RgbaIcon icon;
    icon.width = icon.height = kGameIconPixels;
    icon.pixels.resize(static_cast<size_t>(kGameIconPixels) * kGameIconPixels * 4);
    for (int y = 0; y < kGameIconPixels; ++y) {
        for (int x = 0; x < kGameIconPixels; ++x) {
            const unsigned t = static_cast<unsigned>((x + y) * 255 / (2 * (kGameIconPixels - 1)));
            uint8_t* px = &icon.pixels[(static_cast<size_t>(y) * kGameIconPixels + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const unsigned a = (from >> (16 - 8 * c)) & 0xFF, b = (to >> (16 - 8 * c)) & 0xFF;
                px[c] = static_cast<uint8_t>((a * (255 - t) + b * t) / 255);
            }
            px[3] = 0xFF;
        }
    }
    return icon;
}

}  // namespace

// The fake client's running title (0100000000010000) and its two listed titles (01007EF00011E000 Private,
// 0100152000022000 Hidden) are among them, so the screen shows all three states.
std::vector<InstalledGame> list_installed_games(const std::atomic<bool>&) {
    struct Sample {
        uint64_t id;
        const char* name;
        uint32_t from, to;
    };
    static constexpr Sample kSamples[] = {
        {0x0100000000010000ULL, "Monument Valley", 0xF2A65A, 0xC0392B},
        {0x01007EF00011E000ULL, "Wild Horizons", 0x5DADE2, 0x1E8449},
        {0x0100152000022000ULL, "Kart Rally Deluxe", 0xF4D03F, 0xE74C3C},
        {0x0100A0D02127E000ULL, "Pixel Harbor", 0x48C9B0, 0x2E4053},
        {0x0100ABCD00010000ULL, "Starlight Garden", 0xAF7AC5, 0x1B2631},
        {0x01001B300B9BE000ULL, "echo valley racing", 0x85C1E9, 0x5B2C6F},
        {0x0100E95004038000ULL, "A Very Long Game Name That Keeps Going Well Past the Width of One Row", 0xEB984E,
         0x17202A},
        {0x0100F2C0115B6000ULL, "Tiny Tactics", 0x58D68D, 0x7D3C98},
        {0x01009AA000FAA000ULL, "", 0, 0},  // no control data: shown by title id, without an icon
    };
    std::vector<InstalledGame> games;
    for (const Sample& s : kSamples) {
        InstalledGame game;
        game.title_id = s.id;
        game.name = s.name;
        if (s.name[0]) game.icon = sample_icon(s.from, s.to);
        games.push_back(std::move(game));
    }
    sort_games(games);
    return games;
}

#endif

}  // namespace app
