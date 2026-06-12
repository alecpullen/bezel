#include "notification_overlay.hpp"
#include "egl.hpp"
#include <GLES2/gl2.h>
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <algorithm>
#include <cstdio>

static constexpr float PAD      = 10.0f;
static constexpr float LINE_H_S = 15.0f; // labelSecondaryPx * ~1.4 leading
static constexpr float TWO_LINES = 2.0f * LINE_H_S;

// Returns at most 2 non-default {key, label} pairs from a Notification's actions array.
static std::vector<std::pair<std::string, std::string>>
visibleActions(const Notification& n) {
    std::vector<std::pair<std::string, std::string>> result;
    const auto& a = n.actions;
    for (size_t i = 0; i + 1 < a.size() && result.size() < 2; i += 2) {
        if (a[i] == "default") continue;
        result.push_back({a[i], a[i + 1]});
    }
    return result;
}

NotificationOverlay::NotificationOverlay(Egl& egl, Theme& theme,
                                          wl_compositor* compositor,
                                          zwlr_layer_shell_v1* shell,
                                          wl_output* output,
                                          NotificationService& service)
    : egl_(egl), theme_(theme), service_(service)
{
    surface_ = wl_compositor_create_surface(compositor);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell, surface_, output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "notification");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_margin(layer_surface_,
        0, MARGIN, PANEL_MARGIN + MARGIN, 0);

    int w = TOAST_W + 2 * MARGIN;
    int h = totalHeight();
    zwlr_layer_surface_v1_set_size(layer_surface_, w, h);
    wl_surface_commit(surface_);
}

NotificationOverlay::~NotificationOverlay() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_)                    wl_egl_window_destroy(egl_window_);
    if (layer_surface_)                 zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_)                       wl_surface_destroy(surface_);
}

int NotificationOverlay::cardHeight(int idx) const {
    int base = (idx == hoverIdx_) ? expandedHeight(idx) : TOAST_H;
    auto& active = service_.active();
    if (idx >= 0 && idx < (int)active.size() && !visibleActions(active[idx]).empty())
        base += BTN_AREA_H;
    return base;
}

int NotificationOverlay::totalHeight() const {
    auto& active = service_.active();
    if (active.empty()) return MARGIN * 2;
    int h = MARGIN;
    for (int i = 0; i < (int)active.size(); ++i) {
        h += cardHeight(i);
        if (i + 1 < (int)active.size()) h += TOAST_GAP;
    }
    h += MARGIN;
    return h;
}

int NotificationOverlay::expandedHeight(int idx) const {
    if (!renderer_ || fontHandle_ < 0) return TOAST_H;
    auto& active = service_.active();
    if (idx < 0 || idx >= (int)active.size()) return TOAST_H;
    const std::string& body = active[idx].body;
    if (body.empty()) return TOAST_H;

    NVGcontext* vg = renderer_->ctx();
    float bodyMaxW = TOAST_W - ICON_SIZE - PAD * 3.0f;
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.labelSecondaryPx);
    float bounds[4] = {};
    nvgTextBoxBounds(vg, 0, 0, bodyMaxW, body.c_str(), nullptr, bounds);
    float bodyH = bounds[3] - bounds[1];
    if (bodyH <= TWO_LINES) return TOAST_H;
    return TOAST_H + static_cast<int>(bodyH - TWO_LINES + 0.5f);
}

void NotificationOverlay::resize(int w, int h) {
    if (w == width_ && h == height_) return;
    width_  = w;
    height_ = h;
    if (egl_window_) wl_egl_window_resize(egl_window_, w, h, 0, 0);
    zwlr_layer_surface_v1_set_size(layer_surface_, w, h);
    wl_surface_commit(surface_);
}

void NotificationOverlay::update() {
    int newH = totalHeight();
    int newW = TOAST_W + 2 * MARGIN;
    if (newW != width_ || newH != height_) {
        width_  = newW;
        height_ = newH;
        if (egl_window_) wl_egl_window_resize(egl_window_, newW, newH, 0, 0);
        zwlr_layer_surface_v1_set_size(layer_surface_, newW, newH);
        wl_surface_commit(surface_);
    }
    dirty_ = true;
}

