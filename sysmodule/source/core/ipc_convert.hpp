#pragma once

#include <cstddef>
#include <string_view>

#include "core/settings.hpp"
#include "drpc/protocol.hpp"

namespace drpc {

ipc::Settings to_ipc(const Settings& s);
// Copies the six toggles only; never touches relay_url, api_key, admin_key or username.
void apply_ipc(const ipc::Settings& in, Settings& out);
// Fills every byte of out: items past the list and the padding are zero.
void to_ipc(const Settings& s, ipc::HiddenList& out);
// Replaces hidden_titles with the list, which must pass ipc::valid_hidden; nothing else changes.
void apply_ipc(const ipc::HiddenList& in, Settings& out);
// NUL-terminated, truncated on a UTF-8 boundary. cap must be at least 1.
void copy_str(char* dst, size_t cap, std::string_view src);

}  // namespace drpc
