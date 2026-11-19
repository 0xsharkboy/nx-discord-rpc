#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace drpc {

// Longest version kept: it must fit ipc::Status::min_version with its NUL.
inline constexpr size_t kMinVersionMaxLen = 15;

// The relay's answer when it refuses this client version, on any /v1 route.
inline constexpr int kHttpUpgradeRequired = 426;

// The min_version of a 426 answer when it is a string of the form X.Y.Z (digits only, at most
// kMinVersionMaxLen characters); "" otherwise.
std::string parse_min_version(std::string_view body);

}  // namespace drpc
