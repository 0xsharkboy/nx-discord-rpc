#pragma once

#include <cstdint>
#include <cstring>

#include "drpc/protocol.hpp"

// Edits of the per-title visibility list, shared by the app and the overlay. Every change starts from the list
// just read from the sysmodule, so entries for games that are not on screen (or no longer installed) are kept.
namespace drpc {

// Shown -> Playing a game (Private) -> Hidden -> Shown.
constexpr TitleVisibility next_visibility(TitleVisibility v) {
    switch (v) {
        case TitleVisibility::Shown:
            return TitleVisibility::Private;
        case TitleVisibility::Private:
            return TitleVisibility::Hidden;
        case TitleVisibility::Hidden:
            return TitleVisibility::Shown;
    }
    return TitleVisibility::Shown;
}

// Shown when the title is not listed or the stored value is unknown.
inline TitleVisibility visibility_of(const ipc::HiddenList& list, uint64_t title_id) {
    const uint32_t count = list.count < ipc::kMaxHiddenTitles ? list.count : ipc::kMaxHiddenTitles;
    for (uint32_t i = 0; i < count; ++i) {
        if (list.items[i].title_id != title_id) continue;
        const uint8_t v = list.items[i].visibility;
        if (v == static_cast<uint8_t>(TitleVisibility::Private)) return TitleVisibility::Private;
        if (v == static_cast<uint8_t>(TitleVisibility::Hidden)) return TitleVisibility::Hidden;
        return TitleVisibility::Shown;
    }
    return TitleVisibility::Shown;
}

enum class SetVisibility { Ok, Full };

// Updates the title's entry in place, removes it for Shown (keeping the order of the others), or appends it.
// Full when a new entry would go past kMaxHiddenTitles; the list is then unchanged.
inline SetVisibility set_visibility(ipc::HiddenList& list, uint64_t title_id, TitleVisibility v) {
    if (list.count > ipc::kMaxHiddenTitles) list.count = ipc::kMaxHiddenTitles;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (list.items[i].title_id != title_id) continue;
        if (v != TitleVisibility::Shown) {
            list.items[i].visibility = static_cast<uint8_t>(v);
            return SetVisibility::Ok;
        }
        for (uint32_t j = i + 1; j < list.count; ++j) list.items[j - 1] = list.items[j];
        --list.count;
        std::memset(&list.items[list.count], 0, sizeof list.items[list.count]);
        return SetVisibility::Ok;
    }
    if (v == TitleVisibility::Shown) return SetVisibility::Ok;
    if (list.count >= ipc::kMaxHiddenTitles) return SetVisibility::Full;
    ipc::HiddenTitle& t = list.items[list.count++];
    std::memset(&t, 0, sizeof t);
    t.title_id = title_id;
    t.visibility = static_cast<uint8_t>(v);
    return SetVisibility::Ok;
}

}  // namespace drpc
