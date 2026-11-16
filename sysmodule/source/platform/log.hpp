#pragma once

#include <string_view>

namespace drpc::log {

void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);
// Dropped unless enabled with set_debug (config key debug_log).
void debug(std::string_view msg);
void set_debug(bool enabled);
// Both keys are masked in every line written.
void set_secrets(std::string_view api_key, std::string_view admin_key);

}  // namespace drpc::log