void NotificationOverlay::render() {
    if (!renderer_ || !dirty_) return;
    dirty_ = false;

    renderer_->beginFrame(width_, height_);

    // Clear to fully transparent so gaps between toasts show the desktop
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    NVGcontext* vg = renderer_->ctx();
    auto& active = service_.active();

    btnRects_.assign(active.size(), {});
    float tileY = static_cast<float>(MARGIN);
    for (int i = 0; i < (int)active.size(); ++i) {
        const auto& n = active[i];
        float tileH = static_cast<float>(cardHeight(i));
        float tileX = static_cast<float>(MARGIN);
        float tileW = static_cast<float>(TOAST_W);

        // Card background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tileX, tileY, tileW, tileH, theme_.radiusTile);
        nvgFillColor(vg, theme_.panelBgElevated);
        nvgFill(vg);

        // Card border
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tileX + 0.5f, tileY + 0.5f, tileW - 1.0f, tileH - 1.0f,
                       theme_.radiusTile);
        nvgStrokeColor(vg, theme_.borderHairline);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);

        // Icon
        float iconX = tileX + PAD;
        float iconY = tileY + (tileH - ICON_SIZE) / 2.0f;
        bool drewIcon = false;
        if (iconLoader_ && !n.app_icon.empty()) {
            int img = iconLoader_->get(n.app_icon);
            if (img >= 0) {
                NVGpaint paint = nvgImagePattern(vg, iconX, iconY, ICON_SIZE, ICON_SIZE,
                                                 0.0f, img, 1.0f);
                nvgBeginPath(vg);
                nvgRect(vg, iconX, iconY, ICON_SIZE, ICON_SIZE);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
                drewIcon = true;
            }
        }
        if (!drewIcon) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, iconX, iconY, ICON_SIZE, ICON_SIZE, 4.0f);
            NVGcolor ph = theme_.textMuted;
            ph.a *= 0.4f;
            nvgFillColor(vg, ph);
            nvgFill(vg);
        }

        // Text area
        float textX   = tileX + PAD + ICON_SIZE + 8.0f;
        float textMaxW = tileW - (textX - tileX) - PAD;

        if (fontHandle_ >= 0) {
            nvgFontFaceId(vg, fontHandle_);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

            // Summary
            float summaryY = tileY + PAD;
            nvgFontSize(vg, theme_.labelPrimaryPx);
            nvgFillColor(vg, theme_.textPrimary);
            nvgScissor(vg, textX, summaryY, textMaxW, theme_.labelPrimaryPx + 4.0f);
            nvgText(vg, textX, summaryY, n.summary.empty() ? n.app_name.c_str()
                                                            : n.summary.c_str(), nullptr);
            nvgResetScissor(vg);

            // Body
            auto acts = visibleActions(n);
            float btnAreaH = acts.empty() ? 0.0f : static_cast<float>(BTN_AREA_H);
            if (!n.body.empty()) {
                float bodyY = summaryY + theme_.labelPrimaryPx + 4.0f;
                float bodyAreaH = (i == hoverIdx_)
                    ? (tileH - PAD - (bodyY - tileY) - PAD - btnAreaH)
                    : TWO_LINES;
                nvgFontSize(vg, theme_.labelSecondaryPx);
                nvgFillColor(vg, theme_.textSecondary);
                nvgScissor(vg, textX, bodyY, textMaxW, bodyAreaH + 2.0f);
                nvgTextBox(vg, textX, bodyY, textMaxW, n.body.c_str(), nullptr);
                nvgResetScissor(vg);
            }

            // Action buttons
            if (!acts.empty()) {
                nvgFontFaceId(vg, fontHandle_);
                nvgFontSize(vg, theme_.labelSecondaryPx);
                float btnY = tileY + tileH - static_cast<float>(BTN_H) - static_cast<float>(BTN_GAP) / 2.0f;
                float btnX = textX;
                for (int b = 0; b < (int)acts.size(); ++b) {
                    float bounds[4] = {};
                    float textW = nvgTextBounds(vg, 0, 0, acts[b].second.c_str(), nullptr, bounds);
                    float btnW = std::clamp(textW + 2.0f * BTN_PAD, 60.0f, 140.0f);

                    bool btnHovered = (i == hoverIdx_ && b == hoverBtnIdx_);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, btnX, btnY, btnW, BTN_H, theme_.radiusTile / 2.0f);
                    nvgFillColor(vg, btnHovered ? theme_.accentTint : theme_.panelBg);
                    nvgFill(vg);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, btnX + 0.5f, btnY + 0.5f, btnW - 1.0f, BTN_H - 1.0f,
                                   theme_.radiusTile / 2.0f);
                    nvgStrokeColor(vg, btnHovered ? theme_.accent : theme_.borderHairline);
                    nvgStrokeWidth(vg, 1.0f);
                    nvgStroke(vg);
                    nvgFillColor(vg, theme_.accent);
                    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                    nvgText(vg, btnX + btnW / 2.0f, btnY + BTN_H / 2.0f,
                            acts[b].second.c_str(), nullptr);
                    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

                    btnRects_[i].push_back({btnX, btnY, btnW});
                    btnX += btnW + 6.0f;
                }
            }
        }

        tileY += tileH + TOAST_GAP;
    }

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void NotificationOverlay::handlePointerMotion(int x, int y) {
    auto& active = service_.active();
    int newHover = -1;
    float tileY = static_cast<float>(MARGIN);
    for (int i = 0; i < (int)active.size(); ++i) {
        float tileH = static_cast<float>(cardHeight(i));
        if (y >= tileY && y < tileY + tileH &&
            x >= MARGIN && x < MARGIN + TOAST_W) {
            newHover = i;
            break;
        }
        tileY += tileH + TOAST_GAP;
    }

    int newBtnHover = -1;
    if (newHover >= 0 && newHover < (int)btnRects_.size()) {
        const auto& btns = btnRects_[newHover];
        for (int b = 0; b < (int)btns.size(); ++b) {
            if (static_cast<float>(y) >= btns[b].y &&
                static_cast<float>(y) < btns[b].y + BTN_H &&
                static_cast<float>(x) >= btns[b].x &&
                static_cast<float>(x) < btns[b].x + btns[b].w) {
                newBtnHover = b;
                break;
            }
        }
    }

    bool changed = (newHover != hoverIdx_) || (newBtnHover != hoverBtnIdx_);
    hoverIdx_    = newHover;
    hoverBtnIdx_ = newBtnHover;
    if (changed) update();
}

