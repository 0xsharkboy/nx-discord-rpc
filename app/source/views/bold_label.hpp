#pragma once

#include <borealis.hpp>

namespace app {

class BoldLabel : public brls::Label {
public:
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override {
        brls::Label::draw(vg, x, y, width, height, style, ctx);
        brls::Label::draw(vg, x + 0.7f, y, width, height, style, ctx);
    }
};

}  // namespace app
