#pragma once

#include "theme.hpp"
#include "egl.hpp"
#include <cstdint>
#include <wayland-client-protocol.h>

struct NVGcontext;

class Renderer {
public:
    Renderer(Theme& theme, Egl& egl);

    bool init(EGLSurface eglSurface);
    void destroy();

    void beginFrame(int panelWidth, int panelHeight);
    void endFrame();

    void setScissor(int x, int y, int w, int h);
    void setPixelRatio(float ratio);

    void clear(const NVGcolor& color);
    void clear();

    NVGcontext* ctx() const { return ctx_; }
    const Theme& theme() const { return theme_; }

private:
    Theme& theme_;
    Egl& egl_;
    NVGcontext* ctx_ = nullptr;
    float pixelRatio_ = 1.0f;
};
