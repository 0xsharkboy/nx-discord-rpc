#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/settings.hpp"

namespace drpc {

enum class Scene { HomeMenu, Game, Paused };
enum class Screen { Unknown, HomeMenu, Shop, Album, UserPage };

struct SceneInput {
    Scene scene = Scene::HomeMenu;
    uint64_t title_id = 0;
    std::string game_name;
    std::string firmware;
    Screen paused_screen = Screen::Unknown;
    std::optional<bool> docked;
    std::optional<uint64_t> playtime_s;
};

struct Report {
    std::string name, details;
    std::optional<std::string> title_id;
    std::optional<std::string> icon;  // SHA-256 of the title's icon, lowercase hex; set by the caller
    bool operator==(const Report&) const = default;
};

std::optional<Report> render_report(const SceneInput& in, const Settings& settings);
std::string report_json(const Report& report, std::optional<int64_t> elapsed_s);
std::string format_playtime(uint64_t seconds);
std::string truncate_utf8(std::string_view s, size_t max_bytes);

}  // namespace drpc
