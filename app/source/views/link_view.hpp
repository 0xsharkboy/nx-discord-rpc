#pragma once

#include <borealis.hpp>
#include <chrono>
#include <functional>

#include "drpc/client.hpp"
#include "update_manager.hpp"

namespace app {

class QrView;
class ResultBadge;

class LinkView : public brls::Box {
public:
    LinkView(drpc::Client& client, UpdateManager& updater, std::function<void()> on_close);

    static void open(drpc::Client& client, UpdateManager& updater, std::function<void()> on_close);

    void willAppear(bool resetState = false) override;

private:
    enum class Screen { None, Busy, Pending, Result };

    void start();
    void poll();
    void render(const drpc::ipc::Link& link);
    void show(Screen screen);
    void show_result(const drpc::ipc::Link& link);
    void retry();
    void cancel();
    void close();

    drpc::Client& client_;
    UpdateManager& updater_;
    std::function<void()> on_close_;
    Screen screen_ = Screen::None;
    bool started_ = false;
    bool seen_active_ = false;
    bool closing_ = false;
    drpc::ipc::LinkPhase result_phase_ = drpc::ipc::LinkPhase::Idle;
    drpc::ipc::LinkFailure result_failure_ = drpc::ipc::LinkFailure::None;
    std::chrono::steady_clock::time_point retry_at_;

    brls::Box* busy_;
    brls::Button* busy_cancel_;

    brls::Box* pending_;
    QrView* qr_;
    brls::Label* uri_;
    brls::Label* code_;
    brls::Label* expires_;
    brls::Button* cancel_;

    brls::Box* result_;
    ResultBadge* badge_;
    brls::Label* result_title_;
    brls::Label* result_body_;
    brls::Button* retry_;
    brls::Button* close_;

    brls::RepeatingTimer timer_;
};

}  // namespace app
