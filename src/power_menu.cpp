#include "power_menu.hpp"
#include "egl.hpp"
#include <nanovg.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <linux/input-event-codes.h>
#include <cstdio>

namespace {
const char* actionName(PowerAction a) {
    switch (a) {
        case PowerAction::Lock:      return "Lock";
        case PowerAction::Logout:    return "Log Out";
        case PowerAction::Suspend:   return "Suspend";
        case PowerAction::Hibernate: return "Hibernate";
        case PowerAction::Reboot:    return "Reboot";
        case PowerAction::ShutDown:  return "Shut Down";
    }
    return "Unknown";
}
} // namespace

PowerMenu::PowerMenu(Egl& egl, Theme& theme, wl_compositor* compositor,
                     zwlr_layer_shell_v1* shell, wl_output* output, int scale)
    : egl_(egl), theme_(theme), compositor_(compositor), shell_(shell),
      output_(output), scale_(scale > 0 ? scale : 1) {}

PowerMenu::~PowerMenu() {
    hide();
}

void PowerMenu::setOutput(wl_output* output, int scale) {
    if (output == output_ && scale == scale_) return;
    bool wasVisible = visible_;
    output_ = output;
    scale_ = scale > 0 ? scale : 1;
    if (wasVisible) {
        destroySurface();
        createSurface();
        render();
    }
}

void PowerMenu::createSurface() {
    surface_ = wl_compositor_create_surface(compositor_);
    layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
        shell_, surface_, output_, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "power-menu");

    static const zwlr_layer_surface_v1_listener listener = {
        .configure = handle_configure,
        .closed    = handle_closed,
    };
    zwlr_layer_surface_v1_add_listener(layer_surface_, &listener, this);

    zwlr_layer_surface_v1_set_anchor(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(layer_surface_, 0, 12, 60, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

    wl_surface_set_buffer_scale(surface_, scale_);
    wl_surface_commit(surface_);
    configured_ = false;
}

void PowerMenu::destroySurface() {
    if (renderer_) renderer_->destroy();
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_.display(), egl_surface_);
    if (egl_window_) wl_egl_window_destroy(egl_window_);
    if (layer_surface_) zwlr_layer_surface_v1_destroy(layer_surface_);
    if (surface_) wl_surface_destroy(surface_);
    renderer_.reset();
    fontCache_.reset();
    surface_ = nullptr;
    layer_surface_ = nullptr;
    egl_window_ = nullptr;
    egl_surface_ = EGL_NO_SURFACE;
    configured_ = false;
}

void PowerMenu::show(std::vector<PowerAction> actions) {
    if (actions.empty()) return;
    actions_ = std::move(actions);
    sel_ = 0;
    confirmArmed_ = false;
    visible_ = true;
    if (!surface_) createSurface();
    if (layer_surface_ && configured_) {
        int h = (int)actions_.size() * ITEM_H + PAD * 2;
        zwlr_layer_surface_v1_set_size(layer_surface_, MENU_W, h);
        wl_surface_commit(surface_);
    }
    render();
}

void PowerMenu::hide() {
    if (!visible_ && !surface_) return;
    visible_ = false;
    actions_.clear();
    destroySurface();
    if (onHide_) onHide_();
}

void PowerMenu::handleKey(const KeyEvent& ev) {
    if (!visible_ || !ev.pressed) return;
    switch (ev.keysym) {
        case XKB_KEY_Escape:
            hide();
            break;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_space:
            confirm();
            break;
        case XKB_KEY_Up:
            sel_ = (sel_ - 1 + (int)actions_.size()) % (int)actions_.size();
            confirmArmed_ = false;
            render();
            break;
        case XKB_KEY_Down:
            sel_ = (sel_ + 1) % (int)actions_.size();
            confirmArmed_ = false;
            render();
            break;
        default:
            break;
    }
}

void PowerMenu::confirm() {
    if (actions_.empty()) return;
    PowerAction action = actions_[sel_];
    if (onAction_) {
        hide();
        onAction_(action);
    } else {
        hide();
    }
}

void PowerMenu::handlePointerButton(int x, int y, uint32_t button) {
    if (!visible_ || button != BTN_LEFT) return;
    if (x < 0 || x >= MENU_W) { hide(); return; }
    int idx = (y - PAD) / ITEM_H;
    if (idx >= 0 && idx < (int)actions_.size()) {
        sel_ = idx;
        render();
        confirm();
    }
}

void PowerMenu::handlePointerMotion(int, int y) {
    if (!visible_) return;
    int idx = (y - PAD) / ITEM_H;
    if (idx >= 0 && idx < (int)actions_.size() && idx != sel_) {
        sel_ = idx;
        render();
    }
}

void PowerMenu::render() {
    if (!visible_ || !configured_ || !renderer_ || fontHandle_ < 0) return;
    renderer_->beginFrame(width_, height_);
    renderer_->clear();
    NVGcontext* vg = renderer_->ctx();

    int n = (int)actions_.size();
    int menuH = n * ITEM_H + PAD * 2;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, (float)MENU_W, (float)menuH, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBg);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.5f, 0.5f, (float)MENU_W - 1.0f, (float)menuH - 1.0f, theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderHairline);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    nvgFontFaceId(vg, fontHandle_);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    for (int i = 0; i < n; ++i) {
        int itemY = PAD + i * ITEM_H;
        bool selected = (i == sel_);
        if (selected) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, 4, itemY + 2, MENU_W - 8, ITEM_H - 4, theme_.radiusTile);
            nvgFillColor(vg, theme_.accentTint);
            nvgFill(vg);
        }
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgFillColor(vg, selected ? theme_.accent : theme_.textPrimary);
        nvgText(vg, 14, itemY + ITEM_H / 2.0f, actionName(actions_[i]), nullptr);
    }

    renderer_->endFrame();
    eglSwapBuffers(egl_.display(), egl_surface_);
}

void PowerMenu::handle_configure(void* data, zwlr_layer_surface_v1* s,
                                 uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<PowerMenu*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);

    self->width_  = w > 0 ? (int)w : self->MENU_W;
    self->height_ = h > 0 ? (int)h : (int)self->actions_.size() * ITEM_H + PAD * 2;
    int physW = self->width_ * self->scale_;
    int physH = self->height_ * self->scale_;

    if (!self->egl_window_) {
        self->egl_window_ = wl_egl_window_create(self->surface_, physW, physH);
        self->egl_surface_ = eglCreateWindowSurface(
            self->egl_.display(), self->egl_.config(),
            (EGLNativeWindowType)self->egl_window_, nullptr);
        self->renderer_ = std::make_unique<Renderer>(self->theme_, self->egl_);
        if (!self->renderer_->init(self->egl_surface_)) {
            std::fprintf(stderr, "PowerMenu: renderer init failed\n");
            self->renderer_.reset();
            return;
        }
        self->renderer_->setPixelRatio((float)self->scale_);
        self->fontCache_ = std::make_unique<FontCache>(self->renderer_->ctx());
        self->fontHandle_ = self->fontCache_->loadSans();
    } else {
        wl_egl_window_resize(self->egl_window_, physW, physH, 0, 0);
    }

    self->configured_ = true;
    self->render();
}

void PowerMenu::handle_closed(void* data, zwlr_layer_surface_v1*) {
    auto* self = static_cast<PowerMenu*>(data);
    self->visible_ = false;
    self->configured_ = false;
}
