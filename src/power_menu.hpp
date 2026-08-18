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
#include <vector>

class Egl;

// A power action the user can select in the menu.
enum class PowerAction {
    Lock, Logout, Suspend, Hibernate, Reboot, ShutDown
};

// Keyboard-navigable power menu overlay surface (focus-following, singleton).
class PowerMenu {
public:
    PowerMenu(Egl& egl, Theme& theme, wl_compositor* compositor,
              zwlr_layer_shell_v1* shell, wl_output* output, int scale);
    ~PowerMenu();

    void setOutput(wl_output* output, int scale);

    void show(std::vector<PowerAction> actions);
    void hide();
    bool visible() const { return visible_; }
    void setOnKey(std::function<void(const KeyEvent&)> cb) { onKey_ = std::move(cb); }
    void setOnAction(std::function<void(PowerAction)> cb) { onAction_ = std::move(cb); }
    void setOnHide(std::function<void()> cb) { onHide_ = std::move(cb); }

    void handleKey(const KeyEvent& ev);
    void handlePointerButton(int x, int y, uint32_t button);
    void handlePointerMotion(int x, int y);
    void render();

    wl_surface* surface() const { return surface_; }

private:
    void confirm();
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
    int fontHandle_ = -1;
    bool visible_ = false;
    bool configured_ = false;
    int width_ = 0;
    int height_ = 0;
    int sel_ = 0;
    bool confirmArmed_ = false;
    std::vector<PowerAction> actions_;
    std::function<void(const KeyEvent&)> onKey_;
    std::function<void(PowerAction)> onAction_;
    std::function<void()> onHide_;
    static constexpr int MENU_W = 180;
    static constexpr int ITEM_H = 34;
    static constexpr int PAD = 8;
};
