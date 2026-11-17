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

// The newest play events are enough to know who holds focus.
constexpr s32 kFocusWindow = 64;

// pdm stores each u64 id with its two u32 halves swapped.
uint64_t swapped_id(const u32 (&id)[2]) {
    return (static_cast<uint64_t>(id[0]) << 32) | id[1];
}

}  // namespace

std::optional<Focus> query_focus(uint64_t app_program_id) {
    if (!service_up(OptionalService::PdmQuery)) return std::nullopt;

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
    if (!service_up(OptionalService::PdmQuery) || !service_up(OptionalService::Account)) return std::nullopt;
    AccountUid uid{};
    if (R_FAILED(accountGetLastOpenedUser(&uid)) || !accountUidIsValid(&uid)) return std::nullopt;
    PdmPlayStatistics stats{};
    if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(title_id, uid, false, &stats)))
        return std::nullopt;
    return stats.playtime / 1'000'000'000ULL;
}

}  // namespace drpc
