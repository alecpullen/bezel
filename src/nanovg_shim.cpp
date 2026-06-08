// nanovg_shim.cpp
// NanoVG is header-only; this TU materialises the implementation by including
// the implementation headers with the correct define macros.
//
// GLES2 headers are required before nanovg_gl.h so GL types are in scope.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>

#define NVG_IMPLEMENTATION
#include <nanovg.h>
#define NANOVG_GLES2_IMPLEMENTATION
#include <nanovg_gl.h>
