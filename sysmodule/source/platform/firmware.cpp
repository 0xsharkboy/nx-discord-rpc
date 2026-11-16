#include "platform/firmware.hpp"

#include <switch.h>

#include <cstring>

namespace drpc {

std::string firmware_display_version() {
    if (R_FAILED(setsysInitialize())) return {};
    SetSysFirmwareVersion fw{};
    const Result rc = setsysGetFirmwareVersion(&fw);
    setsysExit();
    if (R_FAILED(rc)) return {};
    return std::string(fw.display_version, strnlen(fw.display_version, sizeof fw.display_version));
}

}  // namespace drpc
