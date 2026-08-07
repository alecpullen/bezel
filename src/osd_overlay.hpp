#pragma once
#include "protocol.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "font_cache.hpp"
#include <EGL/egl.h>
#include <wayland-egl.h>
#include <chrono>
#include <memory>

class Egl;

enum class OsdKind { Volume, Brightness };

struct OsdState {
    OsdKind kind;
    float level = 0.0f;        // 0.0–1.0
    bool muted = false;        // volume only
    std::chrono::steady_clock::time_point shownAt;
};

class OsdOverlay {
public:
    OsdOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
               zwlr_layer_shell_v1* shell, wl_output* output, int scale);
    ~OsdOverlay();

    void setOutput(wl_output* output, int scale);
    void show(OsdState state);
    void render();

    void handlePointerMotion(int, int) {}  // non-interactive
    void handlePointerButton(int, int, uint32_t) {}
    void handlePointerLeave() {}

    wl_surface* surface() const { return surface_; }

private:
    void createSurface();
    void destroySurface();

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl& egl_;
    Theme& theme_;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;
    wl_output* output_ = nullptr;
    int scale_ = 1;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<FontCache> fontCache_;

    OsdState state_;
    bool hasState_ = false;
    bool dirty_ = true;
    bool configured_ = false;
    int width_ = 180;
    int height_ = 56;
    int fontHandle_ = -1;

    static constexpr int OSD_W = 180;
    static constexpr int OSD_H = 56;
    static constexpr int PANEL_HEIGHT = 48;
    static constexpr int AUTO_DISMISS_MS = 2000;
};
