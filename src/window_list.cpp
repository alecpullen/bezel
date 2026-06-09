#include "window_list.hpp"
#include "icon_loader.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>

static constexpr float ICON_SIZE          = 24.0f;
static constexpr float ICON_PAD           = 16.0f;
static constexpr float ICON_TEXT_GAP      = 10.0f;
static constexpr float BUTTON_WIDTH       = 160.0f;
static constexpr float GHOST_BUTTON_WIDTH = ICON_SIZE + ICON_PAD * 2; // 56px, icon-only

WindowList::WindowList(const Theme& theme, int fontHandle, NVGcontext* ctx,
                       wl_output* output, ToplevelService& service, int panelHeight)
    : theme_(theme), fontHandle_(fontHandle), output_(output), service_(service), panelHeight_(panelHeight) {
    if (ctx)
        iconLoader_ = std::make_unique<IconLoader>(ctx);
    subId_ = service_.subscribe([this] {
        if (callback_) callback_();
    });
}

WindowList::~WindowList() {
    if (subId_ != -1)
        service_.unsubscribe(subId_);
}

void WindowList::setCallback(std::function<void()> cb) {
    callback_ = std::move(cb);
}

std::vector<const ToplevelInfo*> WindowList::visibleToplevels() const {
    std::vector<const ToplevelInfo*> visible;
    for (const auto& t : service_.toplevels()) {
        if (t->is_closed()) continue;
        const auto& info = t->info();
        if (info.minimized) continue;
        if (!info.outputs.empty() && !info.is_on_output(output_)) continue;
        visible.push_back(&info);
    }
    return visible;
}

int WindowList::preferredWidth() const {
    auto visible = visibleToplevels();

    static constexpr int MAX_VISIBLE_FOR_PREF = 20;
    int runCount = std::min((int)visible.size(), MAX_VISIBLE_FOR_PREF);

    int ghostCount = 0;
    for (const auto& id : pinnedAppIds_) {
        bool running = std::any_of(visible.begin(), visible.end(),
            [&id](const ToplevelInfo* i) { return i->app_id == id; });
        if (!running) ++ghostCount;
    }

    return static_cast<int>(runCount * BUTTON_WIDTH + ghostCount * GHOST_BUTTON_WIDTH);
}

void WindowList::render(const Renderer& renderer) const {
    buttonRegions_.clear();

    auto visible = visibleToplevels();
    NVGcontext* vg = renderer.ctx();
    float curX = (float)x_;

    if (!visible.empty() && w_ > 0) {
        int count = (int)visible.size();
        int maxVisible = std::max(1, static_cast<int>(w_ / BUTTON_WIDTH));
        if (count > maxVisible) {
            auto activeIt = std::find_if(visible.begin(), visible.end(),
                [](const ToplevelInfo* i) { return i->activated; });
            if (activeIt != visible.end() && std::distance(visible.begin(), activeIt) >= maxVisible)
                std::swap(visible[0], *activeIt);
            visible.resize(maxVisible);
        }

        for (const auto* info : visible) {
            zwlr_foreign_toplevel_handle_v1* handle = nullptr;
            for (const auto& t : service_.toplevels())
                if (&t->info() == info) { handle = t->handle(); break; }
            bool hovered = (hoverX_ >= (int)curX && hoverX_ < (int)(curX + BUTTON_WIDTH));
            buttonRegions_.push_back({handle, info->app_id, (int)curX, (int)BUTTON_WIDTH});
            drawButton(vg, curX, BUTTON_WIDTH, *info, hovered);
            curX += BUTTON_WIDTH;
        }
    }

    // Ghost buttons for pinned apps not currently running
    float remaining = (float)w_ - (curX - (float)x_);
    for (const auto& id : pinnedAppIds_) {
        if (remaining < GHOST_BUTTON_WIDTH) break;
        bool running = false;
        for (const auto* info : visible)
            if (info->app_id == id) { running = true; break; }
        if (running) continue;
        bool hovered = (hoverX_ >= (int)curX && hoverX_ < (int)(curX + GHOST_BUTTON_WIDTH));
        buttonRegions_.push_back({nullptr, id, (int)curX, (int)GHOST_BUTTON_WIDTH});
        drawGhostButton(vg, curX, GHOST_BUTTON_WIDTH, id, hovered);
        curX += GHOST_BUTTON_WIDTH;
        remaining -= GHOST_BUTTON_WIDTH;
    }
}

void WindowList::setPinnedAppIds(const std::set<std::string>& ids) {
    pinnedAppIds_ = ids;
    if (callback_) callback_();
}

void WindowList::setContextMenuCallback(std::function<void(ContextMenuRequest)> cb) {
    contextMenuCb_ = std::move(cb);
}

void WindowList::handleHover(int x, int y) {
    (void)y;
    hoverX_ = x;
}

void WindowList::clearHover() {
    hoverX_ = -1;
}

