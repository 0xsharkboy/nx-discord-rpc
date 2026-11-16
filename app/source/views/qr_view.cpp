#include "views/qr_view.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

#include "qrcodegen.hpp"

namespace app {

namespace {

constexpr int kQuietZone = 4;

}  // namespace

QrView::QrView(float side) {
    setWidth(side);
    setHeight(side);
}

void QrView::set_text(const std::string& text) {
    if (text == text_) return;
    text_ = text;
    modules_.clear();
    size_ = 0;
    if (text.empty()) return;
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        size_ = qr.getSize();
        modules_.resize(static_cast<size_t>(size_) * size_);
        for (int row = 0; row < size_; ++row)
            for (int col = 0; col < size_; ++col) modules_[static_cast<size_t>(row) * size_ + col] = qr.getModule(col, row);
    } catch (const std::exception&) {
        modules_.clear();
        size_ = 0;
    }
}

void QrView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style, brls::FrameContext*) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, 12);
    nvgFillColor(vg, nvgRGB(0xFF, 0xFF, 0xFF));
    nvgFill(vg);
    if (size_ == 0) return;

    const float side = std::min(width, height);
    const float module = std::floor(side / static_cast<float>(size_ + 2 * kQuietZone));
    if (module < 1) return;
    const float origin_x = std::round(x + (width - module * size_) / 2);
    const float origin_y = std::round(y + (height - module * size_) / 2);

    nvgSave(vg);
    nvgShapeAntiAlias(vg, 0);
    nvgBeginPath(vg);
    for (int row = 0; row < size_; ++row) {
        for (int col = 0; col < size_; ++col) {
            if (modules_[static_cast<size_t>(row) * size_ + col])
                nvgRect(vg, origin_x + col * module, origin_y + row * module, module, module);
        }
    }
    nvgFillColor(vg, nvgRGB(0x00, 0x00, 0x00));
    nvgFill(vg);
    nvgRestore(vg);
}

}  // namespace app
