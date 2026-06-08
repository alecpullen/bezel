#pragma once

#include <cstdint>
#include <string_view>

#include <nanovg.h>

struct Theme {
    NVGcolor panelBg;
    NVGcolor panelBgElevated;
    NVGcolor desktopBg;
    NVGcolor textPrimary;
    NVGcolor textSecondary;
    NVGcolor textMuted;
    NVGcolor accent;
    NVGcolor accentTint;
    NVGcolor borderHairline;
    NVGcolor borderEmphasis;
    NVGcolor borderAccent;

    NVGcolor green;
    NVGcolor cyan;
    NVGcolor magenta;
    NVGcolor yellow;
    NVGcolor red;

    float labelPrimaryPx;
    float labelSecondaryPx;
    float largeClockPx;
    float minFontSize;
    int regularWeight;
    int activeWeight;

    float radiusTile;
    float radiusControl;
    float radiusWindow;
    float tileSizeHorizontal;
    float tileSizeVertical;
    float workspaceMapW;
    float workspaceMapH;
    float panelPad;
    float gapItem;
    float iconInline;

    static NVGcolor hex(uint32_t hex) {
        return nvgRGBA(
            static_cast<unsigned char>((hex >> 16) & 0xFF),
            static_cast<unsigned char>((hex >> 8)  & 0xFF),
            static_cast<unsigned char>((hex >> 0)  & 0xFF),
            255
        );
    }

    static NVGcolor hexF(float r, float g, float b, float a = 1.0f) {
        return nvgRGBAf(r, g, b, a);
    }

    static Theme defaultTheme() {
        return {
            .panelBg           = hex(0x16161e),
            .panelBgElevated   = hexF(0.75f, 0.79f, 0.96f, 0.07f),
            .desktopBg         = hex(0x1a1b26),
            .textPrimary       = hex(0xc0caf5),
            .textSecondary     = hex(0xa9b1d6),
            .textMuted         = hex(0x565f89),
            .accent            = hex(0x7aa2f7),
            .accentTint        = hexF(0.48f, 0.64f, 0.97f, 0.16f),
            .borderHairline    = hexF(0.75f, 0.79f, 0.96f, 0.10f),
            .borderEmphasis    = hexF(0.75f, 0.79f, 0.96f, 0.14f),
            .borderAccent      = hexF(0.48f, 0.64f, 0.97f, 0.40f),

            .green             = hex(0x9ece6a),
            .cyan              = hex(0x7dcfff),
            .magenta           = hex(0xbb9af7),
            .yellow            = hex(0xe0af68),
            .red               = hex(0xf7768e),

            .labelPrimaryPx    = 12.0f,
            .labelSecondaryPx  = 11.0f,
            .largeClockPx      = 22.0f,
            .minFontSize       = 11.0f,
            .regularWeight     = 400,
            .activeWeight      = 500,

            .radiusTile        = 8.0f,
            .radiusControl     = 10.0f,
            .radiusWindow      = 10.0f,
            .tileSizeHorizontal= 34.0f,
            .tileSizeVertical  = 40.0f,
            .workspaceMapW     = 36.0f,
            .workspaceMapH     = 26.0f,
            .panelPad          = 12.0f,
            .gapItem           = 8.0f,
            .iconInline        = 16.0f,
        };
    }
};
