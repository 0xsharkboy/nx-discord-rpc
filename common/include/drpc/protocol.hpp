#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace drpc::ipc {

inline constexpr char kServiceName[] = "drpc";
inline constexpr uint32_t kProtocolVersion = 3;

enum class Cmd : uint32_t {
    GetProtocolVersion = 0,
    GetStatus = 1,
    GetSettings = 2,
    SetSettings = 3,
    StartLink = 4,
    GetLink = 5,
    CancelLink = 6,
    Unlink = 7,
    ForgetKey = 8,
};

enum class LinkState : uint8_t { NoKey, Linked, Unauthorized, LinkBroken };
enum class RelayHealth : uint8_t { Ok, Backoff, Offline, NoUrl };
enum class LinkPhase : uint8_t { Idle, Starting, Pending, Linked, Denied, Expired, Failed };
enum class LinkFailure : uint8_t { None, RateLimited, Unreachable, Interrupted, Other };
enum class UnlinkResult : uint8_t { None, InProgress, Done, Failed };

struct Settings {
    uint8_t enabled, private_mode, show_paused, show_firmware, show_home_menu, debug_log, show_docked, show_playtime;
};

struct Status {
    char sysmodule_version[16];
    char username[40];
    char discord_id[24];
    char avatar[40];
    char relay_host[64];
    char name[132];
    char details[132];
    char title_id[20];
    uint8_t pad0[4];
    int64_t elapsed_s;
    uint32_t backoff_s;
    LinkState link;
    RelayHealth health;
    uint8_t shown;
    uint8_t is_game;
    UnlinkResult unlink;
    uint8_t reserved[3];
    uint8_t pad1[4];
};

struct Link {
    LinkPhase phase;
    LinkFailure failure;
    uint8_t reserved[2];
    uint32_t seconds_left;
    uint32_t retry_after_s;
    char user_code[16];
    char verification_uri[128];
    char username[40];
};

template <size_t N>
inline void terminate(char (&s)[N]) {
    s[N - 1] = '\0';
}

inline void terminate_strings(Status& s) {
    terminate(s.sysmodule_version);
    terminate(s.username);
    terminate(s.discord_id);
    terminate(s.avatar);
    terminate(s.relay_host);
    terminate(s.name);
    terminate(s.details);
    terminate(s.title_id);
}

inline void terminate_strings(Link& l) {
    terminate(l.user_code);
    terminate(l.verification_uri);
    terminate(l.username);
}

static_assert(sizeof(Settings) == 8);
static_assert(sizeof(Status) == 496);
static_assert(offsetof(Status, discord_id) == 56);
static_assert(offsetof(Status, avatar) == 80);
static_assert(offsetof(Status, elapsed_s) == 472);
static_assert(sizeof(Link) == 196);
static_assert(std::is_standard_layout_v<Settings> && std::is_trivial_v<Settings>);
static_assert(std::is_standard_layout_v<Status> && std::is_trivial_v<Status>);
static_assert(std::is_standard_layout_v<Link> && std::is_trivial_v<Link>);

}  // namespace drpc::ipc
