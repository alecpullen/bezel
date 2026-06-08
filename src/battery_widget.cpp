#include "battery_widget.hpp"
#include "renderer.hpp"
#include <cstdio>

BatteryWidget::BatteryWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, BatteryService& svc)
    : theme_(theme), svc_(svc) {
    percentLabel_ = std::make_unique<Label>(ctx, fontHandle);
    percentLabel_->setFontSize(14.0f);
    percentLabel_->setColor(theme_.textPrimary);
    percentLabel_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    lastInfo_ = svc_.info();
    updateLabelText();

    subId_ = svc_.subscribe([this] {
        lastInfo_ = svc_.info();
        updateLabelText();
        if (requestRedraw_) {
            requestRedraw_();
        }
    });
}

BatteryWidget::~BatteryWidget() {
    if (subId_ >= 0) {
        svc_.unsubscribe(subId_);
    }
}

int BatteryWidget::preferredWidth() const {
    return percentLabel_->preferredWidth();
}

int BatteryWidget::preferredHeight() const {
    return 24; // Matches layout inner height to allow vertical centering
}

void BatteryWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    percentLabel_->layout(x, y, w, h);
}

void BatteryWidget::render(const Renderer& renderer) const {
    percentLabel_->render(renderer);
}

void BatteryWidget::setCallback(std::function<void()> requestRedraw) {
    requestRedraw_ = requestRedraw;
}

void BatteryWidget::updateLabelText() const {
    const char* batteryIcon = "\xef\x89\x80"; // Battery full: \uf240
    int pct = lastInfo_.percentage;

    if (lastInfo_.state == BatteryInfo::Charging) {
        if (pct >= 95)      batteryIcon = "\xef\x96\x84"; // \uf584 (100% charging)
        else if (pct >= 85) batteryIcon = "\xef\x96\x8a"; // \uf58a (90% charging)
        else if (pct >= 75) batteryIcon = "\xef\x96\x89"; // \uf589 (80% charging)
        else if (pct >= 55) batteryIcon = "\xef\x96\x88"; // \uf588 (60% charging)
        else if (pct >= 35) batteryIcon = "\xef\x96\x87"; // \uf587 (40% charging)
        else if (pct >= 25) batteryIcon = "\xef\x96\x86"; // \uf586 (30% charging)
        else if (pct >= 15) batteryIcon = "\xef\x96\x85"; // \uf585 (20% charging)
        else                batteryIcon = "\xef\x96\x83"; // \uf583 (charging outline)
    } else {
        if (pct >= 90)      batteryIcon = "\xef\x89\x80"; // \uf240
        else if (pct >= 65) batteryIcon = "\xef\x89\x81"; // \uf241
        else if (pct >= 35) batteryIcon = "\xef\x89\x82"; // \uf242
        else if (pct >= 15) batteryIcon = "\xef\x89\x83"; // \uf243
        else                batteryIcon = "\xef\x89\x84"; // \uf244
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s  %d%%", batteryIcon, pct);
    percentLabel_->setText(buf);
}
