// egl.cpp
#include "egl.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <cstdio>

bool Egl::init(wl_display* display) {
    auto get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display)
        dpy_ = get_platform_display(EGL_PLATFORM_WAYLAND_EXT, display, nullptr);
    else
        dpy_ = eglGetDisplay((EGLNativeDisplayType)display);

    if (dpy_ == EGL_NO_DISPLAY) {
        fprintf(stderr, "eglGetDisplay failed\n");
        return false;
    }

    if (!eglInitialize(dpy_, nullptr, nullptr)) {
        fprintf(stderr, "eglInitialise failed\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "eglBindAPI failed\n");
        return false;
    }

    const EGLint cfg_attribs[] {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(dpy_, cfg_attribs, &cfg_, 1, &n) || n < 1) {
        fprintf(stderr, "eglChooseConfig failed\n");
        return false;
    }
    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx_ = eglCreateContext(dpy_, cfg_, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx_ == EGL_NO_CONTEXT) {
        fprintf(stderr, "eglCreateContext failed\n");
        return false;
    }
    return true;
}

void Egl::finish() {
    if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
    if (dpy_ != EGL_NO_DISPLAY) eglTerminate(dpy_);
}
