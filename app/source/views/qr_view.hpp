#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

namespace app {

class QrView : public brls::View {
public:
    explicit QrView(float side);

    void set_text(const std::string& text);
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

private:
    std::string text_;
    std::vector<bool> modules_;
    int size_ = 0;
};

}  // namespace app
