// clock.cpp
#include "clock.hpp"
#include "renderer.hpp"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstring>

namespace {

std::string localTimeFmt(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local;
    localtime_r(&t, &local);

    char buf[64] = {};
    if (std::strftime(buf, sizeof(buf), fmt, &local) == 0)
        return {};
    return buf;
}

}

Clock::Clock(const Theme& theme, NVGcontext* ctx, int fontHandle)
    : theme_(theme),
      lastMinute_(std::chrono::duration_cast<std::chrono::minutes>(
          std::chrono::system_clock::now().time_since_epoch())) {
    int fh = fontHandle >= 0 ? fontHandle : 0;

    cachedTime_ = localTimeFmt("%H:%M");
    cachedDate_ = localTimeFmt("%a %d %b");

    timeLabel_ = std::make_unique<Label>(ctx, fh, cachedTime_);
    timeLabel_->setFontSize(theme_.labelPrimaryPx);
    timeLabel_->setColor(theme_.textPrimary);
    timeLabel_->setAlign(NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);

    dateLabel_ = std::make_unique<Label>(ctx, fh, cachedDate_);
    dateLabel_->setFontSize(theme_.labelSecondaryPx);
    dateLabel_->setColor(theme_.textMuted);
    dateLabel_->setAlign(NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
}

int Clock::preferredWidth() const {
    return std::max(timeLabel_->preferredWidth(), dateLabel_->preferredWidth());
}

int Clock::preferredHeight() const {
    return timeLabel_->preferredHeight() + dateLabel_->preferredHeight() + 2;
}

void Clock::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    int th = timeLabel_->preferredHeight();
    timeLabel_->layout(x, y, w, th);
    dateLabel_->layout(x, y + th + 2, w, dateLabel_->preferredHeight());
}

void Clock::render(const class Renderer& renderer) const {
    auto now = std::chrono::system_clock::now();
    auto thisMinute = std::chrono::duration_cast<std::chrono::minutes>(
        now.time_since_epoch());

    if (thisMinute != lastMinute_ || cachedTime_.empty()) {
        cachedTime_ = localTimeFmt("%H:%M");
        cachedDate_ = localTimeFmt("%a %d %b");
        lastMinute_ = thisMinute;
        timeLabel_->setText(cachedTime_);
        dateLabel_->setText(cachedDate_);
        const_cast<Clock*>(this)->layout(x_, y_, w_, h_);
    }

    timeLabel_->render(renderer);
    dateLabel_->render(renderer);
}

void Clock::setDirty() {
    lastMinute_ = std::chrono::minutes(0);
}

bool Clock::tick() const {
    auto now = std::chrono::system_clock::now();
    auto thisMinute = std::chrono::duration_cast<std::chrono::minutes>(
        now.time_since_epoch());
    return thisMinute != lastMinute_;
}
