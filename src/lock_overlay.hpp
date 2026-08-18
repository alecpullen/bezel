#pragma once
#include "protocol.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "font_cache.hpp"
#include "keyboard_input.hpp"
#include <EGL/egl.h>
#include <wayland-egl.h>
#include <functional>
#include <memory>
#include <string>

class Egl;

// Everything the lock overlay needs to draw on a given frame. App / LockService
// fill this from current services (battery, mpris) plus the clock/date.
struct LockViewState {
    std::string timeText;
    std::string dateText;
    std::string batteryText;   // e.g. "85%" or empty when battery absent
    std::string mediaText;     // now-playing line or empty
    int   passwordLen   = 0;
    bool  capsLock      = false;
    int   failedAttempts = 0;
    std::string statusMessage;
    bool  authenticating = false;
};

class LockOverlay {
public:
    LockOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                zwlr_layer_shell_v1* shell, wl_output* output, int scale);
    ~LockOverlay();

    // Soft lock: a fullscreen opaque layer-shell surface with exclusive
    // keyboard interactivity.
    bool showSoft();
    // Hard lock: creates a wl_surface + ext_session_lock_surface_v1 for this
    // overlay's output and wraps them.
    bool showHard(ext_session_lock_v1* lock);
    void hide();
    void render(const LockViewState& view);

    wl_surface* surface() const { return surface_; }
    void setOnKey(std::function<void(const KeyEvent&)> cb) { onKey_ = std::move(cb); }

private:
    void createRenderer();
    void destroySurface();

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);
    static void lock_configure(void* data, ext_session_lock_surface_v1* s,
                               uint32_t serial, uint32_t w, uint32_t h);

    Egl& egl_;
    Theme& theme_;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* shell_ = nullptr;
    wl_output* output_ = nullptr;
    int scale_ = 1;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_surface_ = nullptr;
    ext_session_lock_surface_v1* lock_surface_ = nullptr;
    wl_egl_window* egl_window_ = nullptr;
    EGLSurface egl_surface_ = EGL_NO_SURFACE;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<FontCache> fontCache_;
    int fontHandle_ = -1;
    bool hard_ = false;
    bool visible_ = false;
    bool configured_ = false;
    int width_ = 0;
    int height_ = 0;
    std::function<void(const KeyEvent&)> onKey_;
};
