#include "workspace_switcher.hpp"
#include <nanovg.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cstdio>

WorkspaceSwitcher::WorkspaceSwitcher(const Theme& theme, int fontHandle, wl_output* output, WorkspaceService& service, int panelHeight)
    : theme_(theme), fontHandle_(fontHandle), output_(output), service_(service), panelHeight_(panelHeight) {
    subId_ = service_.subscribe([this] {
        updateViewOffset();
        if (callback_) callback_();
    });
}

void WorkspaceSwitcher::setCallback(std::function<void()> cb) {
    callback_ = std::move(cb);
}

void WorkspaceSwitcher::updateViewOffset() {
    const auto* state = service_.get_output_state(output_);
    if (!state || state->workspaces.empty()) return;

    int focused = -1;
    for (int i = 0; i < (int)state->workspaces.size(); ++i) {
        if (state->workspaces[i].focused) { focused = i; break; }
    }
    if (focused < 0) return;

    if (focused < viewOffset_)
        viewOffset_ = focused;
    else if (focused >= viewOffset_ + VISIBLE_COUNT)
        viewOffset_ = focused - VISIBLE_COUNT + 1;
}

WorkspaceSwitcher::~WorkspaceSwitcher() {
    if (subId_ != -1) {
        service_.unsubscribe(subId_);
    }
}

int WorkspaceSwitcher::preferredWidth() const {
    const auto* state = service_.get_output_state(output_);
    if (!state || state->workspaces.empty()) return 0;

    int n = std::min((int)state->workspaces.size(), VISIBLE_COUNT);
    return n * (int)theme_.workspaceMapW + (n - 1) * (int)theme_.gapItem;
}

int WorkspaceSwitcher::preferredHeight() const {
    return (int)theme_.workspaceMapH; // From spec workspace.map token
}

void WorkspaceSwitcher::layout(int x, int y, int w, int h) {
    x_ = x; y_ = y; w_ = w; h_ = h;
}

void WorkspaceSwitcher::handleHover(int x, int y) {
    (void)y;
    hoverX_ = x;
}

void WorkspaceSwitcher::clearHover() {
    hoverX_ = -1;
}

bool WorkspaceSwitcher::handleClick(int x, int y, uint32_t button) {
    if (button != BTN_LEFT) return false;
    for (const auto& r : hitRegions_) {
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            auto* out = service_.dwl_output(output_);
            if (out) zdwl_ipc_output_v2_set_tags(out, 1u << r.index, 0);
            return true;
        }
    }
    return false;
}

void WorkspaceSwitcher::render(const Renderer& renderer) const {
    const auto* state = service_.get_output_state(output_);
    if (!state || state->workspaces.empty()) return;

    hitRegions_.clear();

    NVGcontext* vg = renderer.ctx();
    float curX  = (float)x_;
    float pillW = theme_.workspaceMapW;
    float pillH = (float)panelHeight_;
    float padY  = ((float)panelHeight_ - (float)h_) / 2.0f;
    float fillTop = (float)y_ - padY;

    int first = viewOffset_;
    int last  = std::min(first + VISIBLE_COUNT, (int)state->workspaces.size());
    for (int i = first; i < last; ++i) {
        const auto& ws = state->workspaces[i];

        // Background fill
        nvgBeginPath(vg);
        nvgRect(vg, curX, fillTop, pillW, pillH);
        if (ws.state & 1) {
            nvgFillColor(vg, theme_.accentTint);
        } else if (ws.clients > 0) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 20));
        } else {
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 0));
        }
        nvgFill(vg);

        // Hover overlay
        bool hovered = (hoverX_ >= (int)curX && hoverX_ < (int)(curX + pillW));
        if (hovered) {
            nvgBeginPath(vg);
            nvgRect(vg, curX, fillTop, pillW, pillH);
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 30));
            nvgFill(vg);
        }

        // Accent underline for active workspace (matches window button style)
        if (ws.state & 1) {
            nvgBeginPath(vg);
            nvgRect(vg, curX, fillTop + pillH - 2.0f, pillW, 2.0f);
            nvgFillColor(vg, theme_.accent);
            nvgFill(vg);
        }

        hitRegions_.push_back({(int)ws.index, (int)curX, (int)fillTop, (int)pillW, (int)pillH});

        if (theme_.workspaceMode == Theme::WorkspaceMode::Tiling && !ws.tiles.empty()) {
            for (const auto& tile : ws.tiles) {
                nvgBeginPath(vg);
                nvgRect(vg, curX + tile.x * pillW, fillTop + tile.y * pillH,
                        tile.w * pillW, tile.h * pillH);
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
                nvgFill(vg);
            }
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%u", ws.index + 1);
            nvgFontSize(vg, theme_.labelSecondaryPx);
            nvgFontFaceId(vg, fontHandle_);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            if (ws.state & 1) {
                nvgFillColor(vg, theme_.textPrimary);
            } else if (ws.clients > 0) {
                nvgFillColor(vg, theme_.textSecondary);
            } else {
                nvgFillColor(vg, theme_.textMuted);
            }
            nvgText(vg, curX + pillW / 2.0f, fillTop + pillH / 2.0f, buf, nullptr);
        }

        curX += pillW + theme_.gapItem;
    }
}
