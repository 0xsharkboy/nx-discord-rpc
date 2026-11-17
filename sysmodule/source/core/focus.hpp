#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "core/report.hpp"

namespace drpc {

// Applet ids copied from libnx's AppletId enum (switch/services/applet.h); system_state.cpp checks them.
inline constexpr uint32_t kAppletApplication = 0x01;        // AppletId_application
inline constexpr uint32_t kAppletOverlay = 0x02;            // AppletId_OverlayApplet
inline constexpr uint32_t kAppletHomeMenu = 0x03;           // AppletId_SystemAppletMenu
inline constexpr uint32_t kAppletShop = 0x14;               // AppletId_LibraryAppletShop
inline constexpr uint32_t kAppletPhotoViewer = 0x15;        // AppletId_LibraryAppletPhotoViewer
inline constexpr uint32_t kAppletMyPage = 0x1A;             // AppletId_LibraryAppletMyPage

// One applet event from the play event log. in_focus is false for every other kind (out of focus, launch, exit).
struct FocusEvent {
    uint32_t applet_id;
    uint64_t program_id;
    bool in_focus;
};

struct Focus {
    bool app_in_front;
    Screen screen;
};

// Replays focus changes (oldest first) to find what is in front of the application app_program_id.
// nullopt when the events say nothing about focus: none besides the overlay, or none for that application.
std::optional<Focus> replay_focus(std::span<const FocusEvent> events, uint64_t app_program_id);

}  // namespace drpc
