#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace drpc::ipc {

inline constexpr char kServiceName[] = "drpc";
inline constexpr uint32_t kProtocolVersion = 6;

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
    GetHidden = 9,   // out buffer: HiddenList
    SetHidden = 10,  // in buffer: HiddenList
};

// Outdated: the relay refuses this client version (HTTP 426); Status.min_version names the minimum when known.
enum class LinkState : uint8_t { NoKey, Linked, Unauthorized, LinkBroken, Outdated };
enum class RelayHealth : uint8_t { Ok, Backoff, Offline, NoUrl };
enum class LinkPhase : uint8_t { Idle, Starting, Pending, Linked, Denied, Expired, Failed };
enum class LinkFailure : uint8_t { None, RateLimited, Unreachable, Interrupted, Other, Outdated };
enum class UnlinkResult : uint8_t { None, InProgress, Done, Failed };
enum class TitleVisibility : uint8_t { Shown = 0, Private = 1, Hidden = 2 };

inline constexpr size_t kMaxHiddenTitles = 64;
// Titles below this one are system titles: never reported, never in the hidden list.
inline constexpr uint64_t kMinReportableTitleId = 0x0100000000010000ULL;

struct Settings {
    uint8_t enabled, private_mode, show_paused, show_firmware, show_home_menu, debug_log, show_docked, show_playtime;
    // Protocol 6.
    uint8_t show_badge;
    uint8_t reserved[7];
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
    int32_t last_relay_status;  // last HTTP status of a presence PUT or DELETE; 0 none yet, -1 network error
    uint64_t running_title_id;  // the running application whatever its visibility, 0 on the HOME Menu
    // Protocol 5: the minimum client version named by the relay's last 426 answer, "" when unknown. It is
    // filled while link is Outdated and while the last link attempt failed with LinkFailure::Outdated, so it
    // can be set while link is not Outdated: key the UI on link == Outdated or the link failure, never on
    // a non-empty min_version.
    char min_version[16];
};

// Shown titles are not listed. Items past count are zero.
struct HiddenTitle {
    uint64_t title_id;
    uint8_t visibility;  // TitleVisibility::Private or Hidden
    uint8_t pad[7];
};

struct HiddenList {
    uint32_t count;
    uint32_t pad;
    HiddenTitle items[kMaxHiddenTitles];
};

// At most kMaxHiddenTitles entries, each Private or Hidden, a title id from kMinReportableTitleId up, no
// title twice. Items past count are not looked at.
inline bool valid_hidden(const HiddenList& l) {
    if (l.count > kMaxHiddenTitles) return false;
    for (uint32_t i = 0; i < l.count; ++i) {
        const HiddenTitle& t = l.items[i];
        if (t.visibility != static_cast<uint8_t>(TitleVisibility::Private) &&
            t.visibility != static_cast<uint8_t>(TitleVisibility::Hidden))
            return false;
        if (t.title_id < kMinReportableTitleId) return false;
        for (uint32_t j = 0; j < i; ++j) {
            if (l.items[j].title_id == t.title_id) return false;
        }
    }
    return true;
}

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
    terminate(s.min_version);
}

inline void terminate_strings(Link& l) {
    terminate(l.user_code);
    terminate(l.verification_uri);
    terminate(l.username);
}

static_assert(sizeof(Settings) == 16);
static_assert(sizeof(Status) == 520);
static_assert(offsetof(Status, discord_id) == 56);
static_assert(offsetof(Status, avatar) == 80);
static_assert(offsetof(Status, elapsed_s) == 472);
static_assert(offsetof(Status, last_relay_status) == 492);
static_assert(offsetof(Status, running_title_id) == 496);
static_assert(offsetof(Status, min_version) == 504);
static_assert(sizeof(HiddenTitle) == 16);
static_assert(sizeof(HiddenList) == 8 + 16 * kMaxHiddenTitles);
static_assert(sizeof(Link) == 196);
static_assert(std::is_standard_layout_v<Settings> && std::is_trivial_v<Settings>);
static_assert(std::is_standard_layout_v<Status> && std::is_trivial_v<Status>);
static_assert(std::is_standard_layout_v<Link> && std::is_trivial_v<Link>);
static_assert(std::is_standard_layout_v<HiddenList> && std::is_trivial_v<HiddenList>);

}  // namespace drpc::ipc

namespace drpc {
using TitleVisibility = ipc::TitleVisibility;
}  // namespace drpc
