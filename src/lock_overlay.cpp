#include "lock_overlay.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

LockOverlay::LockOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                         zwlr_layer_shell_v1* shell, wl_output* output, int scale)
    : egl_(egl), theme_(theme), compositor_(compositor), shell_(shell),
      output_(output), scale_(scale > 0 ? scale : 1) {}

LockOverlay::~LockOverlay() {
    hide();
}

bool LockOverlay::showSoft() {
    if (visible_) return true;
    if (!compositor_ || !shell_) return false;

    surface_ = wl_compositor_create_surface(compositor_);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "lock");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_commit(surface_);

    hard_ = false;
    visible_ = true;
    configured_ = false;
    return true;
}

bool LockOverlay::showHard(ext_session_lock_v1* lock) {
    if (visible_ || !lock || !compositor_) return false;
    surface_ = wl_compositor_create_surface(compositor_);
    wl_surface_set_buffer_scale(surface_, scale_);
    lock_surface_ = ext_session_lock_v1_get_lock_surface(lock, surface_, output_);
    static const ext_session_lock_surface_v1_listener listener = {
        .configure = lock_configure,
    };
    ext_session_lock_surface_v1_add_listener(lock_surface_, &listener, this);
    hard_ = true;
    visible_ = true;
    configured_ = false;
    return true;
}

void LockOverlay::hide() {
    if (!visible_ && !surface_) return;
    destroySurface();
    surface_ = nullptr;
    layer_surface_ = nullptr;
    lock_surface_ = nullptr;
    egl_window_ = nullptr;
    egl_surface_ = EGL_NO_SURFACE;
    renderer_.reset();
    fontCache_.reset();
    visible_ = false;
    configured_ = false;
}

void LockOverlay::createRenderer() {
    if (renderer_) return;
    if (egl_surface_ == EGL_NO_SURFACE) return;
    renderer_ = std::make_unique<Renderer>(theme_, egl_);
    if (!renderer_->init(egl_surface_)) {
        std::fprintf(stderr, "LockOverlay: renderer init failed\n");
        renderer_.reset();
        return;
    }
    renderer_->setPixelRatio((float)scale_);
    fontCache_ = std::make_unique<FontCache>(renderer_->ctx());
    fontHandle_ = fontCache_->loadSans();
}

