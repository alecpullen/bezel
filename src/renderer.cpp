// renderer.cpp
#include "renderer.hpp"

#include <cstdio>
#include <GLES2/gl2.h>
#include <nanovg.h>
#define NANOVG_GLES2
#include <nanovg_gl.h>

Renderer::Renderer(Theme& theme, Egl& egl) : theme_(theme), egl_(egl) {}

bool Renderer::init(EGLSurface eglSurface) {
    if (!egl_.display() || !egl_.context()) return false;
    eglSurface_ = eglSurface;
    if (!eglMakeCurrent(egl_.display(), eglSurface_, eglSurface_, egl_.context()))
        return false;

    ctx_ = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!ctx_) {
        std::fprintf(stderr, "Renderer: nvgCreateGLES2 failed\n");
        return false;
    }

    return true;
}

void Renderer::destroy() {
    if (ctx_) {
        nvgDeleteGLES2(ctx_);
        ctx_ = nullptr;
    }
}

void Renderer::beginFrame(int panelWidth, int panelHeight) {
    eglMakeCurrent(egl_.display(), eglSurface_, eglSurface_, egl_.context());
    nvgBeginFrame(ctx_, panelWidth, panelHeight, pixelRatio_);
}

void Renderer::endFrame() {
    nvgEndFrame(ctx_);
}

void Renderer::setScissor(int x, int y, int w, int h) {
    nvgScissor(ctx_, static_cast<float>(x), static_cast<float>(y),
               static_cast<float>(w), static_cast<float>(h));
}

void Renderer::setPixelRatio(float ratio) {
    pixelRatio_ = ratio;
}

void Renderer::clear(const NVGcolor& color) {
    nvgBeginPath(ctx_);
    nvgRect(ctx_, 0, 0, 4096, 4096);
    nvgFillColor(ctx_, color);
    nvgFill(ctx_);
}

void Renderer::clear() {
    clear(theme_.panelBg);
}
