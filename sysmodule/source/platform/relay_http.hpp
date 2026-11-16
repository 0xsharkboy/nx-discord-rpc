#pragma once

#include <string_view>

#include "core/relay_client.hpp"
#include "core/relay_url.hpp"

namespace drpc {

// One request over a new TLS connection, 8000 ms overall budget. Never logs secrets or bodies.
Outcome execute(const RelayUrl& url, const Request& req, std::string_view api_key, std::string_view admin_key);

}  // namespace drpc
