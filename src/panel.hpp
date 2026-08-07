// panel.hpp
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "protocol.hpp"
#include "search_engine.hpp"
#include "widget.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"
#include "layout.hpp"

class Egl;
class BatteryService;
class ToplevelService;
class WorkspaceService;
class WindowList;
class Clock;
class ContextMenuSurface;
class IconLoader;

class Panel {
public:
    Panel(Theme& theme, Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell,
          wl_output* output, const char* name, BatteryService* batteryService,
          ToplevelService* toplevelService, WorkspaceService* workspaceService,
          wl_seat* seat);
    ~Panel();

    void setScale(int scale);
    void setSeat(wl_seat* seat);
    void render();
    void tick();
    void requestRedraw();

    NVGcontext* rendererNvg() const;

    wl_surface* surface() const { return surface_; }
    void handlePointerButton(int x, int y, uint32_t button);
    void handlePopupPointerButton(int x, int y, uint32_t button);
    void handlePointerMotion(int x, int y);
    void handlePointerLeave();
    void handlePopupPointerMotion(int x, int y);
    wl_surface* popupSurface() const;

    // Launcher / command mode (M5.4)
    void activateLauncher(SearchEngine& engine,
                          std::function<void(CommandAction)> commandCb);
    void dismissLauncher();
    void handleLauncherText(std::string_view text);  // UTF-8 text from M5.5 InputBackend
    void handleLauncherKey(uint32_t keysym);          // XKB keysym values
    bool launcherActive() const { return launcherActive_; }

private:
    void resize(int w, int h);
    void togglePin(const std::string& app_id);
    void renderLauncher();
    void updateLauncherResults();
    void launchSelected();
    static std::vector<std::string> splitExec(const std::string& exec);

    static void handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h);
    static void handle_closed(void* data, zwlr_layer_surface_v1* s);

    Egl&                   egl_;
    Theme&                 theme_;
    wl_compositor*         compositor_      = nullptr;
    zwlr_layer_shell_v1*   layer_shell_     = nullptr;
    wl_output*             output_          = nullptr;
    wl_seat*               seat_            = nullptr;
    BatteryService*        batteryService_  = nullptr;
    ToplevelService*       toplevelService_ = nullptr;
    WorkspaceService*      workspaceService_= nullptr;
    wl_surface*            surface_         = nullptr;
    zwlr_layer_surface_v1* layer_surface_   = nullptr;
    wl_egl_window*         egl_window_      = nullptr;
    EGLSurface             egl_surface_     = EGL_NO_SURFACE;
    int                    width_           = 0;
    int                    height_          = HEIGHT;
    float                  scale_           = 1.0f;
    bool                   configured_      = false;
    bool                   dirty_           = true;
    Clock*                 clock_           = nullptr;
    WindowList*            windowList_      = nullptr;

    // Launcher state (M5.4)
    bool             launcherActive_      = false;
    std::string      launcherQuery_;
    std::vector<SearchResult> launcherResults_;
    int              launcherSelectedIdx_ = 0;
    SearchEngine*    searchEngine_        = nullptr;
    int              fontHandle_          = -1;
    std::unique_ptr<IconLoader> launcherIconLoader_;
    std::function<void(CommandAction)> commandCb_;

    std::unique_ptr<Renderer>           renderer_;
    std::unique_ptr<FontCache>          fontCache_;
    std::unique_ptr<BoxLayout>          root_;
    std::unique_ptr<ContextMenuSurface> contextMenuSurface_;
    std::set<std::string>               pinnedAppIds_;

    static constexpr int HEIGHT         = 48;
    static constexpr int LAUNCHER_HEIGHT = 62;
};
