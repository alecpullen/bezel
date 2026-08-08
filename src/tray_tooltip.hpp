#pragma once
#include "protocol.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "font_cache.hpp"
#include <EGL/egl.h>
#include <wayland-egl.h>
#include <memory>
#include <string>

class Egl;

class TrayTooltip {
public:
    TrayTooltip(Egl& egl, Theme& theme, wl_compositor* compositor,
                zwlr_layer_shell_v1* shell, wl_output* output,
                int scale, int anchorX, int panelTopY, const std::string& text);
    ~TrayTooltip();

    wl_surface* surface() const { return surface_; }

private:
    void createSurface(int anchorX, int panelTopY);
    void render();

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl& egl_;
    Theme& theme_;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;
    wl_output* output_ = nullptr;
    std::string text_;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<FontCache> fontCache_;

    int scale_ = 1;
    int width_ = 0;
    int height_ = 28;
    int anchorX_ = 0;
    int panelTopY_ = 0;
    bool configured_ = false;
};
