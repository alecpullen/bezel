#include "sni_icon_loader.hpp"
#include <nanovg.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#include <cairo.h>
#endif

namespace fs = std::filesystem;

SniIconLoader::SniIconLoader(NVGcontext* vg) : vg_(vg) {}
SniIconLoader::~SniIconLoader() = default;

void SniIconLoader::destroy(int handle) {
    if (handle >= 0 && vg_) nvgDeleteImage(vg_, handle);
}

int SniIconLoader::rasterizeSvg(const std::string& path) const {
#ifdef HAVE_LIBRSVG
    static constexpr int SIZE = 64;
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
    if (!handle) {
        if (err) g_error_free(err);
        return -1;
    }
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, SIZE, SIZE);
    cairo_t* cr = cairo_create(surf);
    RsvgRectangle vp = { 0.0, 0.0, (double)SIZE, (double)SIZE };
    rsvg_handle_render_document(handle, cr, &vp, &err);
    if (err) { g_error_free(err); err = nullptr; }
    cairo_destroy(cr);
    g_object_unref(handle);
    cairo_surface_flush(surf);
    const unsigned char* src = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    // Cairo ARGB32 (premultiplied, little-endian bytes: B G R A) → NanoVG RGBA straight alpha
    std::vector<unsigned char> rgba(SIZE * SIZE * 4);
    for (int y = 0; y < SIZE; y++) {
        const unsigned char* row = src + y * stride;
        for (int x = 0; x < SIZE; x++) {
            unsigned char b = row[x * 4 + 0];
            unsigned char g = row[x * 4 + 1];
            unsigned char r = row[x * 4 + 2];
            unsigned char a = row[x * 4 + 3];
            if (a > 0) {
                r = (unsigned char)(std::min(255, (int)r * 255 / a));
                g = (unsigned char)(std::min(255, (int)g * 255 / a));
                b = (unsigned char)(std::min(255, (int)b * 255 / a));
            }
            rgba[(y * SIZE + x) * 4 + 0] = r;
            rgba[(y * SIZE + x) * 4 + 1] = g;
            rgba[(y * SIZE + x) * 4 + 2] = b;
            rgba[(y * SIZE + x) * 4 + 3] = a;
        }
    }
    cairo_surface_destroy(surf);
    return nvgCreateImageRGBA(vg_, SIZE, SIZE, 0, rgba.data());
#else
    (void)path;
    return -1;
#endif
}

int SniIconLoader::fromPixmap(const std::vector<std::tuple<int32_t, int32_t, std::vector<uint8_t>>>& pixmaps) {
    if (!vg_ || pixmaps.empty()) return -1;

    // A D-Bus peer can push an arbitrarily large IconPixmap; clamp so a hostile
    // or buggy client cannot force a huge allocation + linear scan in the poll
    // loop. Reject any entry whose width or height exceeds MAX_ICON_DIM.
    static constexpr int MAX_ICON_DIM = 512;

    // Pick the entry closest to 20x20: smallest with width >= 20, else largest.
    // Oversized entries are skipped in favour of a valid smaller candidate.
    int bestIdx = -1;
    int bestW = 0;
    for (int i = 0; i < (int)pixmaps.size(); ++i) {
        int w = std::get<0>(pixmaps[i]);
        int h = std::get<1>(pixmaps[i]);
        if (w <= 0 || h <= 0) continue;
        if (w > MAX_ICON_DIM || h > MAX_ICON_DIM) continue;
        if (bestIdx < 0) { bestIdx = i; bestW = w; continue; }
        if (bestW < 20 && w > bestW) { bestIdx = i; bestW = w; }
        else if (bestW >= 20 && w >= 20 && w < bestW) { bestIdx = i; bestW = w; }
    }
    if (bestIdx < 0) return -1;

    int32_t w = std::get<0>(pixmaps[bestIdx]);
    int32_t h = std::get<1>(pixmaps[bestIdx]);
    const auto& bytes = std::get<2>(pixmaps[bestIdx]);
    if (w <= 0 || h <= 0) return -1;
    size_t expected = (size_t)w * (size_t)h * 4;
    if (bytes.size() < expected) {
        std::fprintf(stderr, "SniIconLoader: pixmap bytes %zu < expected %zu\n",
                     bytes.size(), expected);
        return -1;
    }

    // ARGB32 big-endian: [A, R, G, B] per pixel.
    // NanoVG nvgCreateImageRGBA expects [R, G, B, A] straight-alpha.
    std::vector<unsigned char> rgba(expected);
    for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) {
        uint8_t a = bytes[i * 4 + 0];
        uint8_t r = bytes[i * 4 + 1];
        uint8_t g = bytes[i * 4 + 2];
        uint8_t b = bytes[i * 4 + 3];
        if (a > 0) {
            r = (uint8_t)std::min(255, (int)r * 255 / a);
            g = (uint8_t)std::min(255, (int)g * 255 / a);
            b = (uint8_t)std::min(255, (int)b * 255 / a);
        }
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }

    return nvgCreateImageRGBA(vg_, w, h, 0, rgba.data());
}

