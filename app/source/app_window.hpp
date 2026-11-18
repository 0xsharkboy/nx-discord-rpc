#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <borealis/core/timer.hpp>

#include "app_heartbeat.hpp"
#include "drpc/client.hpp"
#include "sysmodule_launcher.hpp"
#include "update_manager.hpp"

namespace brls {
class Activity;
class AppletFrame;
class Button;
class Label;
}  // namespace brls

namespace app {

std::string blocking_message(drpc::ConnectError error, uint32_t remote_version);

class AppWindow {
public:
    explicit AppWindow(std::unique_ptr<drpc::Client> client);
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    void open();

private:
    void show_blocking(const std::string& message, bool offer_start);
    void show_tabs();
    brls::AppletFrame* make_tabs();
    void start_sysmodule();
    void poll_start(int attempt);
    void connected();
    void start_failed(const std::string& message);
    // The quiet update check, once per run, when Settings > Check for updates is on.
    void auto_check_updates();

    std::unique_ptr<drpc::Client> client_;
    // The not-running screen, while it is shown.
    brls::Activity* blocking_ = nullptr;
    brls::Label* message_ = nullptr;
    brls::Label* start_note_ = nullptr;
    brls::Button* start_button_ = nullptr;
    bool starting_ = false;
    LaunchResult launch_{};  // the last Start now launch, reported if no connection follows
    // Declared after client_: destroyed (and its thread joined) before the client goes.
    std::unique_ptr<UpdateManager> updater_;
    bool auto_checked_ = false;
    Heartbeat heartbeat_;
    brls::RepeatingTimer heartbeat_timer_;
};

}  // namespace app
