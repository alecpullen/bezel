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
#include "egl.hpp"
#include "panel.hpp"
#include "protocol.hpp"
#include "theme.hpp"
#include "renderer.hpp"
#include "font_cache.hpp"
#include "battery_service.hpp"
#include "brightness_service.hpp"
#include "audio_service.hpp"
#include "network_service.hpp"
#include "mpris_service.hpp"
#include "bus_manager.hpp"
#include "control_socket.hpp"
#include "notification_service.hpp"
#include "tray_service.hpp"
#include "notification_overlay.hpp"
#include "osd_overlay.hpp"
#include "desktop_index.hpp"
#include "search_engine.hpp"
#include "toplevel_service.hpp"
#include "workspace_service.hpp"
#include "config.hpp"
#include "keyboard_input.hpp"
#include "idle_service.hpp"

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
        ext_session_lock_manager_v1* sessionLockMgr_ = nullptr;
        ext_idle_notifier_v1* idleNotifier_ = nullptr;
        SessionConfig         sessionConfig_ = loadSessionConfig();
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
        std::unique_ptr<BusManager> busManager_;
        std::unique_ptr<NotificationService> notificationService_;
        std::unique_ptr<NotificationOverlay> notificationOverlay_;
        std::unique_ptr<OsdOverlay> osdOverlay_;
        bool pointerOverOverlay_ = false;
        bool pointerOverOsd_     = false;
        std::unique_ptr<BatteryService>   batteryService_;
        std::unique_ptr<BrightnessService> brightnessService_;
        std::unique_ptr<AudioService>      audioService_;
        std::unique_ptr<NetworkService>    networkService_;
        std::unique_ptr<MprisService>      mprisService_;
        std::unique_ptr<TrayService>       trayService_;
        Output*                     lastTrayOutput_ = nullptr;
        std::unique_ptr<ToplevelService>  toplevelService_;
        std::unique_ptr<WorkspaceService> workspaceService_;
        std::unique_ptr<DesktopIndex>     desktopIndex_;
        std::unique_ptr<SearchEngine>     searchEngine_;
        std::unique_ptr<ControlSocket>    controlSocket_;
        std::unique_ptr<KeyboardInput>    keyboardInput_;
        std::function<void(const KeyEvent&)> keyFocusTarget_;
        std::unique_ptr<IdleService>      idleService_;

        void handleSocketCommand(std::string_view cmd);
        void handleCommandAction(CommandAction action);
        Panel* activeLauncherTarget();
        Output* activeOutput();
        void tick();
};

void App::create_panel(Output& o) {
    if (!compositor_ || !layer_shell_) return;
    o.panel = std::make_unique<Panel>(theme_, egl_, compositor_, layer_shell_, o.wl, o.name[0] ? o.name : "panel",
        batteryService_.get(), toplevelService_.get(), workspaceService_.get(),
        audioService_.get(), brightnessService_.get(), networkService_.get(), mprisService_.get(),
        trayService_.get(), seat_);
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
    } else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
        self->sessionLockMgr_ = (ext_session_lock_manager_v1*)wl_registry_bind(
            r, name, &ext_session_lock_manager_v1_interface, 1);
        fprintf(stderr, "bezel: ext-session-lock-v1 available (hard lock)\n");
    } else if (strcmp(iface, ext_idle_notifier_v1_interface.name) == 0) {
        self->idleNotifier_ = (ext_idle_notifier_v1*)wl_registry_bind(
            r, name, &ext_idle_notifier_v1_interface, 1);
        fprintf(stderr, "bezel: ext-idle-notify-v1 available\n");
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
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !self->keyboardInput_) {
        self->keyboardInput_ = std::make_unique<KeyboardInput>();
        if (self->keyboardInput_->init(seat)) {
            self->keyboardInput_->setKeyHandler([self](const KeyEvent& e) {
                if (self->keyFocusTarget_) self->keyFocusTarget_(e);
            });
        } else {
            self->keyboardInput_.reset();
        }
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && self->keyboardInput_) {
        self->keyboardInput_.reset();
        self->keyFocusTarget_ = nullptr;
    }
}

