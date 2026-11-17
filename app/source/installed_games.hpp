#pragma once

#include <atomic>
#include <vector>

#include "game_list.hpp"

namespace app {

// Icons are scaled down to this many pixels a side so a long library stays small in memory.
inline constexpr int kGameIconPixels = 80;

// The games on the console (ns application records with their name and icon), sorted with sort_games. Slow: run it
// off the UI thread. Returns early with what it has once cancel is set. The desktop build returns sample games.
std::vector<InstalledGame> list_installed_games(const std::atomic<bool>& cancel);

}  // namespace app