void NotificationOverlay::handlePointerLeave() {
    if (hoverIdx_ != -1 || hoverBtnIdx_ != -1) {
        hoverIdx_    = -1;
        hoverBtnIdx_ = -1;
        update();
    }
}

void NotificationOverlay::handlePointerButton(int x, int y, uint32_t button) {
    if (button != BTN_LEFT) return;
    auto& active = service_.active();
    float tileY = static_cast<float>(MARGIN);
    for (int i = 0; i < (int)active.size(); ++i) {
        float tileH = static_cast<float>(cardHeight(i));
        if (y >= tileY && y < tileY + tileH &&
            x >= MARGIN && x < MARGIN + TOAST_W) {
            // Check if click landed on an action button
            if (i < (int)btnRects_.size()) {
                const auto& btns = btnRects_[i];
                for (int b = 0; b < (int)btns.size(); ++b) {
                    if (static_cast<float>(y) >= btns[b].y &&
                        static_cast<float>(y) < btns[b].y + BTN_H &&
                        static_cast<float>(x) >= btns[b].x &&
                        static_cast<float>(x) < btns[b].x + btns[b].w) {
                        auto acts = visibleActions(active[i]);
                        if (b < (int)acts.size()) {
                            uint32_t id = active[i].id;
                            service_.emitActionInvoked(id, acts[b].first);
                            service_.closeNotification(id, 2);
                        }
                        return;
                    }
                }
            }
            service_.closeNotification(active[i].id, 2); // reason 2 = user dismissed
            return;
        }
        tileY += tileH + TOAST_GAP;
    }
}

void NotificationOverlay::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                            uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<NotificationOverlay*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    int width  = w > 0 ? (int)w : (TOAST_W + 2 * MARGIN);
    int height = h > 0 ? (int)h : self->totalHeight();

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
        self->fontCache_ = std::make_unique<FontCache>(self->renderer_->ctx());
        self->fontHandle_ = self->fontCache_->loadSans();
        self->iconLoader_ = std::make_unique<IconLoader>(self->renderer_->ctx());
    } else {
        wl_egl_window_resize(self->egl_window_, width, height, 0, 0);
    }

    self->width_  = width;
    self->height_ = height;
    self->dirty_  = true;
    self->render();
}

void NotificationOverlay::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}