void App::ptr_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                    wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<App*>(data);
    if (self->notificationOverlay_ && self->notificationOverlay_->surface() == surface) {
        if (self->hoveredPanel_) self->hoveredPanel_->handlePointerLeave();
        self->hoveredPanel_ = nullptr;
        self->hoveredMenuOwner_ = nullptr;
        self->pointerOverOverlay_ = true;
        self->ptrX_ = wl_fixed_to_int(sx);
        self->ptrY_ = wl_fixed_to_int(sy);
        self->notificationOverlay_->handlePointerMotion(self->ptrX_, self->ptrY_);
        return;
    }
    if (self->osdOverlay_ && self->osdOverlay_->surface() == surface) {
        if (self->hoveredPanel_) self->hoveredPanel_->handlePointerLeave();
        self->hoveredPanel_ = nullptr;
        self->hoveredMenuOwner_ = nullptr;
        self->pointerOverOsd_ = true;
        // OSD is non-interactive; no further routing
        return;
    }
    self->pointerOverOverlay_ = false;
    self->pointerOverOsd_     = false;
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
        if (o->panel->trayTooltipSurface() == surface) {
            self->hoveredMenuOwner_ = o->panel.get();
            self->ptrX_ = wl_fixed_to_int(sx);
            self->ptrY_ = wl_fixed_to_int(sy);
            return;
        }
    }
    self->ptrX_ = wl_fixed_to_int(sx);
    self->ptrY_ = wl_fixed_to_int(sy);
}

void App::ptr_leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
    auto* self = static_cast<App*>(data);
    if (self->pointerOverOverlay_ && self->notificationOverlay_) {
        self->notificationOverlay_->handlePointerLeave();
    }
    self->pointerOverOverlay_ = false;
    self->pointerOverOsd_     = false;
    if (self->hoveredPanel_) self->hoveredPanel_->handlePointerLeave();
    else if (self->hoveredMenuOwner_) self->hoveredMenuOwner_->handlePointerLeave();
    self->hoveredPanel_     = nullptr;
    self->hoveredMenuOwner_ = nullptr;
}

void App::ptr_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<App*>(data);
    self->ptrX_ = wl_fixed_to_int(sx);
    self->ptrY_ = wl_fixed_to_int(sy);
    if (self->pointerOverOverlay_ && self->notificationOverlay_) {
        self->notificationOverlay_->handlePointerMotion(self->ptrX_, self->ptrY_);
        return;
    }
    if (self->pointerOverOsd_) return;
    if (self->hoveredMenuOwner_)
        self->hoveredMenuOwner_->handlePopupPointerMotion(self->ptrX_, self->ptrY_);
    else if (self->hoveredPanel_)
        self->hoveredPanel_->handlePointerMotion(self->ptrX_, self->ptrY_);
}

void App::ptr_button(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    auto* self = static_cast<App*>(data);
    if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
    if (self->pointerOverOverlay_ && self->notificationOverlay_) {
        self->notificationOverlay_->handlePointerButton(self->ptrX_, self->ptrY_, button);
        return;
    }
    if (self->pointerOverOsd_) return;
    if (self->hoveredMenuOwner_)
        self->hoveredMenuOwner_->handlePopupPointerButton(self->ptrX_, self->ptrY_, button);
    else if (self->hoveredPanel_)
        self->hoveredPanel_->handlePointerButton(self->ptrX_, self->ptrY_, button);
}

