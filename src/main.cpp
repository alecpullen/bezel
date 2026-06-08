#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <sys/types.h>
#include <vector>
#include <wayland-client-protocol.h>
#include "egl.hpp"
#include "panel.hpp"
#include "protocol.hpp"

struct Output {
    wl_output* wl       = nullptr;
    uint32_t   name_id  = 0;
    int        scale    = 1;
    char       name[64] = {0};
    std::unique_ptr<Panel> panel;
};

class App {
    public:
        bool init();
        void run();
        void finish();

    private:
        void create_panel(Output& o);

        static void reg_global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version);
        static void reg_global_remove(void* data, wl_registry* r, uint32_t name);

        static void out_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
        static void out_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
        static void out_done(void*, wl_output*) {}
        static void out_scale(void* data, wl_output*, int32_t factor) {
            static_cast<Output*>(data)->scale = factor;
        } 
        static void out_name(void* data, wl_output*, const char* name) {
            auto* o = static_cast<Output*>(data);
            snprintf(o->name, sizeof(o->name), "%s", name);
        }
        static void out_description(void*, wl_output*, const char*) {}
        
        wl_display*          display_     = nullptr;
        wl_registry*         registry_    = nullptr;
        wl_compositor*       compositor_  = nullptr;
        zwlr_layer_shell_v1* layer_shell_ = nullptr;
        Egl                  egl_;
        std::vector<std::unique_ptr<Output>> outputs_;
        bool                 ready_       = false;
};

void App::create_panel(Output& o) {
    if (!compositor_ || !layer_shell_) return;
    o.panel = std::make_unique<Panel>(egl_, compositor_, layer_shell_, o.wl, o.name[0] ? o.name : "panel");
}

void App::reg_global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
    auto* self = static_cast<App*>(data);
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        self->compositor_ = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell_ = (zwlr_layer_shell_v1*)wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        auto o = std::make_unique<Output>();
        o->name_id = name;
        o->wl = (wl_output*)wl_registry_bind(r, name, &wl_output_interface, v);
        static const wl_output_listener listener {
            .geometry = out_geometry, .mode = out_mode, .done = out_done,
            .scale = out_scale, .name = out_name, .description = out_description,
        };
        wl_output_add_listener(o->wl, &listener, o.get());
        self->outputs_.push_back(std::move(o));
        if (self->ready_) self->create_panel(*self->outputs_.back());
    }
}

void App::reg_global_remove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<App*>(data);
    for (auto it = self->outputs_.begin(); it != self->outputs_.end(); ++it)
        if ((*it)->name_id == name) { self->outputs_.erase(it); break; }
}

bool App::init() {
    display_ = wl_display_connect(nullptr);
    if (!display_) { fprintf(stderr, "cannot connect to Wayland display\n"); return false; }

    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener reg_listener {
    .global = reg_global, .global_remove = reg_global_remove,
    };
    wl_registry_add_listener(registry_, &reg_listener, this);

    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !layer_shell_) {
        fprintf(stderr, "compositor lacks wl_compositor or wlr-layer-shell\n");
        return false;
    }
    if (!egl_.init(display_)) return false;

    for (auto& o : outputs_) create_panel(*o);
    ready_ = true;
    return true;
}

void App::run() { while (wl_display_dispatch(display_) != -1) {} }

void App::finish() {
    outputs_.clear();
    if (layer_shell_) zwlr_layer_shell_v1_destroy(layer_shell_);
    if (compositor_) wl_compositor_destroy(compositor_);
    egl_.finish();
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

int main() {
    App app;
    if (!app.init()) return 1;
    app.run();
    app.finish();
    return 0;
}