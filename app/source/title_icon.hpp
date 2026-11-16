#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app::title_icon {

void init();
void exit();

// The JPEG icon from the title's NACP, or an empty buffer when there is none.
const std::vector<uint8_t>& get(const std::string& title_id);

}  // namespace app::title_icon
