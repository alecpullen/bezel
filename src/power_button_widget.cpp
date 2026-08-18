#include "power_button_widget.hpp"
#include "renderer.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <cstdio>

PowerButtonWidget::PowerButtonWidget(const Theme& theme, NVGcontext* ctx, int fontHandle)
    : theme_(theme) {
    label_ = std::make_unique<Label>(ctx, fontHandle);
    label_->setFontSize(14.0f);
    label_->setColor(theme_.textPrimary);
    label_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    label_->setText("\xef\x80\x87");  // Power icon \uf087
}

int PowerButtonWidget::preferredWidth() const {
    return (int)theme_.iconInline + 4;
}

int PowerButtonWidget::preferredHeight() const {
    return (int)theme_.iconInline;
}

void PowerButtonWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    label_->layout(x, y, w, h);
}

void PowerButtonWidget::render(const Renderer& renderer) const {
    if (hovered_) {
        NVGcontext* vg = renderer.ctx();
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (float)x_, (float)y_, (float)w_, (float)h_, theme_.radiusControl);
        nvgFillColor(vg, theme_.panelBgElevated);
        nvgFill(vg);
    }
    label_->render(renderer);
}

bool PowerButtonWidget::handleClick(int, int, uint32_t button) {
    if (button != BTN_LEFT) return false;
    if (onClick_) onClick_();
    return true;
}

void PowerButtonWidget::handleHover(int, int) {
    if (!hovered_) {
        hovered_ = true;
        if (requestRedraw_) requestRedraw_();
    }
}

void PowerButtonWidget::clearHover() {
    if (hovered_) {
        hovered_ = false;
        if (requestRedraw_) requestRedraw_();
    }
}
