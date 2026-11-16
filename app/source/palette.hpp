#pragma once

#include <borealis.hpp>

namespace app::palette {

inline NVGcolor accent() { return nvgRGB(0x58, 0x65, 0xF2); }
inline NVGcolor accent_light() { return nvgRGB(0x8C, 0x96, 0xF7); }
inline NVGcolor card() { return nvgRGB(0x2B, 0x2D, 0x31); }
inline NVGcolor card_inner() { return nvgRGB(0x1E, 0x1F, 0x22); }
inline NVGcolor text() { return nvgRGB(0xF2, 0xF3, 0xF5); }
inline NVGcolor text_muted() { return nvgRGB(0xB5, 0xBA, 0xC1); }
inline NVGcolor text_faint() { return nvgRGB(0x80, 0x84, 0x8E); }
inline NVGcolor green() { return nvgRGB(0x23, 0xA5, 0x5A); }
inline NVGcolor yellow() { return nvgRGB(0xF0, 0xB2, 0x32); }
inline NVGcolor red() { return nvgRGB(0xF2, 0x3F, 0x43); }

}  // namespace app::palette
