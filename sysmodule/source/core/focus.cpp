#include "core/focus.hpp"

#include <array>
#include <cstddef>

namespace drpc {
namespace {

constexpr size_t kMaxFocused = 16;

struct Holder {
    uint32_t applet_id;
    uint64_t program_id;
};

// Applications are told apart by program id; any other applet by its id alone.
bool same(const Holder& h, const FocusEvent& e) {
    return h.applet_id == e.applet_id && (e.applet_id != kAppletApplication || h.program_id == e.program_id);
}

Screen screen_of(uint32_t applet_id) {
    switch (applet_id) {
        case kAppletHomeMenu: return Screen::HomeMenu;
        case kAppletShop: return Screen::Shop;
        case kAppletPhotoViewer: return Screen::Album;
        case kAppletMyPage: return Screen::UserPage;
        default: return Screen::Unknown;
    }
}

}  // namespace

// The approach (replaying the pdm play event log with a stack of focused applets, skipping the overlay)
// follows switch-discord-activity by saekaze (MIT); this is our own code.
std::optional<Focus> replay_focus(std::span<const FocusEvent> events, uint64_t app_program_id) {
    std::array<Holder, kMaxFocused> stack{};
    size_t size = 0;
    bool app_seen = false;

    for (const FocusEvent& e : events) {
        if (e.applet_id == kAppletOverlay) continue;
        if (e.applet_id == kAppletApplication && e.program_id == app_program_id) app_seen = true;

        // An applet holds at most one place: the newest event for it decides whether it has focus.
        size_t kept = 0;
        for (size_t i = 0; i < size; ++i) {
            if (!same(stack[i], e)) stack[kept++] = stack[i];
        }
        size = kept;
        if (!e.in_focus) continue;
        if (size == stack.size()) {
            for (size_t i = 1; i < size; ++i) stack[i - 1] = stack[i];
            --size;
        }
        stack[size++] = Holder{e.applet_id, e.program_id};
    }

    // Not in the window (just launched, or not logged): the log cannot tell, so do not guess.
    if (!app_seen) return std::nullopt;

    for (size_t i = size; i-- > 0;) {
        const Holder& h = stack[i];
        if (h.applet_id == kAppletApplication) {
            if (h.program_id == app_program_id) return Focus{true, Screen::Unknown};
            continue;  // another application's stale entry
        }
        return Focus{false, screen_of(h.applet_id)};
    }
    return Focus{false, Screen::Unknown};
}

}  // namespace drpc
