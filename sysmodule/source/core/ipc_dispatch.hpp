#pragma once

#include <cstdint>
#include <span>
#include <vector>

#ifdef __SWITCH__
#include <switch/result.h>
#endif

#ifndef MAKERESULT
#define MAKERESULT(module, desc) (((module) & 0x1FF) | ((desc) << 9))
#endif

#include "core/shared_state.hpp"

namespace drpc {

struct DispatchResult {
    uint32_t rc;  // 0 on success
    std::vector<uint8_t> raw_out;
};

inline constexpr uint32_t kRcUnknownCommand = MAKERESULT(420, 1);
inline constexpr uint32_t kRcBadInput = MAKERESULT(420, 2);
inline constexpr uint32_t kRcBadBuffer = MAKERESULT(420, 3);

// in_raw: CMIF raw data after the header, possibly with trailing padding.
// in_buf: the client's in buffer (HipcMapAlias), empty when none was sent.
// out_buf: the client's out buffer, empty when none was sent. Nothing is written on error.
DispatchResult dispatch(uint32_t cmd_id, std::span<const uint8_t> in_raw, std::span<const uint8_t> in_buf,
                        std::span<uint8_t> out_buf, SharedState& state);

}  // namespace drpc
