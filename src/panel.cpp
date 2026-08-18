// panel.cpp
#include "panel.hpp"
#include "context_menu_surface.hpp"
#include "egl.hpp"
#include "clock.hpp"
#include "battery_service.hpp"
#include "battery_widget.hpp"
#include "volume_widget.hpp"
#include "brightness_widget.hpp"
#include "network_widget.hpp"
#include "mpris_widget.hpp"
#include "tray_widget.hpp"
#include "tray_tooltip.hpp"
#include "icon_loader.hpp"
#include "toplevel_service.hpp"
#include "window_list.hpp"
#include "workspace_switcher.hpp"
#include "power_button_widget.hpp"
#include <linux/input-event-codes.h>
#include <nanovg.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

Panel::Panel(Theme& theme, Egl& egl, wl_compositor* compositor, zwlr_layer_shell_v1* shell, wl_output* output, const char* name, BatteryService* batteryService, ToplevelService* toplevelService, WorkspaceService* workspaceService, AudioService* audioService, BrightnessService* brightnessService, NetworkService* networkService, MprisService* mprisService, TrayService* trayService, wl_seat* seat)
    : egl_(egl), theme_(theme), compositor_(compositor), layer_shell_(shell), output_(output), seat_(seat), batteryService_(batteryService), toplevelService_(toplevelService), workspaceService_(workspaceService), audioService_(audioService), brightnessService_(brightnessService), networkService_(networkService), mprisService_(mprisService), trayService_(trayService) {
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
            fontHandle_ = fontH >= 0 ? fontH : 0;
            rebuildLayout();
        }
    } else {
        wl_egl_window_resize(egl_window_, physW, physH, 0, 0);
    }
    configured_ = true;
    dirty_ = true;
}

void Panel::rebuildLayout() {
    if (!renderer_) return;
    root_ = std::make_unique<BoxLayout>(BoxOrientation::Horizontal,
                                        theme_.gapItem, theme_.panelPad);
    if (workspaceService_) {
        auto switcher = std::make_unique<WorkspaceSwitcher>(theme_, fontHandle_, output_, *workspaceService_, HEIGHT);
        switcher->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(switcher));
    }
    if (toplevelService_) {
        auto wl = std::make_unique<WindowList>(theme_, fontHandle_,
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
    if (mprisService_) {
        auto mpris = std::make_unique<MprisWidget>(theme_, renderer_->ctx(), fontHandle_, *mprisService_);
        mpris->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(mpris));
    }
    if (networkService_) {
        auto net = std::make_unique<NetworkWidget>(theme_, renderer_->ctx(), fontHandle_, *networkService_);
        net->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(net));
    }
    if (audioService_) {
        auto vol = std::make_unique<VolumeWidget>(theme_, renderer_->ctx(), fontHandle_, *audioService_);
        vol->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(vol));
    }
    if (trayEnabled_ && trayService_) {
        auto tray = std::make_unique<TrayWidget>(theme_, *trayService_, fontHandle_);
        tray->setCallback([this] { requestRedraw(); });
        tray->setTooltipCallbacks(
            [this](int anchorX, const std::string& text) {
                trayTooltip_ = std::make_unique<TrayTooltip>(
                    egl_, theme_, compositor_, layer_shell_, output_,
                    (int)scale_, anchorX, HEIGHT, text);
                // Bottom-anchored panel: HEIGHT is the panel's top edge; the
                // tooltip's bottom margin is panelTopY + gapItem, so the
                // tooltip hugs the panel top with just the gapItem gap.
            },
            [this] { trayTooltip_.reset(); }
        );
        root_->addChild(std::move(tray));
    }
    if (batteryService_) {
        auto battery = std::make_unique<BatteryWidget>(theme_, renderer_->ctx(), fontHandle_, *batteryService_);
        battery->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(battery));
    }
    auto clock = std::make_unique<Clock>(theme_, renderer_->ctx(), fontHandle_);
    clock_ = clock.get();
    root_->addChild(std::move(clock));

    if (powerButtonEnabled_) {
        auto powerBtn = std::make_unique<PowerButtonWidget>(theme_, renderer_->ctx(), fontHandle_);
        if (powerClickCb_)
            powerBtn->setOnClick(powerClickCb_);
        powerBtn->setCallback([this] { requestRedraw(); });
        root_->addChild(std::move(powerBtn));
    }

    requestRedraw();
}

