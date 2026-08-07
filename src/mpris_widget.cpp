#include "mpris_widget.hpp"
#include "renderer.hpp"
#include <nanovg.h>
#include <algorithm>
#include <cstring>

MprisWidget::MprisWidget(const Theme& theme, NVGcontext* ctx, int fontHandle, MprisService& svc)
    : theme_(theme), svc_(svc), ctx_(ctx), fontHandle_(fontHandle) {
    titleLabel_ = std::make_unique<Label>(ctx_, fontHandle_);
    titleLabel_->setFontSize(theme_.labelPrimaryPx);
    titleLabel_->setColor(theme_.textPrimary);
    titleLabel_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    artistLabel_ = std::make_unique<Label>(ctx_, fontHandle_);
    artistLabel_->setFontSize(theme_.labelSecondaryPx);
    artistLabel_->setColor(theme_.textSecondary);
    artistLabel_->setAlign(NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

    lastInfo_ = svc_.info();
    rebuildLabels();

    subId_ = svc_.subscribe([this] {
        lastInfo_ = svc_.info();
        labelsDirty_ = true;
        if (requestRedraw_) requestRedraw_();
    });
}

MprisWidget::~MprisWidget() {
    if (subId_ >= 0) svc_.unsubscribe(subId_);
}

int MprisWidget::preferredWidth() const {
    if (lastInfo_.status == MprisInfo::Status::Stopped && lastInfo_.title.empty()) {
        return 0;
    }
    // art + gap + text block + gap + play/pause glyph
    int textW = std::max(titleLabel_->preferredWidth(), artistLabel_->preferredWidth());
    if (textW > MAX_TEXT_W) textW = MAX_TEXT_W;
    return ART_SIZE + (int)theme_.gapItem + textW + (int)theme_.gapItem + (int)theme_.iconInline;
}

int MprisWidget::preferredHeight() const { return 24; }

void MprisWidget::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
    int textX = x + ART_SIZE + (int)theme_.gapItem;
    titleLabel_->layout(textX, y + 2, w - ART_SIZE - (int)theme_.gapItem, h / 2);
    artistLabel_->layout(textX, y + h / 2, w - ART_SIZE - (int)theme_.gapItem, h / 2);
}

void MprisWidget::rebuildLabels() const {
    titleLabel_->setText(lastInfo_.title);
    artistLabel_->setText(lastInfo_.artist);
    labelsDirty_ = false;
}

void MprisWidget::render(const Renderer& renderer) const {
    if (lastInfo_.status == MprisInfo::Status::Stopped && lastInfo_.title.empty()) return;
    NVGcontext* vg = renderer.ctx();
    if (!vg) return;
    if (labelsDirty_) rebuildLabels();

    int x = x_, y = y_;

    // Art thumbnail (or placeholder)
    int artH = svc_.artHandle();
    if (artH > 0) {
        NVGpaint ip = nvgImagePattern(vg, (float)x, (float)y,
                                       (float)ART_SIZE, (float)ART_SIZE, 0, artH, 1.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (float)x, (float)y, (float)ART_SIZE, (float)ART_SIZE, 3.0f);
        nvgFillPaint(vg, ip);
        nvgFill(vg);
    } else {
        // Placeholder: accent-tinted square + first letter of title
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (float)x, (float)y, (float)ART_SIZE, (float)ART_SIZE, 3.0f);
        nvgFillColor(vg, theme_.accentTint);
        nvgFill(vg);
        if (!lastInfo_.title.empty()) {
            nvgFontFaceId(vg, fontHandle_);
            nvgFontSize(vg, theme_.labelPrimaryPx);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, theme_.textPrimary);
            char ch[8] = {0};
            // First UTF-8 codepoint (truncate to first 4 bytes safely)
            std::strncpy(ch, lastInfo_.title.c_str(), sizeof(ch) - 1);
            int n = 1;
            if ((ch[0] & 0x80) != 0) {
                if ((ch[0] & 0xE0) == 0xC0) n = 2;
                else if ((ch[0] & 0xF0) == 0xE0) n = 3;
                else if ((ch[0] & 0xF8) == 0xF0) n = 4;
            }
            ch[n] = 0;
            nvgText(vg, (float)(x + ART_SIZE / 2), (float)(y + ART_SIZE / 2), ch, nullptr);
        }
    }

    // Title and artist labels
    titleLabel_->render(renderer);
    artistLabel_->render(renderer);

    // Play/pause glyph at right
    const char* glyph;
    if (lastInfo_.status == MprisInfo::Status::Playing) {
        glyph = "\xef\x81\x8c"; // \uf04c pause
    } else {
        glyph = "\xef\x81\x8b"; // \uf04b play
    }
    int gx = x + w_ - (int)theme_.iconInline;
    int gy = y + h_ / 2;
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.iconInline);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, theme_.textSecondary);
    nvgText(vg, (float)gx, (float)gy, glyph, nullptr);
}

void MprisWidget::setCallback(std::function<void()> requestRedraw) {
    requestRedraw_ = std::move(requestRedraw);
}
