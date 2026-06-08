// panel.hpp
#pragma once
#include <cstdint>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "protocol.hpp"

class Egl;

class Panel {
public:
    Panel(Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell,
          wl_output* output, const char* name);
    ~Panel();

private:
    void resize(int w, int h);
    void render();

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl&                   egl_;
    wl_surface*            surface_       = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window*         egl_window_    = nullptr;
    EGLSurface             egl_surface_   = EGL_NO_SURFACE;
    int                    width_  = 0;
    int                    height_ = HEIGHT;
    bool                   configured_ = false;

    static constexpr int HEIGHT = 32;
};
