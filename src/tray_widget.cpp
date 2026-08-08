#include "tray_widget.hpp"
#include "renderer.hpp"
#include <nanovg.h>
#include <algorithm>

TrayWidget::TrayWidget(const Theme& theme, TrayService& svc, int fontHandle)
    : theme_(theme), svc_(svc), fontHandle_(fontHandle) {
    subId_ = svc_.subscribe([this] {
        childrenDirty_ = true;
    });
}

TrayWidget::~TrayWidget() {
    if (subId_ >= 0) svc_.unsubscribe(subId_);
}

void TrayWidget::setTooltipCallbacks(std::function<void(int, const std::string&)> show,
                                      std::function<void()> dismiss) {
    showTooltip_ = std::move(show);
    dismissTooltip_ = std::move(dismiss);
    for (auto& icon : icons_)
        icon->setTooltipCallbacks(showTooltip_, dismissTooltip_);
}

void TrayWidget::rebuildChildren() {
    icons_.clear();
    const auto& items = svc_.items();
    int count = std::min((int)items.size(), MAX_VISIBLE);
    for (int i = 0; i < count; ++i) {
        auto icon = std::make_unique<SniIconWidget>(theme_, *items[i], fontHandle_);
        if (showTooltip_ && dismissTooltip_)
            icon->setTooltipCallbacks(showTooltip_, dismissTooltip_);
        icons_.push_back(std::move(icon));
    }
    overflow_ = (int)items.size() > MAX_VISIBLE;
    childrenDirty_ = false;
}

int TrayWidget::preferredWidth() const {
    if (childrenDirty_) const_cast<TrayWidget*>(this)->rebuildChildren();
    int w = 0;
    for (const auto& icon : icons_)
        w += icon->preferredWidth();
    if (overflow_) w += (int)theme_.iconInline + (int)theme_.gapItem;  // chevron
    return w;
}

int TrayWidget::preferredHeight() const {
    return (int)theme_.iconInline;
}

void TrayWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    if (childrenDirty_) rebuildChildren();
    int cx = x;
    for (auto& icon : icons_) {
        int iw = icon->preferredWidth();
        icon->layout(cx, y, iw, h);
        cx += iw;
    }
    // Overflow chevron occupies the remaining space; we just track its position.
}

void TrayWidget::render(const Renderer& renderer) const {
    if (childrenDirty_) const_cast<TrayWidget*>(this)->rebuildChildren();
    for (const auto& icon : icons_)
        icon->render(renderer);

    if (overflow_) {
        NVGcontext* vg = renderer.ctx();
        // Chevron at the right edge of the last icon.
        int chevronX = x_ + w_ - (int)theme_.iconInline - (int)theme_.gapItem;
        int chevronY = y_ + h_ / 2;
        nvgFontFaceId(vg, fontHandle_);
        nvgFontSize(vg, theme_.iconInline);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, theme_.textSecondary);
        nvgText(vg, (float)chevronX, (float)chevronY, "\xef\x83\x97", nullptr);  // \uf0d7
    }
}

bool TrayWidget::handleClick(int x, int y, uint32_t button) {
    if (childrenDirty_) rebuildChildren();
    for (auto& icon : icons_) {
        if (x >= icon->x() && x < icon->x() + icon->width() &&
            y >= icon->y() && y < icon->y() + icon->height()) {
            return icon->handleClick(x, y, button);
        }
    }
    return false;
}

void TrayWidget::handleHover(int x, int y) {
    if (childrenDirty_) rebuildChildren();
    for (auto& icon : icons_) {
        if (x >= icon->x() && x < icon->x() + icon->width() &&
            y >= icon->y() && y < icon->y() + icon->height()) {
            icon->handleHover(x, y);
        } else {
            icon->clearHover();
        }
    }
}

void TrayWidget::clearHover() {
    for (auto& icon : icons_)
        icon->clearHover();
}
