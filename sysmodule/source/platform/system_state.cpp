#include "platform/system_state.hpp"

#include <switch.h>

#include <cstddef>
#include <span>

#include "platform/init.hpp"

namespace drpc {
namespace {

static_assert(kAppletApplication == AppletId_application);
static_assert(kAppletOverlay == AppletId_OverlayApplet);
static_assert(kAppletHomeMenu == AppletId_SystemAppletMenu);
static_assert(kAppletShop == AppletId_LibraryAppletShop);
static_assert(kAppletPhotoViewer == AppletId_LibraryAppletPhotoViewer);
static_assert(kAppletMyPage == AppletId_LibraryAppletMyPage);

// pdm:qry has few sessions, and am needs one when a game reads the play log of other games (NACP
// PlayLogQueryCapability, for example the Kirby games): a session held by this sysmodule made that fail and
// crash am (2021-0003, sm OutOfSessions). So it is opened for each query and closed right after.
class PdmSession {
public:
    PdmSession() : ok_(R_SUCCEEDED(pdmqryInitialize())) {}
    ~PdmSession() {
        if (ok_) pdmqryExit();
    }
    PdmSession(const PdmSession&) = delete;
    PdmSession& operator=(const PdmSession&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_;
};

// The newest play events are enough to know who holds focus.
constexpr s32 kFocusWindow = 64;

// pdm stores each u64 id with its two u32 halves swapped.
uint64_t swapped_id(const u32 (&id)[2]) {
    return (static_cast<uint64_t>(id[0]) << 32) | id[1];
}

}  // namespace

std::optional<Focus> query_focus(uint64_t app_program_id) {
    if (!service_up(OptionalService::PdmQuery)) return std::nullopt;
    const PdmSession session;
    if (!session.ok()) return std::nullopt;

    s32 total = 0, first = 0, last = 0;
    if (R_FAILED(pdmqryGetAvailablePlayEventRange(&total, &first, &last)) || total <= 0) return std::nullopt;
    // end_entry_index is taken as inclusive (as switch-discord-activity does). If it is exclusive, the window
    // holds 63 events and the newest one still comes last, which is all the replay needs.
    const s32 from = last - (kFocusWindow - 1) < first ? first : last - (kFocusWindow - 1);

    // About 3.5 KB and 1.5 KB: static, off the small main thread stack.
    static PdmPlayEvent raw[kFocusWindow];
    static FocusEvent events[kFocusWindow];
    s32 count = 0;
    if (R_FAILED(pdmqryQueryPlayEvent(from, raw, kFocusWindow, &count)) || count <= 0) return std::nullopt;

    size_t n = 0;
    for (s32 i = 0; i < count && i < kFocusWindow; ++i) {
        const PdmPlayEvent& e = raw[i];
        if (e.play_event_type != PdmPlayEventType_Applet) continue;
        const auto& a = e.event_data.applet;
        events[n++] = FocusEvent{a.applet_id, swapped_id(a.program_id), a.event_type == PdmAppletEventType_InFocus};
    }
    return replay_focus(std::span<const FocusEvent>{events, n}, app_program_id);
}

std::optional<bool> query_docked() {
    if (!service_up(OptionalService::Apm)) return std::nullopt;
    ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
    if (R_FAILED(apmGetPerformanceMode(&mode)) || mode == ApmPerformanceMode_Invalid) return std::nullopt;
    return mode == ApmPerformanceMode_Boost;
}

std::optional<uint64_t> query_playtime_s(uint64_t title_id) {
    // All profiles together: pdm only records which profile played once a session ends (checked on hardware), so
    // the profile playing now cannot be told apart. switch-discord-activity uses the same console-wide query.
    if (!service_up(OptionalService::PdmQuery)) return std::nullopt;
    const PdmSession session;
    if (!session.ok()) return std::nullopt;
    PdmPlayStatistics stats{};
    if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationId(title_id, false, &stats))) return std::nullopt;
    return stats.playtime / 1'000'000'000ULL;
}

}  // namespace drpc
