#include "window_list.hpp"
#include "icon_loader.hpp"
#include <nanovg.h>
#include <algorithm>

static constexpr float ICON_SIZE   = 16.0f;
static constexpr float ICON_PAD    = 6.0f;  // gap from button edge to icon left
static constexpr float ICON_TEXT_GAP = 4.0f; // gap between icon and text

WindowList::WindowList(const Theme& theme, int fontHandle, NVGcontext* ctx,
                       wl_output* output, ToplevelService& service)
    : theme_(theme), fontHandle_(fontHandle), output_(output), service_(service) {
    if (ctx)
        iconLoader_ = std::make_unique<IconLoader>(ctx);
    subId_ = service_.subscribe([this] {
        if (callback_) callback_();
    });
}

WindowList::~WindowList() {
    if (subId_ != -1)
        service_.unsubscribe(subId_);
}

void WindowList::setCallback(std::function<void()> cb) {
    callback_ = std::move(cb);
}

std::vector<const ToplevelInfo*> WindowList::visibleToplevels() const {
    std::vector<const ToplevelInfo*> visible;
    for (const auto& t : service_.toplevels()) {
        if (t->is_closed()) continue;
        const auto& info = t->info();
        if (info.minimized) continue;
        if (!info.outputs.empty() && !info.is_on_output(output_)) continue;
        visible.push_back(&info);
    }
    return visible;
}

int WindowList::preferredWidth() const {
    auto visible = visibleToplevels();
    if (visible.empty()) return 0;

    int count = (int)visible.size();
    static constexpr int MIN_BTN_W = 60;
    static constexpr int MAX_BTN_W = 200;
    static constexpr int ABSOLUTE_MAX_VISIBLE = 20;

    int maxVisible = std::min(ABSOLUTE_MAX_VISIBLE, std::max(1, w_ / MIN_BTN_W));
    count = std::min(count, maxVisible);

    float btnW = std::min((float)w_ / (float)count, (float)MAX_BTN_W);
    return static_cast<int>(count * btnW);
}

void WindowList::render(const Renderer& renderer) const {
    auto visible = visibleToplevels();
    if (visible.empty() || w_ <= 0) return;

    int count = (int)visible.size();
    int minBtnW = 60;
    int maxVisible = std::max(1, w_ / minBtnW);
    if (count > maxVisible) {
        auto activeIt = std::find_if(visible.begin(), visible.end(),
            [](const ToplevelInfo* i) { return i->activated; });
        if (activeIt != visible.end() && std::distance(visible.begin(), activeIt) >= maxVisible)
            std::swap(visible[0], *activeIt);
        visible.resize(maxVisible);
        count = maxVisible;
    }

    static constexpr float MAX_BTN_W = 200.0f;
    float btnW = std::min((float)w_ / (float)count, MAX_BTN_W);
    NVGcontext* vg = renderer.ctx();
    float curX = (float)x_;
    for (const auto* info : visible) {
        drawButton(vg, curX, btnW, *info);
        curX += btnW;
    }
}

void WindowList::drawButton(NVGcontext* vg, float btnX, float btnW,
                             const ToplevelInfo& info) const {
    float pad = 4.0f;

    if (info.activated) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, btnX + pad, (float)y_ + pad,
                       btnW - 2*pad, (float)h_ - 2*pad, theme_.radiusTile);
        nvgFillColor(vg, theme_.panelBgElevated);
        nvgFill(vg);
    }

    // App icon (if available)
    float textStartX = btnX + 8.0f;
    if (iconLoader_) {
        int img = iconLoader_->get(info.app_id);
        if (img >= 0) {
            float ix = btnX + ICON_PAD;
            float iy = (float)y_ + ((float)h_ - ICON_SIZE) / 2.0f;
            NVGpaint paint = nvgImagePattern(vg, ix, iy, ICON_SIZE, ICON_SIZE, 0.0f, img, 1.0f);
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, ICON_SIZE, ICON_SIZE);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
            textStartX = ix + ICON_SIZE + ICON_TEXT_GAP;
        }
    }

    // Title text, vertically centred, ellipsized
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, info.activated ? theme_.textPrimary : theme_.textSecondary);
    float textEndX = btnX + btnW - 8.0f;
    drawEllipsized(vg, textStartX, (float)y_ + (float)h_ / 2.0f,
                   textEndX - textStartX, info.title);

    // 2px accent underline at bottom for active window
    if (info.activated) {
        nvgBeginPath(vg);
        nvgRect(vg, btnX + pad, (float)y_ + (float)h_ - 2.0f, btnW - 2*pad, 2.0f);
        nvgFillColor(vg, theme_.accent);
        nvgFill(vg);
    }
}

void WindowList::drawEllipsized(NVGcontext* vg, float x, float y, float maxW,
                                 const std::string& text) const {
    if (text.empty() || maxW <= 0) return;

    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    if (bounds[2] - bounds[0] <= maxW) {
        nvgText(vg, x, y, text.c_str(), nullptr);
        return;
    }

    std::string truncated = text;
    while (!truncated.empty()) {
        std::string candidate = truncated + "\xe2\x80\xa6"; // U+2026 ellipsis
        nvgTextBounds(vg, 0, 0, candidate.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] <= maxW) {
            nvgText(vg, x, y, candidate.c_str(), nullptr);
            return;
        }
        // Pop one UTF-8 codepoint from the end
        truncated.pop_back();
        while (!truncated.empty() && (truncated.back() & 0xC0) == 0x80)
            truncated.pop_back();
    }
}
