#pragma once

#include <borealis.hpp>
#include <functional>

#include "drpc/client.hpp"

namespace app {

class QrView;
class ResultBadge;

std::string link_countdown(uint32_t seconds);
uint32_t retry_minutes(uint32_t retry_after_s);

class LinkView : public brls::Box {
public:
    LinkView(drpc::Client& client, std::function<void()> on_close);

    static void open(drpc::Client& client, std::function<void()> on_close);

private:
    enum class Screen { None, Busy, Pending, Result };

    void start();
    void poll();
    void render(const drpc::ipc::Link& link);
    void show(Screen screen);
    void show_result(const drpc::ipc::Link& link);
    void cancel();
    void close();

    drpc::Client& client_;
    std::function<void()> on_close_;
    Screen screen_ = Screen::None;
    bool closing_ = false;

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
