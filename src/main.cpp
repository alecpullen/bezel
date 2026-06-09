#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <sys/types.h>
#include <vector>
#include <poll.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <sdbus-c++/sdbus-c++.h>
#include "egl.hpp"
#include "panel.hpp"
#include "protocol.hpp"
#include "theme.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"
#include "battery_service.hpp"
#include "control_socket.hpp"
#include "desktop_index.hpp"
#include "search_engine.hpp"
#include "toplevel_service.hpp"
#include "workspace_service.hpp"

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

        static void seat_caps(void* data, wl_seat* seat, uint32_t caps);
        static void ptr_enter(void* data, wl_pointer*, uint32_t serial,
                              wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
        static void ptr_leave(void* data, wl_pointer*, uint32_t serial, wl_surface*);
        static void ptr_motion(void* data, wl_pointer*, uint32_t time,
                               wl_fixed_t sx, wl_fixed_t sy);
        static void ptr_button(void* data, wl_pointer*, uint32_t serial, uint32_t time,
                               uint32_t button, uint32_t state);
        
        wl_display*          display_      = nullptr;
        wl_registry*         registry_     = nullptr;
        wl_compositor*       compositor_   = nullptr;
        zwlr_layer_shell_v1* layer_shell_  = nullptr;
        zwlr_foreign_toplevel_manager_v1* toplevel_manager_ = nullptr;
        zdwl_ipc_manager_v2* dwl_ipc_manager_ = nullptr;
        wl_seat*             seat_             = nullptr;
        wl_pointer*          pointer_          = nullptr;
        Panel*               hoveredPanel_     = nullptr;
        Panel*               hoveredMenuOwner_ = nullptr;
        int                  ptrX_             = 0;
        int                  ptrY_             = 0;
        Egl                  egl_;
        Theme                theme_ = Theme::defaultTheme();
        std::vector<std::unique_ptr<Output>> outputs_;
        bool                 ready_        = false;
        bool                 running_      = true;
        std::unique_ptr<sdbus::IConnection> dbusConn_;
        std::unique_ptr<BatteryService>   batteryService_;
        std::unique_ptr<ToplevelService>  toplevelService_;
        std::unique_ptr<WorkspaceService> workspaceService_;
        std::unique_ptr<DesktopIndex>     desktopIndex_;
        std::unique_ptr<SearchEngine>     searchEngine_;
        std::unique_ptr<ControlSocket>    controlSocket_;

        void handleSocketCommand(std::string_view cmd);
        void tick();
};

void App::create_panel(Output& o) {
    if (!compositor_ || !layer_shell_) return;
    o.panel = std::make_unique<Panel>(theme_, egl_, compositor_, layer_shell_, o.wl, o.name[0] ? o.name : "panel", batteryService_.get(), toplevelService_.get(), workspaceService_.get(), seat_);
    o.panel->setScale(o.scale);
}

void App::reg_global(void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
    auto* self = static_cast<App*>(data);
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        self->compositor_ = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell_ = (zwlr_layer_shell_v1*)wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
        self->toplevel_manager_ = (zwlr_foreign_toplevel_manager_v1*)wl_registry_bind(r, name, &zwlr_foreign_toplevel_manager_v1_interface, 3);
        self->toplevelService_ = std::make_unique<ToplevelService>(self->toplevel_manager_);
        self->toplevelService_->init();
    } else if (strcmp(iface, zdwl_ipc_manager_v2_interface.name) == 0) {
        self->dwl_ipc_manager_ = (zdwl_ipc_manager_v2*)wl_registry_bind(r, name, &zdwl_ipc_manager_v2_interface, 2);
        self->workspaceService_ = std::make_unique<WorkspaceService>(self->dwl_ipc_manager_);
        self->workspaceService_->init();
        for (auto& o : self->outputs_)
            self->workspaceService_->add_output(o->wl);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        self->seat_ = (wl_seat*)wl_registry_bind(r, name, &wl_seat_interface, 1);
        static const wl_seat_listener seat_listener {
            .capabilities = seat_caps,
            .name         = [](void*, wl_seat*, const char*) {},
        };
        wl_seat_add_listener(self->seat_, &seat_listener, self);
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
        if (self->workspaceService_) {
            self->workspaceService_->add_output(o->wl);
        }
        self->outputs_.push_back(std::move(o));
        if (self->ready_) self->create_panel(*self->outputs_.back());
    }
}

void App::seat_caps(void* data, wl_seat* seat, uint32_t caps) {
    auto* self = static_cast<App*>(data);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !self->pointer_) {
        self->pointer_ = wl_seat_get_pointer(seat);
        static const wl_pointer_listener pointer_listener = [] {
            wl_pointer_listener l{};
            l.enter  = ptr_enter;
            l.leave  = ptr_leave;
            l.motion = ptr_motion;
            l.button = ptr_button;
            return l;
        }();
        wl_pointer_add_listener(self->pointer_, &pointer_listener, self);
        for (auto& o : self->outputs_)
            if (o->panel) o->panel->setSeat(self->seat_);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && self->pointer_) {
        wl_pointer_release(self->pointer_);
        self->pointer_ = nullptr;
    }
}

void App::ptr_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                    wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<App*>(data);
    self->hoveredPanel_     = nullptr;
    self->hoveredMenuOwner_ = nullptr;
    for (auto& o : self->outputs_) {
        if (!o->panel) continue;
        if (o->panel->surface() == surface) {
            self->hoveredPanel_ = o->panel.get();
            break;
        }
        if (o->panel->popupSurface() == surface) {
            self->hoveredMenuOwner_ = o->panel.get();
            break;
        }
    }
    self->ptrX_ = wl_fixed_to_int(sx);
    self->ptrY_ = wl_fixed_to_int(sy);
}

