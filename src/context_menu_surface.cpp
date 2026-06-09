#include "context_menu_surface.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <cstdio>

static constexpr int PANEL_HEIGHT = 48;

ContextMenuSurface::ContextMenuSurface(Egl& egl, Theme& theme, wl_compositor* compositor,
                                        zwlr_layer_shell_v1* shell, wl_output* output,
                                        int anchor_x, bool pinned,
                                        zwlr_foreign_toplevel_handle_v1* handle,
                                        std::string app_id,
                                        std::function<void()> on_close,
                                        std::function<void()> on_pin)
    : egl_(egl), theme_(theme), pinned_(pinned), handle_(handle),
      app_id_(std::move(app_id)), on_close_(std::move(on_close)), on_pin_(std::move(on_pin))
{
    surface_ = wl_compositor_create_surface(compositor);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell, surface_, output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "context_menu");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_size(layer_surface_, MENU_W, MENU_H);
    zwlr_layer_surface_v1_set_margin(layer_surface_, 0, 0, PANEL_HEIGHT, anchor_x);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);

    wl_surface_commit(surface_);
}

ContextMenuSurface::~ContextMenuSurface() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_)                    wl_egl_window_destroy(egl_window_);
    if (layer_surface_)                 zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_)                       wl_surface_destroy(surface_);
}

void ContextMenuSurface::handlePointerMotion(int x, int y) {
    int item = (x >= 0 && x < MENU_W && y >= 0 && y < MENU_H) ? y / ITEM_H : -1;
    if (item != hoverItem_) {
        hoverItem_ = item;
        render();
    }
}

void ContextMenuSurface::handlePointerButton(int x, int y, uint32_t button) {
    if (button != BTN_LEFT) return;
    if (x < 0 || x >= MENU_W || y < 0 || y >= MENU_H) return;
    int item = y / ITEM_H;
    if (item == 0 && on_close_) on_close_();
    if (item == 1 && on_pin_) on_pin_();
}

void ContextMenuSurface::render() {
    if (!renderer_) return;

    renderer_->beginFrame(MENU_W, MENU_H);

    NVGcontext* vg = renderer_->ctx();

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, (float)MENU_W, (float)MENU_H, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBgElevated);
    nvgFill(vg);

    if (hoverItem_ == 0) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, 0, 0, (float)MENU_W, (float)ITEM_H,
                              theme_.radiusControl, theme_.radiusControl, 0, 0);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 25));
        nvgFill(vg);
    } else if (hoverItem_ == 1) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, 0, (float)ITEM_H, (float)MENU_W, (float)ITEM_H,
                              0, 0, theme_.radiusControl, theme_.radiusControl);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 25));
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5f, 0.5f, (float)MENU_W - 1.0f, (float)MENU_H - 1.0f, theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, 0, (float)ITEM_H);
    nvgLineTo(vg, (float)MENU_W, (float)ITEM_H);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    int fontH = fontCache_ ? fontCache_->loadSans() : -1;
    if (fontH >= 0) {
        nvgFontFaceId(vg, fontH);
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        nvgFillColor(vg, handle_ ? theme_.textPrimary : theme_.textMuted);
        nvgText(vg, theme_.panelPad, (float)ITEM_H * 0.5f, "Close window", nullptr);

        nvgFillColor(vg, theme_.textPrimary);
        nvgText(vg, theme_.panelPad, (float)ITEM_H * 1.5f,
                pinned_ ? "Unpin from panel" : "Pin to panel", nullptr);
    }

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void ContextMenuSurface::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                           uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<ContextMenuSurface*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    int width  = w > 0 ? (int)w : MENU_W;
    int height = h > 0 ? (int)h : MENU_H;

    if (!self->egl_window_) {
        self->egl_window_  = wl_egl_window_create(self->surface_, width, height);
        self->egl_surface_ = eglCreateWindowSurface(self->egl_.display(), self->egl_.config(),
                                                     (EGLNativeWindowType)self->egl_window_, nullptr);
        self->renderer_ = std::make_unique<Renderer>(self->theme_, self->egl_);
        if (!self->renderer_->init(self->egl_surface_)) {
            std::fprintf(stderr, "ContextMenuSurface: renderer init failed\n");
            self->renderer_.reset();
            return;
        }
        self->fontCache_ = std::make_unique<FontCache>(self->renderer_->ctx());
    }

    self->render();
}

void ContextMenuSurface::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}
