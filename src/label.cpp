// label.cpp
#include "label.hpp"

#include <nanovg.h>
#include <cstdio>

Label::Label(NVGcontext* ctx, int fontHandle, const std::string& text)
    : ctx_(ctx), fontHandle_(fontHandle), color_(nvgRGBA(0,0,0,0)), text_(text) {
    recalcPreferred();
}

void Label::setText(const std::string& text) {
    text_ = text;
    recalcPreferred();
}

void Label::setFontSize(float px) {
    fontSize_ = px;
    recalcPreferred();
}

void Label::setColor(NVGcolor color) {
    color_ = color;
}

void Label::setAlign(int align) {
    align_ = align;
}

void Label::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
}

void Label::render(const Renderer& renderer) const {
    auto* vg = renderer.ctx();
    if (!vg || text_.empty()) return;

    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, fontSize_);
    nvgFillColor(vg, color_);
    nvgTextAlign(vg, align_);

    float tx = static_cast<float>(x_);
    float ty = static_cast<float>(y_);

    if (align_ & NVG_ALIGN_CENTER)
        tx += w_ * 0.5f;
    else if (align_ & NVG_ALIGN_RIGHT)
        tx += static_cast<float>(w_);

    if (align_ & NVG_ALIGN_MIDDLE)
        ty += h_ * 0.5f;
    else if (align_ & NVG_ALIGN_BOTTOM)
        ty += static_cast<float>(h_);

    nvgText(vg, tx, ty, text_.c_str(), nullptr);
}

void Label::recalcPreferred() {
    if (text_.empty()) {
        prefW_ = 0;
        prefH_ = 0;
        return;
    }

    if (fontHandle_ > 0) nvgFontFaceId(ctx_, fontHandle_);
    nvgFontSize(ctx_, fontSize_);
    float bounds[4];
    nvgTextBounds(ctx_, 0, 0, text_.c_str(), nullptr, bounds);
    prefW_ = static_cast<int>(bounds[2] - bounds[0] + 1);
    prefH_ = static_cast<int>(bounds[3] - bounds[1] + 1);
}