void App::ptr_leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
    auto* self = static_cast<App*>(data);
    if (self->hoveredPanel_) self->hoveredPanel_->handlePointerLeave();
    self->hoveredPanel_     = nullptr;
    self->hoveredMenuOwner_ = nullptr;
}

void App::ptr_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<App*>(data);
    self->ptrX_ = wl_fixed_to_int(sx);
    self->ptrY_ = wl_fixed_to_int(sy);
    if (self->hoveredMenuOwner_)
        self->hoveredMenuOwner_->handlePopupPointerMotion(self->ptrX_, self->ptrY_);
    else if (self->hoveredPanel_)
        self->hoveredPanel_->handlePointerMotion(self->ptrX_, self->ptrY_);
}

void App::ptr_button(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    auto* self = static_cast<App*>(data);
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (self->hoveredMenuOwner_)
        self->hoveredMenuOwner_->handlePopupPointerButton(self->ptrX_, self->ptrY_, button);
    else if (self->hoveredPanel_)
        self->hoveredPanel_->handlePointerButton(self->ptrX_, self->ptrY_, button);
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

    desktopIndex_ = std::make_unique<DesktopIndex>();
    desktopIndex_->scan();
    searchEngine_ = std::make_unique<SearchEngine>(*desktopIndex_, toplevelService_.get());

    controlSocket_ = std::make_unique<ControlSocket>();
    if (!controlSocket_->init())
        controlSocket_.reset();

    try {
        dbusConn_ = sdbus::createSystemBusConnection();
        batteryService_ = std::make_unique<BatteryService>(*dbusConn_);
        if (!batteryService_->init()) {
            batteryService_.reset();
        }
    } catch (const sdbus::Error& e) {
        fprintf(stderr, "Warning: Failed to connect to D-Bus system bus: %s\n", e.what());
    }

    for (auto& o : outputs_) create_panel(*o);
    wl_display_roundtrip(display_);
    ready_ = true;
    return true;
}

void App::run() {
    wl_display_flush(display_);
    
    while (running_) {
        if (wl_display_get_error(display_)) {
            break;
        }
        while (wl_display_prepare_read(display_) != 0) {
            if (wl_display_dispatch_pending(display_) < 0) {
                running_ = false;
                break;
            }
        }
        if (!running_) break;
        wl_display_flush(display_);

        int wlFd = wl_display_get_fd(display_);

        struct pollfd fds[4];
        int nfds = 0;
        int wlIdx = nfds++;
        fds[wlIdx] = { wlFd, POLLIN, 0 };

        int dbusIdx = -1, dbusEventIdx = -1;
        int timeout = 1000;
        sdbus::IConnection::PollData dbusPoll{};
        if (dbusConn_) {
            dbusPoll   = dbusConn_->getEventLoopPollData();
            int dbusTo = dbusPoll.getPollTimeout();
            if (dbusTo >= 0 && dbusTo < timeout) timeout = dbusTo;
            dbusIdx      = nfds++;
            fds[dbusIdx] = { dbusPoll.fd, dbusPoll.events, 0 };
            dbusEventIdx      = nfds++;
            fds[dbusEventIdx] = { dbusPoll.eventFd, POLLIN, 0 };
        }

        int sockIdx = -1;
        if (controlSocket_) {
            sockIdx      = nfds++;
            fds[sockIdx] = { controlSocket_->fd(), POLLIN, 0 };
        }

        int ret = poll(fds, nfds, timeout);

        if (ret > 0) {
            if (fds[wlIdx].revents & POLLIN) {
                if (wl_display_read_events(display_) < 0) break;
            } else {
                wl_display_cancel_read(display_);
            }
        } else {
            wl_display_cancel_read(display_);
        }

        if (dbusConn_)
            dbusConn_->processPendingEvent();

        if (sockIdx >= 0 && (fds[sockIdx].revents & POLLIN))
            controlSocket_->dispatch([this](std::string_view cmd){ handleSocketCommand(cmd); });
        
        if (wl_display_dispatch_pending(display_) < 0) {
            break;
        }

        if (batteryService_) {
            batteryService_->tick();
        }
        if (toplevelService_) {
            toplevelService_->tick();
        }
        if (workspaceService_) {
            workspaceService_->tick();
        }
        
        tick();
    }
}

void App::handleSocketCommand(std::string_view cmd) {
    if (cmd == "toggle_launcher") {
        if (searchEngine_) {
            auto results = searchEngine_->query("a");
            fprintf(stderr, "toggle_launcher: %zu results for query 'a'\n", results.size());
            for (size_t i = 0; i < results.size() && i < 5; ++i)
                fprintf(stderr, "  [%d] %s (%s)\n", results[i].score, results[i].name.c_str(), results[i].subtitle.c_str());
        }
    } else {
        fprintf(stderr, "control socket: unknown command '%.*s'\n",
                (int)cmd.size(), cmd.data());
    }
}

void App::tick() {
    if (!ready_) return;
    for (auto& o : outputs_) {
        if (o->panel) {
            o->panel->tick();
            o->panel->render();
        }
    }
}

void App::finish() {
    outputs_.clear();
    toplevelService_.reset();
    workspaceService_.reset();
    if (pointer_) { wl_pointer_release(pointer_); pointer_ = nullptr; }
    if (seat_)    { wl_seat_destroy(seat_); seat_ = nullptr; }
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