void App::reg_global_remove(void* data, wl_registry*, uint32_t name) {
    auto* self = static_cast<App*>(data);
    for (auto it = self->outputs_.begin(); it != self->outputs_.end(); ++it) {
        if ((*it)->name_id == name) {
            if (self->lastTrayOutput_ == it->get())
                self->lastTrayOutput_ = nullptr;
            if (self->hoveredPanel_ == (*it)->panel.get())
                self->hoveredPanel_ = nullptr;
            if (self->hoveredMenuOwner_ == (*it)->panel.get())
                self->hoveredMenuOwner_ = nullptr;
            self->outputs_.erase(it);
            break;
        }
    }
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
    fprintf(stderr, "bezel: lock path = %s\n",
            sessionLockMgr_ ? "hard (ext-session-lock-v1)" : "soft (layer-shell fallback)");
    if (idleNotifier_ && seat_) {
        idleService_ = std::make_unique<IdleService>();
        if (idleService_->init(idleNotifier_, seat_, sessionConfig_)) {
            idleService_->subscribe([this] {
                if (!idleService_ || idleService_->state() != IdleState::Idle) return;
                if (sessionConfig_.lock_on_idle && !idleService_->inhibited()) {
                    // LockService lands in Task 4; wire lockService_->lock() there.
                    fprintf(stderr, "bezel: idle -> would lock\n");
                }
            });
        } else {
            idleService_.reset();
        }
    }
    if (!egl_.init(display_)) return false;

    desktopIndex_ = std::make_unique<DesktopIndex>();
    desktopIndex_->scan();
    searchEngine_ = std::make_unique<SearchEngine>(*desktopIndex_, toplevelService_.get());

    controlSocket_ = std::make_unique<ControlSocket>();
    if (!controlSocket_->init())
        controlSocket_.reset();

    busManager_ = std::make_unique<BusManager>();
    if (busManager_->system()) {
        batteryService_ = std::make_unique<BatteryService>(busManager_->system());
        if (!batteryService_->init()) {
            batteryService_.reset();
        }
        networkService_ = std::make_unique<NetworkService>(busManager_->system());
        if (!networkService_->init()) {
            networkService_.reset();
        }
    }
    brightnessService_ = std::make_unique<BrightnessService>();
    if (!brightnessService_->init()) {
        brightnessService_.reset();
    }
    audioService_ = std::make_unique<AudioService>();
    if (!audioService_->init()) {
        audioService_.reset();
    }
    if (busManager_->session()) {
        notificationService_ = std::make_unique<NotificationService>(busManager_->session());
        if (!notificationService_->init()) {
            notificationService_.reset();
        }
        mprisService_ = std::make_unique<MprisService>(busManager_->session(), nullptr);
        if (!mprisService_->init()) {
            mprisService_.reset();
        }
        trayService_ = std::make_unique<TrayService>(busManager_->session(), nullptr);
        if (!trayService_->init()) {
            trayService_.reset();
        }
    }

    for (auto& o : outputs_) create_panel(*o);
    if (notificationService_ && compositor_ && layer_shell_ && !outputs_.empty()) {
        Output* focus = activeOutput();
        notificationOverlay_ = std::make_unique<NotificationOverlay>(
            egl_, theme_, compositor_, layer_shell_,
            focus ? focus->wl : nullptr,
            focus ? focus->scale : 1,
            *notificationService_);
    }
    if ((audioService_ || brightnessService_) && compositor_ && layer_shell_ && !outputs_.empty()) {
        Output* focus = activeOutput();
        osdOverlay_ = std::make_unique<OsdOverlay>(
            egl_, theme_, compositor_, layer_shell_,
            focus ? focus->wl : nullptr,
            focus ? focus->scale : 1);
        if (audioService_) {
            audioService_->subscribe([this] {
                if (osdOverlay_ && audioService_) {
                    OsdState st;
                    st.kind = OsdKind::Volume;
                    st.level = audioService_->info().volume;
                    st.muted = audioService_->info().muted;
                    osdOverlay_->show(st);
                }
            });
        }
        if (brightnessService_) {
            brightnessService_->subscribe([this] {
                if (osdOverlay_ && brightnessService_) {
                    OsdState st;
                    st.kind = OsdKind::Brightness;
                    st.level = brightnessService_->info().level;
                    st.muted = false;
                    osdOverlay_->show(st);
                }
            });
        }
    }
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

        std::vector<pollfd> pollFds;
        pollFds.push_back({wlFd, POLLIN, 0});

        if (busManager_) {
            for (const auto& p : busManager_->pollFds()) {
                pollFds.push_back(p);
            }
        }

        if (brightnessService_ && brightnessService_->inotifyFd() >= 0) {
            pollFds.push_back({brightnessService_->inotifyFd(), POLLIN, 0});
        }

        if (audioService_ && audioService_->pollFd() >= 0) {
            pollFds.push_back({audioService_->pollFd(), POLLIN, 0});
        }

        if (mprisService_ && mprisService_->artFd() >= 0) {
            pollFds.push_back({mprisService_->artFd(), POLLIN | POLLOUT, 0});
        }

        if (controlSocket_) {
            pollFds.push_back({controlSocket_->fd(), POLLIN, 0});
        }

        int timeout = 1000;
        if (busManager_) {
            int busTimeout = busManager_->pollTimeout();
            if (busTimeout >= 0 && busTimeout < timeout) timeout = busTimeout;
        }
        if (mprisService_) {
            int artT = mprisService_->artTimeout();
            if (artT == 0) timeout = 0;
            else if (artT >= 0 && artT < timeout) timeout = artT;
        }

        int ret = poll(pollFds.data(), pollFds.size(), timeout);

        if (ret > 0) {
            if (pollFds[0].revents & POLLIN) {
                if (wl_display_read_events(display_) < 0) break;
            } else {
                wl_display_cancel_read(display_);
            }
        } else {
            wl_display_cancel_read(display_);
        }

        if (busManager_)
            busManager_->processPending();

        if (controlSocket_ && (pollFds.back().revents & POLLIN))
            controlSocket_->dispatch([this](std::string_view cmd){ handleSocketCommand(cmd); });
        
        if (wl_display_dispatch_pending(display_) < 0) {
            break;
        }

        if (batteryService_) {
            batteryService_->tick();
        }
        if (brightnessService_) {
            brightnessService_->tick();
        }
        if (audioService_) {
            audioService_->tick();
        }
        if (networkService_) {
            networkService_->tick();
        }
        if (mprisService_) {
            mprisService_->tick();
        }
        if (trayService_) {
            trayService_->tick();
        }
        if (toplevelService_) {
            toplevelService_->tick();
        }
        if (workspaceService_) {
            workspaceService_->tick();
        }
        if (idleService_) {
            idleService_->tick();
        }
        
        tick();
    }
}

