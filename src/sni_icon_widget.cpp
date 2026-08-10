#include "sni_icon_widget.hpp"
#include "renderer.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <cstdio>
#include <cstring>

SniIconWidget::SniIconWidget(const Theme& theme, SniItem& item, int fontHandle)
    : theme_(theme), item_(item), fontHandle_(fontHandle) {}

int SniIconWidget::preferredWidth() const {
    return (int)theme_.iconInline + (int)theme_.gapItem;
}

int SniIconWidget::preferredHeight() const {
    return (int)theme_.iconInline;
}

void SniIconWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
}

void SniIconWidget::render(const Renderer& renderer) const {
    NVGcontext* vg = renderer.ctx();
    const TrayItemInfo& info = item_.info();

    // Dim passive items.
    bool passive = (info.status == "Passive");
    if (passive) nvgGlobalAlpha(vg, 0.5f);

    int handle = item_.imageHandle();
    float iconSize = theme_.iconInline;
    float ix = (float)x_;
    float iy = (float)y_ + ((float)h_ - iconSize) / 2.0f;

    if (handle >= 0) {
        NVGpaint ip = nvgImagePattern(vg, ix, iy, iconSize, iconSize, 0, handle, 1.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ix, iy, iconSize, iconSize, 3.0f);
        nvgFillPaint(vg, ip);
        nvgFill(vg);
    } else {
        // Placeholder: accent-tinted square + first letter of title.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, ix, iy, iconSize, iconSize, 3.0f);
        nvgFillColor(vg, theme_.accentTint);
        nvgFill(vg);
        if (!info.title.empty()) {
            nvgFontFaceId(vg, fontHandle_);
            nvgFontSize(vg, theme_.labelPrimaryPx);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, theme_.textPrimary);
            // First UTF-8 codepoint.
            char ch[8] = {0};
            std::strncpy(ch, info.title.c_str(), sizeof(ch) - 1);
            int n = 1;
            if ((ch[0] & 0x80) != 0) {
                if ((ch[0] & 0xE0) == 0xC0) n = 2;
                else if ((ch[0] & 0xF0) == 0xE0) n = 3;
                else if ((ch[0] & 0xF8) == 0xF0) n = 4;
            }
            ch[n] = 0;
            nvgText(vg, ix + iconSize / 2, iy + iconSize / 2, ch, nullptr);
        }
    }

    if (passive) nvgGlobalAlpha(vg, 1.0f);

    // NeedsAttention: accent underline.
    if (info.status == "NeedsAttention") {
        nvgBeginPath(vg);
        nvgRect(vg, ix, iy + iconSize, iconSize, 2.0f);
        nvgFillColor(vg, theme_.accent);
        nvgFill(vg);
    }
}

bool SniIconWidget::handleClick(int x, int y, uint32_t button) {
    // SNI Activate/ContextMenu expect global screen coordinates for popup
    // placement. bezel does not track wl_output geometry (logical height), so
    // exact global coords are unavailable. The panel is full-width and
    // bottom-anchored, so the passed panel-local x equals the global x; y is a
    // panel-local (0..HEIGHT) value and should be offset by (outputHeight -
    // HEIGHT) for a true global y, which is left as a documented approximation
    // pending output-geometry tracking.
    if (button == BTN_LEFT) {
        if (item_.info().itemIsMenu) {
            item_.contextMenu(x, y);
        } else {
            item_.activate(x, y);
        }
        return true;
    } else if (button == BTN_RIGHT) {
        item_.contextMenu(x, y);
        return true;
    }
    return false;
}

void SniIconWidget::handleHover(int /*x*/, int /*y*/) {
    if (!hovered_) {
        hovered_ = true;
        if (showTooltip_) {
            showTooltip_(x_, item_.info().title);
        }
    }
}

void SniIconWidget::clearHover() {
    if (hovered_) {
        hovered_ = false;
        if (dismissTooltip_) dismissTooltip_();
    }
}

void SniIconWidget::setTooltipCallbacks(std::function<void(int, const std::string&)> show,
                                         std::function<void()> dismiss) {
    showTooltip_ = std::move(show);
    dismissTooltip_ = std::move(dismiss);
}
