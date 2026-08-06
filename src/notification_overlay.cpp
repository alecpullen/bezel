#include "notification_overlay.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

static constexpr int PANEL_HEIGHT = 48;
static constexpr int ACTION_BUTTON_H = 24;
static constexpr int ACTION_BUTTON_PAD = 10;

// The header is fixed (verbatim from the plan) and has no member for the font
// handle or a hovered action key. Keep the font handle file-local; it is set
// once the FontCache is created in handle_configure.
static int g_overlayFont = -1;

static float measureTextWidth(NVGcontext* vg, int font, float size, const std::string& text) {
    if (font < 0 || !vg) return 0.0f;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    return bounds[2] - bounds[0];
}

static std::string truncateText(NVGcontext* vg, int font, float size,
                                const std::string& text, float maxW) {
    if (font < 0 || !vg) return text;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    if (bounds[2] - bounds[0] <= maxW) return text;
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        std::string t = out + "...";
        nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] <= maxW) return t;
    }
    return "...";
}

static std::vector<std::string> wrapText(NVGcontext* vg, int font, float size,
                                         const std::string& text, float maxW) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    if (font < 0 || !vg) {
        lines.push_back(text);
        return lines;
    }
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    std::string cur;
    std::string word;
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            if (!word.empty()) {
                std::string test = cur.empty() ? word : cur + " " + word;
                float bounds[4];
                nvgTextBounds(vg, 0, 0, test.c_str(), nullptr, bounds);
                if (bounds[2] - bounds[0] > maxW && !cur.empty()) {
                    lines.push_back(cur);
                    cur = word;
                } else {
                    cur = test;
                }
                word.clear();
            }
            if (c == '\n' && !cur.empty()) {
                lines.push_back(cur);
                cur.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        std::string test = cur.empty() ? word : cur + " " + word;
        float bounds[4];
        nvgTextBounds(vg, 0, 0, test.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] > maxW && !cur.empty()) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = test;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

NotificationOverlay::NotificationOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                                         zwlr_layer_shell_v1* shell, wl_output* output, int scale,
                                         NotificationService& service)
    : egl_(egl), theme_(theme), compositor_(compositor), shell_(shell),
      output_(output), scale_(scale > 0 ? scale : 1), service_(service) {
    createSurface();
    subId_ = service_.subscribe([this] { dirty_ = true; });
}

NotificationOverlay::~NotificationOverlay() {
    if (subId_ >= 0) service_.unsubscribe(subId_);
    destroySurface();
}

void NotificationOverlay::setOutput(wl_output* output, int scale) {
    if (output == output_ && scale == scale_) return;
    output_ = output;
    scale_ = scale > 0 ? scale : 1;
    destroySurface();
    createSurface();
}

void NotificationOverlay::createSurface() {
    surface_ = wl_compositor_create_surface(compositor_);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "notification_overlay");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_surface_, width_, height_);
    int bottomMargin = (int)((PANEL_HEIGHT + theme_.gapItem) * scale_);
    zwlr_layer_surface_v1_set_margin(layer_surface_, 0, 0, bottomMargin, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);

    wl_surface_commit(surface_);

    configured_ = false;
    dirty_ = true;
}

void NotificationOverlay::destroySurface() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_)                    wl_egl_window_destroy(egl_window_);
    if (layer_surface_)                 zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_)                       wl_surface_destroy(surface_);
    renderer_.reset();
    fontCache_.reset();
    iconLoader_.reset();
    surface_       = nullptr;
    layer_surface_ = nullptr;
    egl_window_    = nullptr;
    egl_surface_   = EGL_NO_SURFACE;
    configured_    = false;
}

int NotificationOverlay::computeWidth() const {
    return MAX_WIDTH;
}

int NotificationOverlay::computeHeight(const std::vector<ToastLayout>& layouts) const {
    if (layouts.empty()) return 0;
    int h = 0;
    for (size_t i = 0; i < layouts.size(); ++i) {
        h += layouts[i].h;
        if (i + 1 < layouts.size()) h += (int)theme_.gapItem;
    }
    return h;
}