Panel* App::activeLauncherTarget() {
    // Prefer the output that owns the currently-focused toplevel
    if (toplevelService_) {
        for (const auto& t : toplevelService_->toplevels()) {
            if (t->is_closed() || !t->info().activated) continue;
            if (t->info().outputs.empty()) continue;
            wl_output* target_output = t->info().outputs.front();
            for (auto& o : outputs_)
                if (o->wl == target_output && o->panel)
                    return o->panel.get();
        }
    }
    // Fall back to the panel the pointer is over
    if (hoveredPanel_) return hoveredPanel_;
    // Last resort: first panel
    for (auto& o : outputs_)
        if (o->panel) return o->panel.get();
    return nullptr;
}

Output* App::activeOutput() {
    if (toplevelService_) {
        for (const auto& t : toplevelService_->toplevels()) {
            if (t->is_closed() || !t->info().activated) continue;
            if (t->info().outputs.empty()) continue;
            wl_output* target = t->info().outputs.front();
            for (auto& o : outputs_)
                if (o->wl == target) return o.get();
        }
    }
    if (hoveredPanel_) {
        for (auto& o : outputs_)
            if (o->panel.get() == hoveredPanel_) return o.get();
    }
    if (!outputs_.empty()) return outputs_.front().get();
    return nullptr;
}

