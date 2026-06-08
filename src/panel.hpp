// panel.hpp
#pragma once
#include <cstdint>
#include <memory>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "protocol.hpp"
#include "widget.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"
#include "layout.hpp"

class Egl;
class BatteryService;
class ToplevelService;
class WorkspaceService;
class Clock;

class Panel {
public:
    Panel(Theme& theme, Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell,
          wl_output* output, const char* name, BatteryService* batteryService,
          ToplevelService* toplevelService, WorkspaceService* workspaceService);
    ~Panel();

    void setScale(int scale);
    void render();
    void tick();
    void requestRedraw();

private:
    void resize(int w, int h);

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl&                   egl_;
    Theme&                 theme_;
    wl_output*             output_        = nullptr;
    BatteryService*        batteryService_ = nullptr;
    ToplevelService*       toplevelService_ = nullptr;
    WorkspaceService*      workspaceService_ = nullptr;
    wl_surface*            surface_       = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window*         egl_window_    = nullptr;
    EGLSurface             egl_surface_   = EGL_NO_SURFACE;
    int                    width_  = 0;
    int                    height_ = HEIGHT;
    float                  scale_  = 1.0f;
    bool                   configured_ = false;
    bool                   dirty_ = true;
    Clock*                 clock_ = nullptr;

    std::unique_ptr<Renderer>  renderer_;
    std::unique_ptr<FontCache> fontCache_;
    std::unique_ptr<BoxLayout> root_;

    static constexpr int HEIGHT = 48;
};