void LockOverlay::render(const LockViewState& view) {
    if (!visible_ || !configured_ || !renderer_ || fontHandle_ < 0) return;
    // nvgBeginFrame with a zero extent is a documented NanoVG crash case; a
    // compositor may legally send a 0x0 configure (e.g. while arranging an
    // output). Skip the frame until a real size arrives.
    if (width_ <= 0 || height_ <= 0) return;

    renderer_->beginFrame(width_, height_);
    renderer_->clear();

    NVGcontext* vg = renderer_->ctx();

    // Background.
    NVGcolor bg = theme_.lockBgColor;
    bg.a = theme_.lockBgOpacity;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)width_, (float)height_);
    nvgFillColor(vg, bg);
    nvgFill(vg);

    float cx = (float)width_ / 2.0f;

    // Clock (large).
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, 64.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, theme_.textPrimary);
    nvgText(vg, cx, (float)height_ * 0.28f, view.timeText.c_str(), nullptr);

    // Date.
    nvgFontSize(vg, theme_.labelSecondaryPx);
    nvgFillColor(vg, theme_.textSecondary);
    nvgText(vg, cx, (float)height_ * 0.28f + 34.0f, view.dateText.c_str(), nullptr);

    // Media line.
    if (!view.mediaText.empty()) {
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgFillColor(vg, theme_.textSecondary);
        nvgText(vg, cx, (float)height_ * 0.40f, view.mediaText.c_str(), nullptr);
    }

    // Battery.
    float infoY = view.mediaText.empty() ? (float)height_ * 0.40f
                                         : (float)height_ * 0.44f;
    if (!view.batteryText.empty()) {
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgFillColor(vg, theme_.textMuted);
        nvgText(vg, cx, infoY, view.batteryText.c_str(), nullptr);
        infoY += 26.0f;
    }

    // Password prompt: dots (U+2022 BULLET in UTF-8).
    std::string dots;
    for (int i = 0; i < view.passwordLen; ++i) dots += "\xe2\x80\xa2";
    float pwY = (float)height_ * 0.55f;
    nvgFontSize(vg, 20.0f);
    nvgFillColor(vg, theme_.textPrimary);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, pwY, dots.c_str(), nullptr);

    // Status / errors.
    float statusY = pwY + 30.0f;
    if (!view.statusMessage.empty()) {
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgFillColor(vg, theme_.red);
        nvgText(vg, cx, statusY, view.statusMessage.c_str(), nullptr);
        statusY += 22.0f;
    }
    if (view.failedAttempts > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d failed attempt(s)", view.failedAttempts);
        nvgFontSize(vg, theme_.labelSecondaryPx);
        nvgFillColor(vg, theme_.textMuted);
        nvgText(vg, cx, statusY, buf, nullptr);
        statusY += 22.0f;
    }
    if (view.capsLock) {
        nvgFontSize(vg, theme_.labelSecondaryPx);
        nvgFillColor(vg, theme_.yellow);
        nvgText(vg, cx, statusY, "Caps Lock is on", nullptr);
        statusY += 22.0f;
    }
    if (view.authenticating) {
        nvgFontSize(vg, theme_.labelSecondaryPx);
        nvgFillColor(vg, theme_.accent);
        nvgText(vg, cx, statusY, "Verifying\xe2\x80\xa6", nullptr); // U+2026 …
    }

    // Hint.
    nvgFontSize(vg, theme_.labelSecondaryPx);
    nvgFillColor(vg, theme_.textMuted);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, (float)height_ * 0.95f,
            "Enter to unlock \xc2\xb7 Esc to clear", nullptr); // U+00B7 ·

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void LockOverlay::destroySurface() {
    if (hard_) {
        if (lock_surface_) ext_session_lock_surface_v1_destroy(lock_surface_);
        lock_surface_ = nullptr;
    } else {
        if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
        layer_surface_ = nullptr;
    }
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    // surface_ is owned externally for hard lock; only destroy for soft lock.
    if (!hard_ && surface_) wl_surface_destroy(surface_);
    egl_surface_ = EGL_NO_SURFACE;
    egl_window_ = nullptr;
    renderer_.reset();
    fontCache_.reset();
}

void LockOverlay::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                   uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<LockOverlay*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    self->width_ = (int)w;
    self->height_ = (int)h;
    int physW = self->width_ * self->scale_;
    int physH = self->height_ * self->scale_;

    if (!self->egl_window_) {
        self->egl_window_ = wl_egl_window_create(self->surface_, physW, physH);
        self->egl_surface_ = eglCreateWindowSurface(
            self->egl_.display(), self->egl_.config(),
            (EGLNativeWindowType)self->egl_window_, nullptr);
        self->createRenderer();
    } else {
        wl_egl_window_resize(self->egl_window_, physW, physH, 0, 0);
    }

    self->configured_ = true;
}

void LockOverlay::lock_configure(void* data, ext_session_lock_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<LockOverlay*>(data);
    ext_session_lock_surface_v1_ack_configure(s, serial);

    self->width_ = (int)w;
    self->height_ = (int)h;
    int physW = self->width_ * self->scale_;
    int physH = self->height_ * self->scale_;

    if (!self->egl_window_) {
        self->egl_window_ = wl_egl_window_create(self->surface_, physW, physH);
        self->egl_surface_ = eglCreateWindowSurface(
            self->egl_.display(), self->egl_.config(),
            (EGLNativeWindowType)self->egl_window_, nullptr);
        self->createRenderer();
    } else {
        wl_egl_window_resize(self->egl_window_, physW, physH, 0, 0);
    }

    self->configured_ = true;
    // Do NOT commit a null buffer here — that is a protocol error for
    // session-lock surfaces. The first real frame (attached via EGL swap in
    // render()) is what commits the surface; App::tick() calls render() each
    // loop while the lock is up.
}

void LockOverlay::handle_closed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<LockOverlay*>(data);
    self->configured_ = false;
    self->visible_ = false;
}
