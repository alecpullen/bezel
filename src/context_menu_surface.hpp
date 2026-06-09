#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "protocol.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"

class Egl;

class ContextMenuSurface {
public:
    ContextMenuSurface(Egl& egl, Theme& theme, wl_compositor* compositor,
                       zwlr_layer_shell_v1* shell, wl_output* output,
                       int anchor_x, bool pinned,
                       zwlr_foreign_toplevel_handle_v1* handle,
                       std::string app_id,
                       std::function<void()> on_close,
                       std::function<void()> on_pin);
    ~ContextMenuSurface();

    wl_surface* surface() const { return surface_; }
    void handlePointerButton(int x, int y, uint32_t button);
    void handlePointerMotion(int x, int y);

    static constexpr int MENU_W = 160;
    static constexpr int ITEM_H = 32;
    static constexpr int MENU_H = ITEM_H * 2;

private:
    void render();

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl&                   egl_;
    Theme&                 theme_;
    wl_surface*            surface_       = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window*         egl_window_    = nullptr;
    EGLSurface             egl_surface_   = EGL_NO_SURFACE;
    bool                   pinned_;
    zwlr_foreign_toplevel_handle_v1* handle_;
    std::string            app_id_;
    std::function<void()>  on_close_;
    std::function<void()>  on_pin_;
    int                        hoverItem_ = -1;
    std::unique_ptr<Renderer>  renderer_;
    std::unique_ptr<FontCache> fontCache_;
};
