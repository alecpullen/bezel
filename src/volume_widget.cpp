#include "volume_widget.hpp"
#include "renderer.hpp"
#include <cstdio>

VolumeWidget::VolumeWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, AudioService& svc)
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

VolumeWidget::~VolumeWidget() {
    if (subId_ >= 0) svc_.unsubscribe(subId_);
}

int VolumeWidget::preferredWidth() const { return label_->preferredWidth(); }
int VolumeWidget::preferredHeight() const { return 24; }

void VolumeWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    label_->layout(x, y, w, h);
}

void VolumeWidget::render(const Renderer& renderer) const {
    label_->render(renderer);
}

void VolumeWidget::setCallback(std::function<void()> requestRedraw) {
    requestRedraw_ = std::move(requestRedraw);
}

void VolumeWidget::updateLabelText() const {
    const char* icon;
    if (lastInfo_.muted || lastInfo_.volume <= 0.0f) {
        icon = "\xef\x9a\xa9"; // \uf6a9 muted
    } else if (lastInfo_.volume < 0.4f) {
        icon = "\xef\x80\xa7"; // \uf027 low
    } else {
        icon = "\xef\x80\xa8"; // \uf028 high
    }
    int pct = (int)(lastInfo_.volume * 100.0f + 0.5f);
    if (pct > 100) pct = 100;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s  %d%%", icon, pct);
    label_->setText(buf);
}