void App::handleSocketCommand(std::string_view cmd) {
    if (cmd == "toggle_launcher") {
        // If any panel is already in launcher mode, dismiss it
        for (auto& o : outputs_) {
            if (o->panel && o->panel->launcherActive()) {
                o->panel->dismissLauncher();
                return;
            }
        }
        // Otherwise activate on the active output
        Panel* target = activeLauncherTarget();
        if (target && searchEngine_)
            target->activateLauncher(*searchEngine_,
                [this](CommandAction a) { handleCommandAction(a); });
    } else if (cmd == "inhibit" || cmd.rfind("inhibit ", 0) == 0) {
        // parse "0" | "1" | "toggle"; default toggle
        if (!idleService_) {
            fprintf(stderr, "control socket: idle service unavailable\n");
            return;
        }
        std::string arg(cmd.substr(cmd.find(' ') == std::string_view::npos
                                       ? cmd.size() : cmd.find(' ') + 1));
        bool inhibit;
        if (arg == "1") inhibit = true;
        else if (arg == "0") inhibit = false;
        else inhibit = !idleService_->inhibited(); // toggle / bare "inhibit"
        idleService_->setInhibited(inhibit);
        fprintf(stderr, "control socket: idle %s\n", inhibit ? "inhibited" : "uninhibited");
    } else {
        fprintf(stderr, "control socket: unknown command '%.*s'\n",
                (int)cmd.size(), cmd.data());
    }
}

void App::handleCommandAction(CommandAction action) {
    switch (action) {
        case CommandAction::Lock:
            system("loginctl lock-session");
            break;
        case CommandAction::Suspend:
            system("systemctl suspend");
            break;
        case CommandAction::Logout:
            system("loginctl terminate-session \"\"");
            break;
        case CommandAction::QuitBezel:
            running_ = false;
            break;
    }
}

void App::tick() {
    if (!ready_) return;
    if (mprisService_) {
        for (auto& o : outputs_) {
            if (o->panel) {
                mprisService_->setNvgContext(o->panel->rendererNvg());
            }
        }
    }
    if (trayService_) {
        for (auto& o : outputs_) {
            if (o->panel && o->panel->rendererNvg()) {
                trayService_->setNvgContext(o->panel->rendererNvg());
                break;
            }
        }
    }
    if (trayService_) {
        Output* focus = activeOutput();
        if (focus != lastTrayOutput_) {
            if (lastTrayOutput_ && lastTrayOutput_->panel)
                lastTrayOutput_->panel->setTrayEnabled(false);
            if (focus && focus->panel)
                focus->panel->setTrayEnabled(true);
            lastTrayOutput_ = focus;
        }
    }
    if (notificationService_) {
        notificationService_->tick();
    }
    if (notificationOverlay_) {
        Output* focus = activeOutput();
        if (focus) {
            notificationOverlay_->setOutput(focus->wl, focus->scale);
        }
        notificationOverlay_->render();
    }
    if (osdOverlay_) {
        Output* focus = activeOutput();
        if (focus) {
            osdOverlay_->setOutput(focus->wl, focus->scale);
        }
        osdOverlay_->render();
    }
    for (auto& o : outputs_) {
        if (o->panel) {
            o->panel->tick();
            o->panel->render();
        }
    }
}

void App::finish() {
    osdOverlay_.reset();
    notificationOverlay_.reset();
    notificationService_.reset();
    // Panels (and their widgets) must be destroyed before the services they
    // subscribe to, so widget destructors can unsubscribe from live services.
    outputs_.clear();
    trayService_.reset();
    mprisService_.reset();
    networkService_.reset();
    audioService_.reset();
    brightnessService_.reset();
    idleService_.reset();
    toplevelService_.reset();
    workspaceService_.reset();
    if (keyboardInput_) keyboardInput_.reset();
    if (pointer_) { wl_pointer_release(pointer_); pointer_ = nullptr; }
    if (seat_)    { wl_seat_destroy(seat_); seat_ = nullptr; }
    if (idleNotifier_)    { ext_idle_notifier_v1_destroy(idleNotifier_); idleNotifier_ = nullptr; }
    if (sessionLockMgr_)  { ext_session_lock_manager_v1_destroy(sessionLockMgr_); sessionLockMgr_ = nullptr; }
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
