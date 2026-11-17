#pragma once

#include <array>
#include <borealis.hpp>
#include <memory>
#include <optional>

#include "diagnostics.hpp"
#include "drpc/client.hpp"

namespace app {

class QrView;

// Status > Test connection: network, relay, key, link and presence, each OK or failed with a short reason.
// The network and relay checks run on the brls::async worker; the sysmodule is only asked on the UI thread.
class ConnectionTestView : public brls::ScrollingFrame {
public:
    explicit ConnectionTestView(drpc::Client& client);
    ~ConnectionTestView() override;

    static void open(drpc::Client& client);

private:
    struct Row {
        brls::Box* dot;
        brls::Label* reason;
    };

    void start();
    // Called on the UI thread with what the worker found so far.
    void on_network(unsigned run, bool connected);
    void on_healthz(unsigned run, diag::Healthz healthz);
    void read_status();
    void render();

    drpc::Client& client_;
    diag::Facts facts_{};
    bool running_ = false;
    unsigned run_ = 0;  // results from an older run are dropped

    std::array<Row, diag::kStepCount> rows_{};
    brls::Box* help_;
    QrView* qr_;
    brls::Label* tip_;
    brls::Button* again_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace app
