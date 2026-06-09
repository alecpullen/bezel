// panel.cpp
#include "panel.hpp"
#include "context_menu_surface.hpp"
#include "egl.hpp"
#include "clock.hpp"
#include "battery_service.hpp"
#include "battery_widget.hpp"
#include "toplevel_service.hpp"
#include "window_list.hpp"
#include "workspace_switcher.hpp"
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <algorithm>
#include <cstdio>

Panel::Panel(Theme& theme, Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell, wl_output* output, const char* name, BatteryService* batteryService, ToplevelService* toplevelService, WorkspaceService* workspaceService, wl_seat* seat)
    : egl_(egl), theme_(theme), compositor_(compositor), layer_shell_(shell), output_(output), seat_(seat), batteryService_(batteryService), toplevelService_(toplevelService), workspaceService_(workspaceService) {
    surface_ = wl_compositor_create_surface(compositor);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(shell, surface_, output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, name);

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(layer_surface_, 0, HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, HEIGHT);

    wl_surface_commit(surface_);
}

Panel::~Panel() {
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_)                    wl_egl_window_destroy(egl_window_);
    if (layer_surface_)                 zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_)                       wl_surface_destroy(surface_);
}

void Panel::setScale(int scale) {
    scale_ = static_cast<float>(scale > 0 ? scale : 1);
    if (renderer_) renderer_->setPixelRatio(scale_);
    if (egl_window_) {
        wl_surface_set_buffer_scale(surface_, scale > 0 ? scale : 1);
        wl_egl_window_resize(egl_window_, width_ * scale, height_ * scale, 0, 0);
        dirty_ = true;
    }
}

void Panel::resize(int w, int h) {
    width_  = w > 0 ? w : width_;
    height_ = h > 0 ? h : HEIGHT;
    int physW = width_  * static_cast<int>(scale_);
    int physH = height_ * static_cast<int>(scale_);
    if (!egl_window_) {
        wl_surface_set_buffer_scale(surface_, static_cast<int32_t>(scale_));
        egl_window_  = wl_egl_window_create(surface_, physW, physH);
        egl_surface_ = eglCreateWindowSurface(egl_.display(), egl_.config(), (EGLNativeWindowType)egl_window_, nullptr);
        if (!renderer_) {
            renderer_ = std::make_unique<Renderer>(theme_, egl_);
            if (!renderer_->init(egl_surface_)) {
                std::fprintf(stderr, "Panel: Renderer init failed on output\n");
                renderer_.reset();
                return;
            }
            renderer_->setPixelRatio(scale_);
            fontCache_ = std::make_unique<FontCache>(renderer_->ctx());
            int fontH = fontCache_->loadSans();

            root_ = std::make_unique<BoxLayout>(BoxOrientation::Horizontal,
                                                theme_.gapItem, theme_.panelPad);
            if (workspaceService_) {
                auto switcher = std::make_unique<WorkspaceSwitcher>(theme_, fontH >= 0 ? fontH : 0, output_, *workspaceService_, HEIGHT);
                switcher->setCallback([this] { requestRedraw(); });
                root_->addChild(std::move(switcher));
            }
            if (toplevelService_) {
                auto wl = std::make_unique<WindowList>(theme_, fontH >= 0 ? fontH : 0,
                                                       renderer_->ctx(), output_, *toplevelService_, HEIGHT);
                wl->setCallback([this] { requestRedraw(); });
                wl->setSeat(seat_);
                wl->setContextMenuCallback([this](WindowList::ContextMenuRequest req) {
                    contextMenuSurface_.reset();
                    int ax = std::min(req.anchorX, width_ - ContextMenuSurface::MENU_W);
                    bool pinned = pinnedAppIds_.count(req.app_id) > 0;
                    contextMenuSurface_ = std::make_unique<ContextMenuSurface>(
                        egl_, theme_, compositor_, layer_shell_, output_,
                        ax, pinned, req.handle, req.app_id,
                        [this, req] {
                            if (req.handle)
                                zwlr_foreign_toplevel_handle_v1_close(req.handle);
                            contextMenuSurface_.reset();
                        },
                        [this, req] {
                            togglePin(req.app_id);
                            contextMenuSurface_.reset();
                        });
                });
                windowList_ = wl.get();
                root_->addChild(std::move(wl));
            }
            root_->addSpacer();
            if (batteryService_) {
                auto battery = std::make_unique<BatteryWidget>(theme_, renderer_->ctx(), fontH >= 0 ? fontH : 0, *batteryService_);
                battery->setCallback([this] {
                    requestRedraw();
                });
                root_->addChild(std::move(battery));
            }
            auto clock = std::make_unique<Clock>(theme_, renderer_->ctx(),
                                                 fontH >= 0 ? fontH : 0);
            clock_ = clock.get();
            root_->addChild(std::move(clock));
        }
    } else {
        wl_egl_window_resize(egl_window_, physW, physH, 0, 0);
    }
    configured_ = true;
    dirty_ = true;
}

void Panel::tick() {
    if (clock_ && clock_->tick()) {
        dirty_ = true;
    }
}

void Panel::requestRedraw() {
    dirty_ = true;
}

void Panel::render() {
    if (!configured_) return;
    if (!renderer_ || !root_) return;
    if (!dirty_) return;

    dirty_ = false;

    renderer_->beginFrame(width_, height_);
    renderer_->clear();

    root_->layout(0, 0, width_, height_);
    root_->render(*renderer_);

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void Panel::setSeat(wl_seat* seat) {
    seat_ = seat;
    if (windowList_) windowList_->setSeat(seat_);
}

void Panel::handlePointerButton(int x, int y, uint32_t button) {
    if (button != BTN_LEFT && button != BTN_RIGHT) return;
    contextMenuSurface_.reset();
    if (root_) root_->handleClick(x, y, button);
    requestRedraw();
}

void Panel::handlePointerMotion(int x, int y) {
    if (root_) root_->handleHover(x, y);
    requestRedraw();
}

void Panel::handlePointerLeave() {
    if (root_) root_->clearHover();
    requestRedraw();
}

void Panel::handlePopupPointerMotion(int x, int y) {
    if (contextMenuSurface_) contextMenuSurface_->handlePointerMotion(x, y);
}

void Panel::handlePopupPointerButton(int x, int y, uint32_t button) {
    if (contextMenuSurface_) {
        contextMenuSurface_->handlePointerButton(x, y, button);
        contextMenuSurface_.reset();
        requestRedraw();
    }
}

wl_surface* Panel::popupSurface() const {
    return contextMenuSurface_ ? contextMenuSurface_->surface() : nullptr;
}

void Panel::togglePin(const std::string& app_id) {
    if (pinnedAppIds_.count(app_id))
        pinnedAppIds_.erase(app_id);
    else
        pinnedAppIds_.insert(app_id);
    if (windowList_) windowList_->setPinnedAppIds(pinnedAppIds_);
}


void Panel::handle_configure(void* data, zwlr_layer_surface_v1* s, uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<Panel*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);
    self->resize((int)w, (int)h);
    self->render();
}

void Panel::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}

