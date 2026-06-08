// egl.hpp
#pragma once
#include <EGL/egl.h>

struct wl_display;

class Egl {
    public:
        bool init(wl_display* display);
        void finish();
        EGLDisplay display() const { return dpy_; }
        EGLConfig  config()  const { return cfg_; }
        EGLContext context() const { return ctx_; }

    private:
        EGLDisplay dpy_ = EGL_NO_DISPLAY;
        EGLConfig  cfg_ = nullptr;
        EGLContext ctx_ = EGL_NO_CONTEXT;
};