bool WindowList::handleClick(int x, int y, uint32_t button) {
    (void)y;
    for (const auto& r : buttonRegions_) {
        if (x >= r.x && x < r.x + r.w) {
            if (button == BTN_LEFT) {
                if (r.handle && seat_) {
                    zwlr_foreign_toplevel_handle_v1_activate(r.handle, seat_);
                    return true;
                }
                if (!r.handle && !r.app_id.empty()) {
                    std::string app = r.app_id; // copy before fork touches the vector
                    pid_t pid = fork();
                    if (pid == 0) {
                        if (fork() == 0) {
                            setsid();
                            // gtk-launch handles desktop-file IDs (org.foo.Bar, etc.)
                            execlp("gtk-launch", "gtk-launch", app.c_str(), nullptr);
                            // fallback: exec directly (works for simple names like "foot")
                            const char* argv[] = { app.c_str(), nullptr };
                            execvp(app.c_str(), const_cast<char* const*>(argv));
                            _exit(1);
                        }
                        _exit(0);
                    }
                    if (pid > 0) waitpid(pid, nullptr, 0);
                    return true;
                }
            }
            if (button == BTN_RIGHT && contextMenuCb_) {
                contextMenuCb_({r.handle, r.app_id, r.x});
                return true;
            }
        }
    }
    return false;
}

static std::string appName(const ToplevelInfo& info) {
    if (!info.app_id.empty()) return info.app_id;
    return info.title;
}

void WindowList::drawButton(NVGcontext* vg, float btnX, float btnW,
                             const ToplevelInfo& info, bool hovered) const {
    float padY = (panelHeight_ - h_) / 2.0f;
    float fillTop = (float)y_ - padY;

    if (info.activated || hovered) {
        nvgBeginPath(vg);
        nvgRect(vg, btnX, fillTop, btnW, (float)panelHeight_);
        nvgFillColor(vg, info.activated ? theme_.panelBgElevated : nvgRGBA(255, 255, 255, 20));
        nvgFill(vg);
    }

    // App icon (if available)
    float textStartX = btnX + ICON_PAD;
    if (iconLoader_) {
        int img = iconLoader_->get(info.app_id);
        if (img >= 0) {
            float ix = btnX + ICON_PAD;
            float iy = (float)y_ + ((float)h_ - ICON_SIZE) / 2.0f;
            NVGpaint paint = nvgImagePattern(vg, ix, iy, ICON_SIZE, ICON_SIZE, 0.0f, img, 1.0f);
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, ICON_SIZE, ICON_SIZE);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
            textStartX = ix + ICON_SIZE + ICON_TEXT_GAP;
        }
    }

    // App name text, vertically centred, ellipsized
    nvgFontFaceId(vg, fontHandle_);
    nvgFontSize(vg, theme_.labelPrimaryPx);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, info.activated ? theme_.textPrimary : theme_.textSecondary);
    float textEndX = btnX + btnW - ICON_PAD;
    drawEllipsized(vg, textStartX, (float)y_ + (float)h_ / 2.0f,
                   textEndX - textStartX, appName(info));

    // 2px accent underline at bottom for active window
    if (info.activated) {
        nvgBeginPath(vg);
        nvgRect(vg, btnX, fillTop + panelHeight_ - 2.0f, btnW, 2.0f);
        nvgFillColor(vg, theme_.accent);
        nvgFill(vg);
    }
}

void WindowList::drawGhostButton(NVGcontext* vg, float btnX, float btnW,
                                 const std::string& app_id, bool hovered) const {
    if (hovered) {
        float padY = (panelHeight_ - h_) / 2.0f;
        nvgBeginPath(vg);
        nvgRect(vg, btnX, (float)y_ - padY, btnW, (float)panelHeight_);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 20));
        nvgFill(vg);
    }

    float iy = (float)y_ + ((float)h_ - ICON_SIZE) / 2.0f;
    float ix = btnX + (btnW - ICON_SIZE) / 2.0f;

    if (iconLoader_) {
        int img = iconLoader_->get(app_id);
        if (img >= 0) {
            NVGpaint paint = nvgImagePattern(vg, ix, iy, ICON_SIZE, ICON_SIZE, 0.0f, img, 1.0f);
            nvgBeginPath(vg);
            nvgRect(vg, ix, iy, ICON_SIZE, ICON_SIZE);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
            return;
        }
    }

    // Fallback: dimmed placeholder square
    nvgBeginPath(vg);
    nvgRoundedRect(vg, ix, iy, ICON_SIZE, ICON_SIZE, 4.0f);
    NVGcolor fill = theme_.textMuted;
    fill.a *= 0.4f;
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

void WindowList::drawEllipsized(NVGcontext* vg, float x, float y, float maxW,
                                 const std::string& text) const {
    if (text.empty() || maxW <= 0) return;

    float bounds[4];
    nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    if (bounds[2] - bounds[0] <= maxW) {
        nvgText(vg, x, y, text.c_str(), nullptr);
        return;
    }

    std::string truncated = text;
    while (!truncated.empty()) {
        std::string candidate = truncated + "\xe2\x80\xa6"; // U+2026 ellipsis
        nvgTextBounds(vg, 0, 0, candidate.c_str(), nullptr, bounds);
        if (bounds[2] - bounds[0] <= maxW) {
            nvgText(vg, x, y, candidate.c_str(), nullptr);
            return;
        }
        // Pop one UTF-8 codepoint from the end
        truncated.pop_back();
        while (!truncated.empty() && (truncated.back() & 0xC0) == 0x80)
            truncated.pop_back();
    }
}
