#include "brightness_widget.hpp"
#include "renderer.hpp"
#include <cstdio>

BrightnessWidget::BrightnessWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, BrightnessService& svc)
    : theme_(theme), svc_(svc) {
    label_ = std::make_unique<Label>(ctx, fontHandle);
    label_->setFontSize(14.0f);
    label_->setColor(theme_.textPrimary);
    label_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    lastInfo_ = svc_.info();
    updateLabelText();
    subId_ = svc_.subscribe([this] {
        lastInfo_ = svc_.info();
        updateLabelText();
        if (requestRedraw_) requestRedraw_();
    });
}

BrightnessWidget::~BrightnessWidget() {
    if (subId_ >= 0) svc_.unsubscribe(subId_);
}

int BrightnessWidget::preferredWidth() const { return label_->preferredWidth(); }
int BrightnessWidget::preferredHeight() const { return 24; }

void BrightnessWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    label_->layout(x, y, w, h);
}

void BrightnessWidget::render(const Renderer& renderer) const {
    label_->render(renderer);
}

void BrightnessWidget::setCallback(std::function<void()> requestRedraw) {
    requestRedraw_ = std::move(requestRedraw);
}

void BrightnessWidget::updateLabelText() const {
    const char* icon = "\xef\x86\x85"; // \uf185 sun
    int pct = (int)(lastInfo_.level * 100.0f + 0.5f);
    if (pct > 100) pct = 100;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s  %d%%", icon, pct);
    label_->setText(buf);
}
