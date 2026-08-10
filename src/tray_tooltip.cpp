#include "tray_tooltip.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <cstdio>

static constexpr int PANEL_HEIGHT = 48;
static constexpr int TOOLTIP_H = 28;
static constexpr int MAX_TOOLTIP_W = 240;

TrayTooltip::TrayTooltip(Egl& egl, Theme& theme, wl_compositor* compositor,
                         zwlr_layer_shell_v1* shell, wl_output* output,
                         int scale, int anchorX, int panelTopY, const std::string& text)
    : egl_(egl), theme_(theme), compositor_(compositor), shell_(shell),
      output_(output), text_(text), scale_(scale > 0 ? scale : 1),
      anchorX_(anchorX), panelTopY_(panelTopY) {
    createSurface(anchorX, panelTopY);
}

TrayTooltip::~TrayTooltip() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_) wl_surface_destroy(surface_);
}

void TrayTooltip::createSurface(int anchorX, int panelTopY) {
    // Measure text width to size the surface.
    // We need an NVGcontext to measure; defer width computation to first render
    // if no renderer yet. For now use a default width; render() will resize.
    width_ = (int)theme_.panelPad * 2 + 80;  // initial estimate
    if (width_ > MAX_TOOLTIP_W) width_ = MAX_TOOLTIP_W;

    surface_ = wl_compositor_create_surface(compositor_);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "tray_tooltip");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    // Position above the panel: the panel is bottom-anchored and panelTopY is
    // its top edge (HEIGHT=48). The tooltip's bottom margin is the distance
    // from the bottom of the screen to the tooltip's bottom edge; to hug the
    // panel top with just the gapItem gap, that margin is panelTopY + gapItem
    // (48 + 8 = 56). Margins are surface-local (logical) coordinates, so no
    // scale factor is applied here; the compositor places the surface in
    // output space.
    int bottomMargin = panelTopY + (int)theme_.gapItem;
    int leftMargin = anchorX;
    zwlr_layer_surface_v1_set_size(layer_surface_, width_, TOOLTIP_H);
    zwlr_layer_surface_v1_set_margin(layer_surface_, 0, 0, bottomMargin, leftMargin);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);

    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_commit(surface_);
}

void TrayTooltip::render() {
    if (!configured_ || !renderer_) return;

    // Measure text and resize if needed.
    NVGcontext* vg = renderer_->ctx();
    int fontH = fontCache_ ? fontCache_->loadSans() : -1;
    float textW = 0.0f;
    if (fontH >= 0 && vg) {
        nvgFontFaceId(vg, fontH);
        nvgFontSize(vg, theme_.labelPrimaryPx);
        float bounds[4];
        nvgTextBounds(vg, 0, 0, text_.c_str(), nullptr, bounds);
        textW = bounds[2] - bounds[0];
    }
    int newWidth = (int)textW + (int)theme_.panelPad * 2;
    if (newWidth > MAX_TOOLTIP_W) newWidth = MAX_TOOLTIP_W;
    if (newWidth < 40) newWidth = 40;
    if (newWidth != width_) {
        width_ = newWidth;
        if (layer_surface_) {
            zwlr_layer_surface_v1_set_size(layer_surface_, width_, TOOLTIP_H);
            wl_surface_commit(surface_);
        }
        return;  // configure callback will re-render
    }

    renderer_->beginFrame(width_, TOOLTIP_H);
    renderer_->clear();

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, (float)width_, (float)TOOLTIP_H, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBg);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5f, 0.5f, (float)width_ - 1.0f, (float)TOOLTIP_H - 1.0f, theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    if (fontH >= 0) {
        nvgFontFaceId(vg, fontH);
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, theme_.textPrimary);
        nvgText(vg, (float)theme_.panelPad, (float)TOOLTIP_H / 2, text_.c_str(), nullptr);
    }

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void TrayTooltip::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                    uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<TrayTooltip*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    int width  = w > 0 ? (int)w : self->width_;
    int height = h > 0 ? (int)h : TOOLTIP_H;
    int physW = width  * self->scale_;
    int physH = height * self->scale_;

    if (!self->egl_window_) {
        self->egl_window_  = wl_egl_window_create(self->surface_, physW, physH);
        self->egl_surface_ = eglCreateWindowSurface(self->egl_.display(), self->egl_.config(),
                                                     (EGLNativeWindowType)self->egl_window_, nullptr);
        self->renderer_ = std::make_unique<Renderer>(self->theme_, self->egl_);
        if (!self->renderer_->init(self->egl_surface_)) {
            std::fprintf(stderr, "TrayTooltip: renderer init failed\n");
            self->renderer_.reset();
            return;
        }
        self->renderer_->setPixelRatio((float)self->scale_);
        self->fontCache_ = std::make_unique<FontCache>(self->renderer_->ctx());
    } else {
        wl_egl_window_resize(self->egl_window_, physW, physH, 0, 0);
    }

    self->configured_ = true;
    self->render();
}

void TrayTooltip::handle_closed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<TrayTooltip*>(data);
    self->configured_ = false;
}