std::vector<NotificationOverlay::ToastLayout> NotificationOverlay::computeLayouts(int width) const {
    std::vector<ToastLayout> layouts;
    const auto& notifs = service_.notifications();
    NVGcontext* vg = renderer_ ? renderer_->ctx() : nullptr;
    int font = g_overlayFont;

    int pad = (int)theme_.panelPad;
    int textX = pad + ICON_SIZE + (int)theme_.gapItem;
    int textW = width - textX - pad;
    if (textW < 10) textW = 10;
    float lineH = theme_.labelPrimaryPx * 1.5f;

    int count = 0;
    for (auto it = notifs.rbegin(); it != notifs.rend() && count < MAX_VISIBLE; ++it, ++count) {
        const Notification& n = it->second;
        ToastLayout tl;
        tl.notification = &n;
        tl.w = width;
        tl.expanded = (hoverToast_ == (int)n.id);

        float summaryH = lineH;
        float bodyH = 0.0f;
        if (!n.body.empty()) {
            if (tl.expanded) {
                auto lines = wrapText(vg, font, theme_.labelSecondaryPx, n.body, (float)textW);
                bodyH = (float)lines.size() * lineH;
            } else {
                bodyH = lineH;
            }
        }

        float textH = summaryH + bodyH;
        float mainH = std::max((float)ICON_SIZE, textH);

        float actionsH = 0.0f;
        if (!n.actions.empty()) {
            actionsH = (float)ACTION_BUTTON_H + (float)theme_.gapItem;
        }

        tl.h = (int)(pad + mainH + actionsH + pad);

        if (!n.actions.empty()) {
            int bx = textX;
            for (const auto& [key, label] : n.actions) {
                float lw = measureTextWidth(vg, font, theme_.labelSecondaryPx, label);
                int bw = (int)lw + 2 * ACTION_BUTTON_PAD;
                tl.actionButtons.emplace_back(key, std::make_pair(bx, bw));
                bx += bw + (int)theme_.gapItem;
            }
        }

        layouts.push_back(tl);
    }

    int y = 0;
    for (auto& tl : layouts) {
        tl.y = y;
        y += tl.h + (int)theme_.gapItem;
    }
    return layouts;
}

void NotificationOverlay::updateGeometry() {
    int w = computeWidth();
    auto layouts = computeLayouts(w);
    int h = computeHeight(layouts);
    if (h != height_ || w != width_) {
        width_  = w;
        height_ = h;
        if (layer_surface_) {
            zwlr_layer_surface_v1_set_size(layer_surface_, width_, height_);
            wl_surface_commit(surface_);
        }
    }
}

void NotificationOverlay::drawToast(const ToastLayout& toast, NVGcontext* vg, int fontHandle) {
    const Notification& n = *toast.notification;
    int x = toast.x, y = toast.y, w = toast.w, h = toast.h;
    int pad = (int)theme_.panelPad;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, (float)x, (float)y, (float)w, (float)h, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBg);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, (float)x + 0.5f, (float)y + 0.5f, (float)w - 1.0f, (float)h - 1.0f,
                   theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    int textX = x + pad + ICON_SIZE + (int)theme_.gapItem;
    int textW = w - pad - ICON_SIZE - (int)theme_.gapItem - pad;

    int icon = iconLoader_ ? iconLoader_->get(n.app_icon) : -1;
    if (icon >= 0) {
        NVGpaint ip = nvgImagePattern(vg, (float)(x + pad), (float)(y + pad),
                                      (float)ICON_SIZE, (float)ICON_SIZE, 0, icon, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, (float)(x + pad), (float)(y + pad), (float)ICON_SIZE, (float)ICON_SIZE);
        nvgFillPaint(vg, ip);
        nvgFill(vg);
    }

    if (fontHandle < 0) return;

    float lineH = theme_.labelPrimaryPx * 1.5f;
    float ty = (float)y + pad;

    nvgFontFaceId(vg, fontHandle);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, theme_.textPrimary);
    std::string summary = truncateText(vg, fontHandle, theme_.labelPrimaryPx, n.summary, (float)textW);
    nvgText(vg, (float)textX, ty, summary.c_str(), nullptr);
    ty += lineH;

    if (!n.body.empty()) {
        nvgFontSize(vg, theme_.labelSecondaryPx);
        nvgFillColor(vg, theme_.textSecondary);
        if (toast.expanded) {
            auto lines = wrapText(vg, fontHandle, theme_.labelSecondaryPx, n.body, (float)textW);
            for (const auto& line : lines) {
                nvgText(vg, (float)textX, ty, line.c_str(), nullptr);
                ty += lineH;
            }
        } else {
            std::string body = truncateText(vg, fontHandle, theme_.labelSecondaryPx, n.body, (float)textW);
            nvgText(vg, (float)textX, ty, body.c_str(), nullptr);
            ty += lineH;
        }
    }

    if (!n.actions.empty()) {
        int by = (int)ty + (int)theme_.gapItem;
        for (const auto& [key, rect] : toast.actionButtons) {
            int bx = rect.first;
            int bw = rect.second;
            std::string label = key;
            for (const auto& [akey, alabel] : n.actions) {
                if (akey == key) { label = alabel; break; }
            }
            nvgBeginPath(vg);
            nvgRoundedRect(vg, (float)bx, (float)by, (float)bw, (float)ACTION_BUTTON_H,
                           theme_.radiusControl * 0.5f);
            nvgFillColor(vg, theme_.accentTint);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, (float)bx + 0.5f, (float)by + 0.5f, (float)bw - 1.0f,
                           (float)ACTION_BUTTON_H - 1.0f, theme_.radiusControl * 0.5f);
            nvgStrokeColor(vg, theme_.borderAccent);
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
            nvgFontSize(vg, theme_.labelSecondaryPx);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, theme_.textPrimary);
            nvgText(vg, (float)(bx + ACTION_BUTTON_PAD), (float)(by + ACTION_BUTTON_H / 2),
                    label.c_str(), nullptr);
        }
    }
}

