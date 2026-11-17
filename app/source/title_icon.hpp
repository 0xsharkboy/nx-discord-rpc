#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app::title_icon {

void init();
void exit();

// The JPEG icon from the title's NACP, or an empty buffer when there is none. UI thread only (cached).
const std::vector<uint8_t>& get(const std::string& title_id);

struct Control {
    std::string name;           // the console language's entry, else the first one set
    std::vector<uint8_t> jpeg;  // empty when there is none
};

// The title's name and JPEG icon, read from ns without caching. Safe from any thread. False when ns has no
// control data for the title (or ns is not available, as on desktop).
bool read_control(uint64_t title_id, Control& out);

// Every application ns has a record for (installed, archived or on a game card), in ns order. Safe from any thread.
// False when ns cannot be read (always on desktop).
bool list_applications(std::vector<uint64_t>& out);

}  // namespace app::title_icon
