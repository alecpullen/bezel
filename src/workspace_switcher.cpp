#include "workspace_switcher.hpp"
#include <nanovg.h>
#include <algorithm>
#include <cstdio>

WorkspaceSwitcher::WorkspaceSwitcher(const Theme& theme, int fontHandle, wl_output* output, WorkspaceService& service)
    : theme_(theme), fontHandle_(fontHandle), output_(output), service_(service) {
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

void WorkspaceSwitcher::render(const Renderer& renderer) const {
    const auto* state = service_.get_output_state(output_);
    if (!state || state->workspaces.empty()) return;

    hitRegions_.clear();

    NVGcontext* vg = renderer.ctx();
    float curX = (float)x_;
    float pillW = theme_.workspaceMapW;
    float pillH = theme_.workspaceMapH;
    float centerY = (float)y_ + ((float)h_ - pillH) / 2.0f;

    int first = viewOffset_;
    int last  = std::min(first + VISIBLE_COUNT, (int)state->workspaces.size());
    for (int i = first; i < last; ++i) {
        const auto& ws = state->workspaces[i];
        nvgBeginPath(vg);
        nvgRoundedRect(vg, curX, centerY, pillW, pillH, theme_.radiusTile);

        if (ws.focused) {
            nvgFillColor(vg, theme_.accentTint);
        } else if (ws.state == 1) {
            nvgFillColor(vg, theme_.panelBgElevated);
        } else if (ws.clients > 0) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 20));
        } else {
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 0));
        }
        nvgFill(vg);

        if (ws.focused) {
            nvgStrokeColor(vg, theme_.borderAccent);
            nvgStrokeWidth(vg, 2.0f);
            nvgStroke(vg);
        }

        hitRegions_.push_back({(int)ws.index, (int)curX, (int)centerY, (int)pillW, (int)pillH});

        if (theme_.workspaceMode == Theme::WorkspaceMode::Tiling && !ws.tiles.empty()) {
            for (const auto& tile : ws.tiles) {
                nvgBeginPath(vg);
                nvgRect(vg, curX + tile.x * pillW, centerY + tile.y * pillH,
                        tile.w * pillW, tile.h * pillH);
                nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
                nvgFill(vg);
            }
        } else {
            // Flat-icons fallback: draw index text centered
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%u", ws.index + 1);
            nvgFontSize(vg, theme_.labelSecondaryPx);
            nvgFontFaceId(vg, fontHandle_);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

            if (ws.focused || ws.state == 1) {
                nvgFillColor(vg, theme_.textPrimary);
            } else if (ws.clients > 0) {
                nvgFillColor(vg, theme_.textSecondary);
            } else {
                nvgFillColor(vg, theme_.textMuted);
            }
            nvgText(vg, curX + pillW / 2.0f, centerY + pillH / 2.0f, buf, nullptr);
        }

        curX += pillW + theme_.gapItem;
    }
}
