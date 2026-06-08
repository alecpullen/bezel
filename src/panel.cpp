// panel.cpp
#include "panel.hpp"
#include "egl.hpp"
#include <EGL/egl.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <cstdint>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>

Panel::Panel(Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell, wl_output* output, const char* name) : egl_(egl) {
    surface_ = wl_compositor_create_surface(compositor);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(shell, surface_, output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, name);

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);
    
    zwlr_layer_surface_v1_set_anchor(layer_surface_,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT  |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    // 0 width = stretch
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, HEIGHT);

    wl_surface_commit(surface_);
}

Panel::~Panel() {
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_)                    wl_egl_window_destroy(egl_window_);
    if (layer_surface_)                 zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_)                       wl_surface_destroy(surface_);
}

void Panel::resize(int w, int h) {
    width_  = w > 0 ? w : width_;
    height_ = h > 0 ? h : HEIGHT;
    if (!egl_window_) {
        egl_window_  = wl_egl_window_create(surface_, width_, height_);
        egl_surface_ = eglCreateWindowSurface(egl_.display(), egl_.config(), (EGLNativeWindowType)egl_window_, nullptr);
    } else {
        wl_egl_window_resize(egl_window_, width_, height_, 0, 0);
    }
    configured_ = true;
}

void Panel::render() {
    if (!configured_) return;
    eglMakeCurrent(egl_.display(), egl_surface_, egl_surface_, egl_.context());
    glViewport(0, 0, width_, height_);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void Panel::handle_configure(void* data, zwlr_layer_surface_v1* s, uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<Panel*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);
    self->resize((int)w, (int)h);
    self->render();
}

void Panel::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}
