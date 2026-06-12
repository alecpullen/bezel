#pragma once
#include <EGL/egl.h>
#include <cstdint>
#include <memory>
#include <wayland-egl.h>
#include "protocol.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"
#include "icon_loader.hpp"
#include "notification_service.hpp"

class Egl;

class NotificationOverlay {
public:
    NotificationOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                        zwlr_layer_shell_v1* shell, wl_output* output,
                        NotificationService& service);
    ~NotificationOverlay();

    void update();
    void render();

    wl_surface* surface() const { return surface_; }

    void handlePointerMotion(int x, int y);
    void handlePointerLeave();
    void handlePointerButton(int x, int y, uint32_t button);

    static constexpr int TOAST_W      = 360;
    static constexpr int TOAST_H      = 68;
    static constexpr int TOAST_GAP    = 8;
    static constexpr int MARGIN       = 12;
    static constexpr int PANEL_MARGIN = 48;
    static constexpr int ICON_SIZE    = 24;
    static constexpr int BTN_H        = 26;
    static constexpr int BTN_GAP      = 8;
    static constexpr int BTN_AREA_H   = BTN_H + BTN_GAP;
    static constexpr int BTN_PAD      = 10;

private:
    int  totalHeight() const;
    int  expandedHeight(int idx) const;
    int  cardHeight(int idx) const;
    void resize(int w, int h);

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl&                    egl_;
    Theme&                  theme_;
    wl_surface*             surface_       = nullptr;
    zwlr_layer_surface_v1*  layer_surface_ = nullptr;
    wl_egl_window*          egl_window_    = nullptr;
    EGLSurface              egl_surface_   = EGL_NO_SURFACE;

    std::unique_ptr<Renderer>   renderer_;
    std::unique_ptr<FontCache>  fontCache_;
    std::unique_ptr<IconLoader> iconLoader_;
    int fontHandle_ = -1;

    NotificationService& service_;
    int width_       = 0;
    int height_      = 0;
    int hoverIdx_    = -1;
    int hoverBtnIdx_ = -1;
    bool dirty_      = false;

    struct BtnRect { float x, y, w; };
    std::vector<std::vector<BtnRect>> btnRects_;
};
