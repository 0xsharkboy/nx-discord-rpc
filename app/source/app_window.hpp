#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "drpc/client.hpp"

namespace app {

std::string blocking_message(drpc::ConnectError error, uint32_t remote_version);

class AppWindow {
public:
    explicit AppWindow(std::unique_ptr<drpc::Client> client);
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    void open();

private:
    void show_blocking(const std::string& message);
    void show_tabs();

    std::unique_ptr<drpc::Client> client_;
};

}  // namespace app
