#include "window_list.hpp"
#include "icon_loader.hpp"
#include <nanovg.h>
#include <algorithm>

static constexpr float ICON_SIZE     = 24.0f;
static constexpr float ICON_PAD      = 16.0f; // gap from button edge to icon left
static constexpr float ICON_TEXT_GAP = 10.0f; // gap between icon and text
static constexpr float BUTTON_WIDTH  = 160.0f;

WindowList::WindowList(const Theme& theme, int fontHandle, NVGcontext* ctx,
                       wl_output* output, ToplevelService& service, int panelHeight)
    : theme_(theme), fontHandle_(fontHandle), output_(output), service_(service), panelHeight_(panelHeight) {
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
    static constexpr int MAX_VISIBLE_FOR_PREF = 20;

    count = std::min(count, MAX_VISIBLE_FOR_PREF);
    return static_cast<int>(count * BUTTON_WIDTH);
}

void WindowList::render(const Renderer& renderer) const {
    auto visible = visibleToplevels();
    if (visible.empty() || w_ <= 0) return;

    int count = (int)visible.size();
    int maxVisible = std::max(1, static_cast<int>(w_ / BUTTON_WIDTH));
    if (count > maxVisible) {
        auto activeIt = std::find_if(visible.begin(), visible.end(),
            [](const ToplevelInfo* i) { return i->activated; });
        if (activeIt != visible.end() && std::distance(visible.begin(), activeIt) >= maxVisible)
            std::swap(visible[0], *activeIt);
        visible.resize(maxVisible);
        count = maxVisible;
    }

    NVGcontext* vg = renderer.ctx();
    float curX = (float)x_;
    for (const auto* info : visible) {
        drawButton(vg, curX, BUTTON_WIDTH, *info);
        curX += BUTTON_WIDTH;
    }
}

static std::string appName(const ToplevelInfo& info) {
    if (!info.app_id.empty()) return info.app_id;
    return info.title;
}

void WindowList::drawButton(NVGcontext* vg, float btnX, float btnW,
                             const ToplevelInfo& info) const {
    float padY = (panelHeight_ - h_) / 2.0f;
    float fillTop = (float)y_ - padY;

    if (info.activated) {
        nvgBeginPath(vg);
        nvgRect(vg, btnX, fillTop, btnW, (float)panelHeight_);
        nvgFillColor(vg, theme_.panelBgElevated);
        nvgFill(vg);
    }

    // App icon (if available)
    float textStartX = btnX + ICON_PAD;
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

    // App name text, vertically centred, ellipsized
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, info.activated ? theme_.textPrimary : theme_.textSecondary);
    float textEndX = btnX + btnW - ICON_PAD;
    drawEllipsized(vg, textStartX, (float)y_ + (float)h_ / 2.0f,
                   textEndX - textStartX, appName(info));

    // 2px accent underline at bottom for active window
    if (info.activated) {
        nvgBeginPath(vg);
        nvgRect(vg, btnX, fillTop + panelHeight_ - 2.0f, btnW, 2.0f);
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
