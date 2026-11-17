#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The app cannot replace its own .nro while it runs (hbloader keeps it open for romfs), so the updater
// stages the new one next to it and the sysmodule swaps it in once the app has closed.
//
// "The app is open" is a heartbeat file: the app writes (in place, fixed width) the console's monotonic clock (milliseconds since
// boot, the same counter in every process) into it at start and every kHeartbeatMs, and deletes it on exit.
// A file whose stamp is older than kHeartbeatStaleMs, or ahead of the clock (written before a reboot), is
// left by an app that crashed or a console that lost power: the app counts as closed.
namespace drpc::nro_swap {

inline constexpr const char* kNro = "/switch/nx-discord-rpc/nx-discord-rpc.nro";
inline constexpr const char* kStaged = "/switch/nx-discord-rpc/nx-discord-rpc.nro.new";
inline constexpr const char* kHeartbeat = "/config/nx-discord-rpc/app.running";

inline constexpr int64_t kHeartbeatMs = 5'000;
inline constexpr int64_t kHeartbeatStaleMs = 20'000;

// The heartbeat file's content.
std::string format_heartbeat(int64_t now_ms);
std::optional<int64_t> parse_heartbeat(std::string_view text);

// Whether a heartbeat stamp (nullopt: no file, or unreadable) means the app is open now.
bool app_open(std::optional<int64_t> stamp_ms, int64_t now_ms);

enum class Action { None, Wait, Swap };

Action decide(bool staged_exists, std::optional<int64_t> stamp_ms, int64_t now_ms);

enum class SwapResult { Swapped, Locked, Failed };

// Puts staged in place of target: target is renamed to target.old, staged to target, then target.old is
// removed. FAT has no rename over an existing file, and this way a failed second rename puts the old file
// back instead of leaving none. Locked: target could not be moved (an open file), nothing changed. Failed:
// staged could not be renamed; target was restored. Paths are full ("sdmc:/..." on the console). Also used
// by the app's updater for the files it replaces itself.
SwapResult swap(const std::string& target, const std::string& staged);

// Reads the heartbeat file; nullopt when it is missing or unreadable.
std::optional<int64_t> read_heartbeat(const std::string& path);

}  // namespace drpc::nro_swap
