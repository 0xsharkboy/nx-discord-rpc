#pragma once

#include <string_view>

#ifndef NXDRPC_VERSION
#error "NXDRPC_VERSION must be defined from the VERSION file"
#endif

namespace drpc {

inline constexpr std::string_view kVersion = NXDRPC_VERSION;

}  // namespace drpc