static bool fexists(const std::string& p) { return fs::exists(p); }

static std::string findIconInThemePath(const std::string& name, const std::string& themePath) {
    if (name.empty() || themePath.empty()) return {};

    static const char* sizes[] = {
        "512x512", "256x256", "192x192", "128x128", "96x96",
        "64x64", "48x48", "32x32", "22x22", "16x16", nullptr
    };

    // themePath may be a colon-separated list of dirs
    std::string remaining = themePath;
    while (!remaining.empty()) {
        std::string base;
        auto colon = remaining.find(':');
        if (colon == std::string::npos) { base = remaining; remaining.clear(); }
        else { base = remaining.substr(0, colon); remaining = remaining.substr(colon + 1); }
        if (base.empty()) continue;

        // Try <base>/<size>/apps/<name>.png
        for (int i = 0; sizes[i]; ++i) {
            std::string p = base + "/" + sizes[i] + "/apps/" + name + ".png";
            if (fexists(p)) return p;
        }
        // Try <base>/scalable/apps/<name>.svg
        std::string p = base + "/scalable/apps/" + name + ".svg";
        if (fexists(p)) return p;
        // Try <base>/<name>.png directly
        p = base + "/" + name + ".png";
        if (fexists(p)) return p;
    }
    return {};
}

int SniIconLoader::fromIconName(const std::string& name, const std::string& themePath) {
    if (!vg_ || name.empty()) return -1;

    // Absolute path
    if (name[0] == '/') {
        if (fexists(name)) {
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".svg") == 0)
                return rasterizeSvg(name);   // nvgCreateImage cannot decode SVG
            return nvgCreateImage(vg_, name.c_str(), 0);
        }
        std::string png = name + ".png";
        if (fexists(png)) return nvgCreateImage(vg_, png.c_str(), 0);
        return -1;
    }

    // 1. Search the client-provided theme path first
    std::string path = findIconInThemePath(name, themePath);
    if (!path.empty()) {
        if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".svg") == 0) {
            // SVG: rasterize inline with librsvg (mirrors IconLoader::rasterizeSvg).
            // We fall through to the hicolor path if librsvg is absent.
            int h = rasterizeSvg(path);
            if (h >= 0) return h;
        } else {
            return nvgCreateImage(vg_, path.c_str(), 0);
        }
    }

    // 2. Fall back to hicolor dirs (mirrors IconLoader::findIconFile logic)
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    std::vector<std::string> bases;
    if (!home.empty()) {
        bases.push_back(home + "/.local/share/icons/hicolor/");
        bases.push_back(home + "/.icons/hicolor/");
    }
    bases.push_back("/usr/share/icons/hicolor/");

    static const char* sizes[] = {
        "512x512", "256x256", "192x192", "128x128", "96x96",
        "64x64", "48x48", "32x32", nullptr
    };
    for (const auto& base : bases)
        for (int i = 0; sizes[i]; ++i) {
            std::string p = base + sizes[i] + "/apps/" + name + ".png";
            if (fexists(p)) return nvgCreateImage(vg_, p.c_str(), 0);
        }
    for (const auto& base : bases) {
        std::string p = base + "scalable/apps/" + name + ".svg";
        if (fexists(p)) {
            int h = rasterizeSvg(p);
            if (h >= 0) return h;
        }
    }

    // 3. Pixmaps dir
    {
        std::string p = "/usr/share/pixmaps/" + name + ".png";
        if (fexists(p)) return nvgCreateImage(vg_, p.c_str(), 0);
    }

    return -1;
}