void Panel::setPowerClickCallback(std::function<void()> cb) {
    powerClickCb_ = std::move(cb);
    powerButtonEnabled_ = (bool)powerClickCb_;
    rebuildLayout();
}

void Panel::tick() {
    if (clock_ && clock_->tick()) {
        dirty_ = true;
    }
}

void Panel::requestRedraw() {
    dirty_ = true;
}

NVGcontext* Panel::rendererNvg() const {
    return renderer_ ? renderer_->ctx() : nullptr;
}

void Panel::render() {
    if (!configured_) return;
    if (!renderer_) return;
    if (!dirty_) return;

    dirty_ = false;

    renderer_->beginFrame(width_, height_);
    renderer_->clear();

    if (launcherActive_) {
        renderLauncher();
    } else {
        if (root_) {
            root_->layout(0, 0, width_, height_);
            root_->render(*renderer_);
        }
    }

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
    if (!launcherActive_ && root_) root_->handleClick(x, y, button);
    requestRedraw();
}

void Panel::handlePointerMotion(int x, int y) {
    if (!launcherActive_ && root_) root_->handleHover(x, y);
    requestRedraw();
}

void Panel::handlePointerLeave() {
    if (!launcherActive_ && root_) root_->clearHover();
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

// ── Launcher / command mode ───────────────────────────────────────────────────

void Panel::activateLauncher(SearchEngine& engine,
                              std::function<void(CommandAction)> commandCb) {
    searchEngine_         = &engine;
    commandCb_            = std::move(commandCb);
    launcherActive_       = true;
    launcherQuery_.clear();
    launcherResults_.clear();
    launcherSelectedIdx_  = 0;

    if (!launcherIconLoader_ && renderer_)
        launcherIconLoader_ = std::make_unique<IconLoader>(renderer_->ctx());

    zwlr_layer_surface_v1_set_size(layer_surface_, 0, LAUNCHER_HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, LAUNCHER_HEIGHT);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
    wl_surface_commit(surface_);
    requestRedraw();
}

void Panel::dismissLauncher() {
    launcherActive_  = false;
    searchEngine_    = nullptr;
    launcherQuery_.clear();
    launcherResults_.clear();
    commandCb_       = nullptr;

    zwlr_layer_surface_v1_set_size(layer_surface_, 0, HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, HEIGHT);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_surface_,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(surface_);
    requestRedraw();
}

void Panel::handleLauncherText(std::string_view text) {
    launcherQuery_.append(text);
    updateLauncherResults();
    requestRedraw();
}

void Panel::handleLauncherKey(uint32_t keysym) {
    switch (keysym) {
        case 0xff08: // XKB_KEY_BackSpace
            if (!launcherQuery_.empty()) {
                // pop one UTF-8 codepoint from the tail
                launcherQuery_.pop_back();
                while (!launcherQuery_.empty() && (launcherQuery_.back() & 0xC0) == 0x80)
                    launcherQuery_.pop_back();
                updateLauncherResults();
                requestRedraw();
            }
            break;
        case 0xff1b: // XKB_KEY_Escape
            dismissLauncher();
            break;
        case 0xff0d: // XKB_KEY_Return
        case 0xff8d: // XKB_KEY_KP_Enter
            launchSelected();
            break;
        case 0xff53: // XKB_KEY_Right
            if (!launcherResults_.empty()) {
                launcherSelectedIdx_ = std::min(launcherSelectedIdx_ + 1,
                                                (int)launcherResults_.size() - 1);
                requestRedraw();
            }
            break;
        case 0xff51: // XKB_KEY_Left
            if (!launcherResults_.empty()) {
                launcherSelectedIdx_ = std::max(launcherSelectedIdx_ - 1, 0);
                requestRedraw();
            }
            break;
        default:
            break;
    }
}

void Panel::updateLauncherResults() {
    if (searchEngine_ && !launcherQuery_.empty())
        launcherResults_ = searchEngine_->query(launcherQuery_);
    else
        launcherResults_.clear();
    launcherSelectedIdx_ = 0;
}

void Panel::launchSelected() {
    if (launcherResults_.empty()) { dismissLauncher(); return; }
    // Capture result before dismissing (which clears the vector)
    const SearchResult result = launcherResults_[
        std::min(launcherSelectedIdx_, (int)launcherResults_.size() - 1)];

    // Dismiss first so the launcher surface is gone before the app appears
    auto cb = commandCb_;
    dismissLauncher();

    switch (result.kind) {
        case ResultKind::Application: {
            auto argv = splitExec(result.exec);
            pid_t pid = fork();
            if (pid == 0) {
                if (fork() == 0) {
                    setsid();
                    execlp("gtk-launch", "gtk-launch", result.app_id.c_str(), nullptr);
                    if (!argv.empty()) {
                        std::vector<char*> cargv;
                        for (auto& a : argv) cargv.push_back(a.data());
                        cargv.push_back(nullptr);
                        execvp(cargv[0], cargv.data());
                    }
                    _exit(1);
                }
                _exit(0);
            }
            if (pid > 0) waitpid(pid, nullptr, 0);
            break;
        }
        case ResultKind::Window:
            if (result.toplevel_handle && seat_)
                zwlr_foreign_toplevel_handle_v1_activate(result.toplevel_handle, seat_);
            break;
        case ResultKind::Command:
            if (cb) cb(result.command_action);
            break;
    }
}

std::vector<std::string> Panel::splitExec(const std::string& exec) {
    std::vector<std::string> args;
    std::istringstream ss(exec);
    std::string token;
    while (ss >> token) args.push_back(token);
    return args;
}

void Panel::renderLauncher() {
    NVGcontext* vg = renderer_->ctx();
    if (!vg) return;

    const float pad    = theme_.panelPad;
    const float h      = static_cast<float>(height_);
    const float w      = static_cast<float>(width_);

    // ── Search field ─────────────────────────────────────────────────────────
    const float sfW    = 260.0f;
    const float sfH    = 36.0f;
    const float sfX    = pad;
    const float sfY    = (h - sfH) / 2.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sfX, sfY, sfW, sfH, theme_.radiusControl);
    nvgFillColor(vg, theme_.panelBgElevated);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sfX, sfY, sfW, sfH, theme_.radiusControl);
    nvgStrokeColor(vg, theme_.borderAccent);
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    // Magnifier glyph
    const float glyphPad = 10.0f;
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, theme_.textMuted);
    nvgText(vg, sfX + glyphPad, sfY + sfH / 2.0f, "\xe2\x8c\x95", nullptr); // U+2315 ⌕

    // Query text
    const float textX = sfX + glyphPad + 18.0f;
    const float textY = sfY + sfH / 2.0f;
    nvgFillColor(vg, theme_.textPrimary);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    if (!launcherQuery_.empty()) {
        // Ellipsize from the left if text is too long (show tail of query)
        float maxTextW = sfW - (textX - sfX) - 14.0f;
        float bounds[4];
        nvgTextBounds(vg, 0, 0, launcherQuery_.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] <= maxTextW) {
            nvgText(vg, textX, textY, launcherQuery_.c_str(), nullptr);
        } else {
            // Clip to region and render with scissor
            nvgScissor(vg, textX, sfY, maxTextW, sfH);
            nvgText(vg, textX, textY, launcherQuery_.c_str(), nullptr);
            nvgResetScissor(vg);
        }
    }

    // Cursor (always visible while active)
    float cursorX = textX;
    if (!launcherQuery_.empty()) {
        float bounds[4];
        nvgTextBounds(vg, 0, 0, launcherQuery_.c_str(), nullptr, bounds);
        float textW = bounds[2] - bounds[0];
        float maxW  = sfW - (textX - sfX) - 14.0f;
        cursorX += std::min(textW, maxW);
    }
    nvgBeginPath(vg);
    nvgRect(vg, cursorX, sfY + (sfH - 14.0f) / 2.0f, 1.5f, 14.0f);
    nvgFillColor(vg, theme_.accent);
    nvgFill(vg);

    // ── Results strip ─────────────────────────────────────────────────────────
    if (launcherResults_.empty()) return;

    const float tileW   = 144.0f;
    const float tileGap = 6.0f;
    const float resX    = sfX + sfW + theme_.gapItem;
    const float resW    = w - resX - pad;
    const float tileH   = sfH;
    const float tileY   = sfY;

    int maxTiles = std::max(1, static_cast<int>((resW + tileGap) / (tileW + tileGap)));
    int numTiles = std::min(maxTiles, static_cast<int>(launcherResults_.size()));

    for (int i = 0; i < numTiles; ++i) {
        const auto& r     = launcherResults_[i];
        float tileX = resX + i * (tileW + tileGap);
        bool selected = (i == launcherSelectedIdx_);

        // Tile background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tileX, tileY, tileW, tileH, theme_.radiusTile);
        if (selected) {
            nvgFillColor(vg, theme_.accentTint);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, tileX, tileY, tileW, tileH, theme_.radiusTile);
            nvgStrokeColor(vg, theme_.accent);
            nvgStrokeWidth(vg, 1.0f);
            nvgStroke(vg);
        } else {
            nvgFillColor(vg, theme_.panelBgElevated);
            nvgFill(vg);
        }

        // Icon (16×16, left-padded)
        const float iconSize = 16.0f;
        const float iconPad  = 8.0f;
        const float iconX    = tileX + iconPad;
        const float iconY    = tileY + (tileH - iconSize) / 2.0f;
        bool drewIcon = false;
        if (launcherIconLoader_ && !r.icon_name.empty()) {
            int img = launcherIconLoader_->get(r.icon_name);
            if (img >= 0) {
                NVGpaint paint = nvgImagePattern(vg, iconX, iconY, iconSize, iconSize, 0.0f, img, 1.0f);
                nvgBeginPath(vg);
                nvgRect(vg, iconX, iconY, iconSize, iconSize);
                nvgFillPaint(vg, paint);
                nvgFill(vg);
                drewIcon = true;
            }
        }
        if (!drewIcon) {
            // Placeholder square
            nvgBeginPath(vg);
            nvgRoundedRect(vg, iconX, iconY, iconSize, iconSize, 3.0f);
            NVGcolor ph = theme_.textMuted;
            ph.a *= 0.4f;
            nvgFillColor(vg, ph);
            nvgFill(vg);
        }

        // Labels (name + subtitle stacked)
        const float labelX    = iconX + iconSize + 6.0f;
        const float labelMaxW = tileX + tileW - labelX - iconPad;
        nvgFontFaceId(vg, fontHandle_);
        nvgFontSize(vg, theme_.labelPrimaryPx);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, theme_.textPrimary);
        nvgScissor(vg, labelX, tileY, labelMaxW, tileH);
        nvgText(vg, labelX, tileY + tileH * 0.40f, r.name.c_str(), nullptr);

        nvgFontSize(vg, theme_.labelSecondaryPx);
        nvgFillColor(vg, theme_.textMuted);
        nvgText(vg, labelX, tileY + tileH * 0.72f, r.subtitle.c_str(), nullptr);
        nvgResetScissor(vg);
    }
}

// ── Layer surface callbacks ───────────────────────────────────────────────────

void Panel::handle_configure(void* data, zwlr_layer_surface_v1* s, uint32_t serial, uint32_t w, uint32_t h) {
    auto* self = static_cast<Panel*>(data);
    zwlr_layer_surface_v1_ack_configure(s, serial);
    self->resize((int)w, (int)h);
    self->render();
}

void Panel::handle_closed(void* data, zwlr_layer_surface_v1*) {
    (void)data;
}

void Panel::setTrayEnabled(bool enabled) {
    if (trayEnabled_ == enabled) return;
    trayEnabled_ = enabled;
    if (!enabled) {
        trayTooltip_.reset();
    } else if (trayService_ && renderer_) {
        // Reload icons on this panel's context so the new TrayWidget's
        // SniIconWidgets hold valid image handles. If enabled before the first
        // renderer init (fontHandle_ still -1), rebuildLayout() no-ops anyway
        // and the reload is skipped; in practice the tray is enabled only after
        // the panel is configured.
        trayService_->reloadIcons(renderer_->ctx());
    }
    rebuildLayout();
    requestRedraw();
}

wl_surface* Panel::trayTooltipSurface() const {
    return trayTooltip_ ? trayTooltip_->surface() : nullptr;
}
