#pragma once

#include <string_view>

#include "core/link_client.hpp"
#include "core/relay_client.hpp"
#include "core/relay_url.hpp"

namespace drpc {

// One request over a new TLS connection, 8000 ms overall budget. Never logs secrets or bodies.
Outcome execute(const RelayUrl& url, const Request& req, std::string_view api_key);

// The same for the link flow and account calls. The body is kept up to 4096 bytes when complete;
// status -1 for a link id that cannot be sent.
LinkResponse execute_link(const RelayUrl& url, const LinkRequest& req, std::string_view api_key);

}  // namespace drpc
