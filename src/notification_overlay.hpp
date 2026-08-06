#pragma once
#include "protocol.hpp"
#include "renderer.hpp"
#include "theme.hpp"
#include "notification_service.hpp"
#include "font_cache.hpp"
#include "icon_loader.hpp"
#include <EGL/egl.h>
#include <wayland-egl.h>
#include <functional>
#include <memory>
#include <vector>

class Egl;

class NotificationOverlay {
public:
    NotificationOverlay(Egl& egl, Theme& theme, wl_compositor* compositor,
                        zwlr_layer_shell_v1* shell, wl_output* output, int scale,
                        NotificationService& service);
    ~NotificationOverlay();

    void setOutput(wl_output* output, int scale);
    void render();

    void handlePointerMotion(int x, int y);
    void handlePointerButton(int x, int y, uint32_t button);
    void handlePointerLeave();

    wl_surface* surface() const { return surface_; }

private:
    struct ToastLayout {
        const Notification* notification = nullptr;
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<std::pair<std::string, std::pair<int,int>>> actionButtons; // key, (x, w)
        bool expanded = false;
    };

    void createSurface();
    void destroySurface();
    void updateGeometry();
    int computeWidth() const;
    int computeHeight(const std::vector<ToastLayout>& layouts) const;
    std::vector<ToastLayout> computeLayouts(int width) const;
    void drawToast(const ToastLayout& toast, NVGcontext* vg, int fontHandle);

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
    std::unique_ptr<IconLoader> iconLoader_;

    NotificationService& service_;
    int subId_ = -1;
    bool dirty_ = true;
    bool configured_ = false;
    int width_ = 360;
    int height_ = 0;
    int hoverToast_ = -1;
    static constexpr int MAX_WIDTH = 360;
    static constexpr int MAX_VISIBLE = 3;
    static constexpr int ICON_SIZE = 20;
};