void NotificationOverlay::render() {
    if (!configured_) return;
    if (!renderer_) return;
    if (!dirty_) return;
    dirty_ = false;

    updateGeometry();

    renderer_->beginFrame(width_, height_);
    renderer_->clear();
    NVGcontext* vg = renderer_->ctx();
    int fontH = fontCache_ ? fontCache_->loadSans() : -1;
    auto layouts = computeLayouts(width_);
    for (const auto& tl : layouts) {
        drawToast(tl, vg, fontH);
    }
    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void NotificationOverlay::handlePointerMotion(int x, int y) {
    auto layouts = computeLayouts(width_);
    int hover = -1;
    for (const auto& tl : layouts) {
        if (x >= tl.x && x < tl.x + tl.w && y >= tl.y && y < tl.y + tl.h) {
            hover = (int)tl.notification->id;
            break;
        }
    }
    if (hover != hoverToast_) {
        hoverToast_ = hover;
        dirty_ = true;
    }
}

void NotificationOverlay::handlePointerButton(int x, int y, uint32_t button) {
    if (button != BTN_LEFT) return;
    auto layouts = computeLayouts(width_);
    for (const auto& tl : layouts) {
        if (x < tl.x || x >= tl.x + tl.w || y < tl.y || y >= tl.y + tl.h) continue;
        int by = tl.y + tl.h - (int)theme_.panelPad - ACTION_BUTTON_H;
        for (const auto& [key, rect] : tl.actionButtons) {
            int bx = rect.first;
            int bw = rect.second;
            if (x >= bx && x < bx + bw && y >= by && y < by + ACTION_BUTTON_H) {
                service_.invokeAction(tl.notification->id, key);
                return;
            }
        }
        service_.closeNotification(tl.notification->id, 2);
        return;
    }
}

void NotificationOverlay::handlePointerLeave() {
    if (hoverToast_ != -1) {
        hoverToast_ = -1;
        dirty_ = true;
    }
}

void NotificationOverlay::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                           uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<NotificationOverlay*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    int width  = w > 0 ? (int)w : self->width_;
    int height = h > 0 ? (int)h : self->height_;

    if (!self->egl_window_) {
        self->egl_window_  = wl_egl_window_create(self->surface_, width, height);
        self->egl_surface_ = eglCreateWindowSurface(self->egl_.display(), self->egl_.config(),
                                                     (EGLNativeWindowType)self->egl_window_, nullptr);
        self->renderer_ = std::make_unique<Renderer>(self->theme_, self->egl_);
        if (!self->renderer_->init(self->egl_surface_)) {
            std::fprintf(stderr, "NotificationOverlay: renderer init failed\n");
            self->renderer_.reset();
            return;
        }
        self->fontCache_   = std::make_unique<FontCache>(self->renderer_->ctx());
        self->iconLoader_  = std::make_unique<IconLoader>(self->renderer_->ctx());
        g_overlayFont      = self->fontCache_->loadSans();
    } else {
        wl_egl_window_resize(self->egl_window_, width, height, 0, 0);
    }

    self->configured_ = true;
    self->dirty_      = true;
    self->render();
}

void NotificationOverlay::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}
