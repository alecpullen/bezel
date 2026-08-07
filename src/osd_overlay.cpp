#include "osd_overlay.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <cstdio>
#include <cstring>

OsdOverlay::OsdOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                       zwlr_layer_shell_v1* shell, wl_output* output, int scale)
    : egl_(egl), theme_(theme), compositor_(compositor), shell_(shell),
      output_(output), scale_(scale > 0 ? scale : 1) {
    createSurface();
}

OsdOverlay::~OsdOverlay() {
    destroySurface();
}

void OsdOverlay::setOutput(wl_output* output, int scale) {
    if (output == output_ && scale == scale_) return;
    output_ = output;
    scale_ = scale > 0 ? scale : 1;
    destroySurface();
    createSurface();
}

void OsdOverlay::createSurface() {
    surface_ = wl_compositor_create_surface(compositor_);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "osd");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);  // hidden until show()
    int bottomMargin = (int)((PANEL_HEIGHT + theme_.gapItem) * scale_);
    zwlr_layer_surface_v1_set_margin(layer_surface_, 0, 0, bottomMargin, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);

    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_commit(surface_);

    configured_ = false;
    dirty_ = true;
}

void OsdOverlay::destroySurface() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_) wl_surface_destroy(surface_);
    renderer_.reset();
    fontCache_.reset();
    surface_ = nullptr;
    layer_surface_ = nullptr;
    egl_window_ = nullptr;
    egl_surface_ = EGL_NO_SURFACE;
    configured_ = false;
}

void OsdOverlay::show(OsdState state) {
    state_ = state;
    state_.shownAt = std::chrono::steady_clock::now();
    hasState_ = true;
    dirty_ = true;
    if (layer_surface_ && configured_) {
        zwlr_layer_surface_v1_set_size(layer_surface_, OSD_W, OSD_H);
        wl_surface_commit(surface_);
    }
}

void OsdOverlay::render() {
    if (!configured_ || !renderer_) return;
    if (!hasState_) return;

    // Auto-dismiss
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state_.shownAt);
    if (elapsed.count() > AUTO_DISMISS_MS) {
        if (layer_surface_) {
            zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);
            wl_surface_commit(surface_);
        }
        hasState_ = false;
        return;
    }

    if (!dirty_) return;
    dirty_ = false;

    renderer_->beginFrame(OSD_W, OSD_H);
    renderer_->clear();
    NVGcontext* vg = renderer_->ctx();

    // Card background
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, (float)OSD_W, (float)OSD_H, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBg);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5f, 0.5f, (float)OSD_W - 1.0f, (float)OSD_H - 1.0f, theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    if (fontHandle_ < 0) {
        renderer_->endFrame();
        eglSwapBuffers(egl_.display(), egl_surface_);
        return;
    }

    // Icon (left, 20px)
    const char* icon;
    if (state_.kind == OsdKind::Volume) {
        if (state_.muted || state_.level <= 0.0f) icon = "\xef\x9a\xa9"; // \uf6a9 muted
        else if (state_.level < 0.4f)              icon = "\xef\x80\xa7"; // \uf027 low
        else                                       icon = "\xef\x80\xa8"; // \uf028 high
    } else {
        icon = "\xef\x86\x85"; // \uf185 sun
    }

    int pad = (int)theme_.panelPad;
    float iconSize = 20.0f;
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, iconSize);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, theme_.textPrimary);
    nvgText(vg, (float)(pad + 10), (float)(OSD_H / 2), icon, nullptr);

    // Bar (middle)
    int barX = pad + 24;
    int barW = OSD_W - barX - pad - 48;  // leave room for percentage text
    int barY = OSD_H / 2 - 3;
    int barH = 6;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, (float)barX, (float)barY, (float)barW, (float)barH, theme_.radiusControl * 0.5f);
    nvgFillColor(vg, theme_.borderHairline);
    nvgFill(vg);
    float fillW = (float)barW * (state_.kind == OsdKind::Volume && state_.muted ? 0.0f : state_.level);
    if (fillW > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, (float)barX, (float)barY, fillW, (float)barH, theme_.radiusControl * 0.5f);
        nvgFillColor(vg, theme_.accent);
        nvgFill(vg);
    }

    // Percentage text (right)
    int pct = (int)(state_.level * 100.0f + 0.5f);
    if (pct > 100) pct = 100;
    char pctBuf[16];
    std::snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, theme_.textPrimary);
    nvgText(vg, (float)(OSD_W - pad), (float)(OSD_H / 2), pctBuf, nullptr);

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void OsdOverlay::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                  uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<OsdOverlay*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    int width  = w > 0 ? (int)w : self->OSD_W;
    int height = h > 0 ? (int)h : self->OSD_H;
    int physW = width  * self->scale_;
    int physH = height * self->scale_;

    if (!self->egl_window_) {
        self->egl_window_  = wl_egl_window_create(self->surface_, physW, physH);
        self->egl_surface_ = eglCreateWindowSurface(self->egl_.display(), self->egl_.config(),
                                                     (EGLNativeWindowType)self->egl_window_, nullptr);
        self->renderer_ = std::make_unique<Renderer>(self->theme_, self->egl_);
        if (!self->renderer_->init(self->egl_surface_)) {
            std::fprintf(stderr, "OsdOverlay: renderer init failed\n");
            self->renderer_.reset();
            return;
        }
        self->renderer_->setPixelRatio((float)self->scale_);
        self->fontCache_ = std::make_unique<FontCache>(self->renderer_->ctx());
        self->fontHandle_ = self->fontCache_->loadSans();
    } else {
        wl_egl_window_resize(self->egl_window_, physW, physH, 0, 0);
    }

    self->configured_ = true;
    self->dirty_ = true;
    self->render();
}

void OsdOverlay::handle_closed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<OsdOverlay*>(data);
    self->configured_ = false;
    self->dirty_ = true;
}
