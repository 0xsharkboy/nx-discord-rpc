#pragma once

#include <borealis.hpp>

#include "drpc/client.hpp"

namespace app {

class AboutTab : public brls::ScrollingFrame {
public:
    explicit AboutTab(drpc::Client& client);
};

}  // namespace app
